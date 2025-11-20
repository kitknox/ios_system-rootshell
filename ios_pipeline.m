/*
 * ios_pipeline.c
 * Pipeline scheduler implementation
 */

#include "ios_pipeline.h"
#include "ios_system.h"
#include "ios_thread_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>

// Maximum stages in a pipeline
#define MAX_PIPELINE_STAGES 32

// Pipeline stage structure
struct _ios_pipeline_stage {
    char* command;                          // Command string
    FILE* stdin_stream;                     // Input stream
    FILE* stdout_stream;                    // Output stream
    FILE* stderr_stream;                    // Error stream
    ios_buffered_pipe_t* output_pipe;       // Pipe to next stage (if any)
    pthread_t thread_id;                    // Thread ID
    _Atomic(ios_pipeline_stage_status_t) status;  // Current status
    _Atomic(int) exit_code;                 // Exit code when completed
    void* session;                          // Session context
};

// Pipeline structure
struct _ios_pipeline {
    ios_pipeline_stage_t stages[MAX_PIPELINE_STAGES];
    int num_stages;
    bool share_stderr;

    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t all_complete;

    // Statistics
    _Atomic(int) running_count;
    _Atomic(int) completed_count;
    _Atomic(bool) all_started;
};

// Helper: Execute a single pipeline stage
static void* pipeline_stage_thread(void* arg) {
    ios_pipeline_stage_t* stage = (ios_pipeline_stage_t*)arg;

    // Mark as running
    atomic_store(&stage->status, IOS_STAGE_RUNNING);

    // Execute the command with redirected I/O
    // Note: ios_system will use the thread-local streams
    extern __thread FILE* thread_stdin;
    extern __thread FILE* thread_stdout;
    extern __thread FILE* thread_stderr;

    thread_stdin = stage->stdin_stream;
    thread_stdout = stage->stdout_stream;
    thread_stderr = stage->stderr_stream;

    int result = ios_system(stage->command);

    // Close output streams (but not stdin - that's owned by previous stage)
    if (stage->stdout_stream && stage->stdout_stream != stdout) {
        fclose(stage->stdout_stream);
        stage->stdout_stream = NULL;
    }

    if (stage->stderr_stream && stage->stderr_stream != stderr &&
        stage->stderr_stream != stage->stdout_stream) {
        fclose(stage->stderr_stream);
        stage->stderr_stream = NULL;
    }

    // Update status and exit code
    atomic_store(&stage->exit_code, result);
    if (result == 0) {
        atomic_store(&stage->status, IOS_STAGE_COMPLETED);
    } else {
        atomic_store(&stage->status, IOS_STAGE_FAILED);
    }

    return NULL;
}

// Helper: Parse command into pipeline stages
static int parse_pipeline(const char* command, char** stages, int max_stages, bool* share_stderr) {
    int num_stages = 0;
    *share_stderr = false;

    // Make a copy we can modify
    char* cmd_copy = strdup(command);
    char* ptr = cmd_copy;
    char* stage_start = ptr;

    // Simple pipeline parsing (doesn't handle quotes properly yet - TODO)
    while (*ptr && num_stages < max_stages) {
        if (*ptr == '|') {
            // Check for &| or |&
            if (ptr > cmd_copy && *(ptr-1) == '&') {
                *share_stderr = true;
                *(ptr-1) = '\0';
            } else if (*(ptr+1) == '&') {
                *share_stderr = true;
                *(ptr+1) = ' ';  // Replace & with space
            }

            *ptr = '\0';  // Terminate current stage

            // Trim spaces
            while (*stage_start == ' ') stage_start++;
            char* stage_end = ptr - 1;
            while (stage_end > stage_start && *stage_end == ' ') {
                *stage_end = '\0';
                stage_end--;
            }

            if (strlen(stage_start) > 0) {
                stages[num_stages++] = strdup(stage_start);
            }

            ptr++;
            stage_start = ptr;
        } else {
            ptr++;
        }
    }

    // Last stage
    if (*stage_start) {
        while (*stage_start == ' ') stage_start++;
        if (strlen(stage_start) > 0) {
            // Remove trailing spaces
            char* end = stage_start + strlen(stage_start) - 1;
            while (end > stage_start && *end == ' ') {
                *end = '\0';
                end--;
            }
            stages[num_stages++] = strdup(stage_start);
        }
    }

    free(cmd_copy);
    return num_stages;
}

/**
 * Execute a pipeline
 */
ios_pipeline_t* ios_pipeline_execute(const char* command, const ios_pipeline_options_t* options) {
    if (!command || !options) {
        return NULL;
    }

    ios_pipeline_t* pipeline = calloc(1, sizeof(ios_pipeline_t));
    if (!pipeline) {
        return NULL;
    }

    pthread_mutex_init(&pipeline->mutex, NULL);
    pthread_cond_init(&pipeline->all_complete, NULL);
    atomic_init(&pipeline->running_count, 0);
    atomic_init(&pipeline->completed_count, 0);
    atomic_init(&pipeline->all_started, false);

    // Parse pipeline into stages
    char* stage_commands[MAX_PIPELINE_STAGES];
    pipeline->num_stages = parse_pipeline(command, stage_commands, MAX_PIPELINE_STAGES,
                                          &pipeline->share_stderr);

    if (pipeline->num_stages == 0) {
        free(pipeline);
        return NULL;
    }

    // Set up each stage
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_t* stage = &pipeline->stages[i];

        stage->command = stage_commands[i];
        stage->session = options->session;
        atomic_init(&stage->status, IOS_STAGE_PENDING);
        atomic_init(&stage->exit_code, 0);
        stage->output_pipe = NULL;
        stage->thread_id = 0;

        // Set up stdin
        if (i == 0) {
            // First stage: use provided input or stdin
            stage->stdin_stream = options->input ? options->input : stdin;
        } else {
            // Read from previous stage's output pipe
            stage->stdin_stream = ios_pipe_fdopen_read(pipeline->stages[i-1].output_pipe);
        }

        // Set up stdout
        if (i == pipeline->num_stages - 1) {
            // Last stage: use provided output or stdout
            stage->stdout_stream = options->output ? options->output : stdout;
        } else {
            // Create pipe to next stage
            stage->output_pipe = ios_pipe_create_default();
            if (!stage->output_pipe) {
                // Cleanup and fail
                for (int j = 0; j <= i; j++) {
                    free(pipeline->stages[j].command);
                    if (pipeline->stages[j].output_pipe) {
                        ios_pipe_destroy(pipeline->stages[j].output_pipe);
                    }
                }
                free(pipeline);
                return NULL;
            }
            stage->stdout_stream = ios_pipe_fdopen_write(stage->output_pipe);
        }

        // Set up stderr
        if (pipeline->share_stderr || i == pipeline->num_stages - 1) {
            // Share stderr with stdout, or use provided error stream for last stage
            if (i == pipeline->num_stages - 1 && options->error) {
                stage->stderr_stream = options->error;
            } else {
                stage->stderr_stream = stage->stdout_stream;
            }
        } else {
            // Each stage gets its own stderr (TODO: could be optimized)
            stage->stderr_stream = options->error ? options->error : stderr;
        }
    }

    // Launch all stages in parallel
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_t* stage = &pipeline->stages[i];

        // Create thread for this stage
        if (pthread_create(&stage->thread_id, NULL, pipeline_stage_thread, stage) != 0) {
            fprintf(stderr, "[ios_pipeline] Failed to create thread for stage %d\n", i);
            atomic_store(&stage->status, IOS_STAGE_FAILED);
            atomic_store(&stage->exit_code, -1);
        } else {
            atomic_fetch_add(&pipeline->running_count, 1);
        }
    }

    atomic_store(&pipeline->all_started, true);

    return pipeline;
}

/**
 * Wait for pipeline to complete
 */
int ios_pipeline_wait(ios_pipeline_t* pipeline, int timeout_ms) {
    if (!pipeline) {
        return -1;
    }

    // Wait for all stages to complete
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_t* stage = &pipeline->stages[i];

        if (stage->thread_id != 0) {
            pthread_join(stage->thread_id, NULL);
            stage->thread_id = 0;

            atomic_fetch_sub(&pipeline->running_count, 1);
            atomic_fetch_add(&pipeline->completed_count, 1);
        }
    }

    // Signal completion
    pthread_mutex_lock(&pipeline->mutex);
    pthread_cond_broadcast(&pipeline->all_complete);
    pthread_mutex_unlock(&pipeline->mutex);

    // Return exit code of last stage
    if (pipeline->num_stages > 0) {
        return atomic_load(&pipeline->stages[pipeline->num_stages - 1].exit_code);
    }

    return 0;
}

/**
 * Check if pipeline is complete
 */
bool ios_pipeline_is_complete(ios_pipeline_t* pipeline) {
    if (!pipeline) {
        return true;
    }

    return atomic_load(&pipeline->completed_count) == pipeline->num_stages;
}

/**
 * Get pipeline statistics
 */
int ios_pipeline_get_stats(ios_pipeline_t* pipeline, ios_pipeline_stats_t* stats) {
    if (!pipeline || !stats) {
        return -1;
    }

    stats->num_stages = pipeline->num_stages;
    stats->running_stages = atomic_load(&pipeline->running_count);
    stats->completed_stages = atomic_load(&pipeline->completed_count);
    stats->failed_stages = 0;
    stats->all_started = atomic_load(&pipeline->all_started);
    stats->all_completed = ios_pipeline_is_complete(pipeline);

    // Count failed stages
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_status_t status = atomic_load(&pipeline->stages[i].status);
        if (status == IOS_STAGE_FAILED || status == IOS_STAGE_KILLED) {
            stats->failed_stages++;
        }
    }

    return 0;
}

/**
 * Kill pipeline
 */
int ios_pipeline_kill(ios_pipeline_t* pipeline) {
    if (!pipeline) {
        return -1;
    }

    // Send termination signal to all running stages
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_t* stage = &pipeline->stages[i];
        ios_pipeline_stage_status_t status = atomic_load(&stage->status);

        if (status == IOS_STAGE_RUNNING && stage->thread_id != 0) {
            pthread_kill(stage->thread_id, SIGINT);
            atomic_store(&stage->status, IOS_STAGE_KILLED);
        }
    }

    return 0;
}

/**
 * Destroy pipeline
 */
void ios_pipeline_destroy(ios_pipeline_t* pipeline) {
    if (!pipeline) {
        return;
    }

    // Wait for completion if still running
    if (!ios_pipeline_is_complete(pipeline)) {
        ios_pipeline_wait(pipeline, -1);
    }

    // Free resources
    for (int i = 0; i < pipeline->num_stages; i++) {
        ios_pipeline_stage_t* stage = &pipeline->stages[i];

        if (stage->command) {
            free(stage->command);
        }

        if (stage->output_pipe) {
            ios_pipe_destroy(stage->output_pipe);
        }
    }

    pthread_mutex_destroy(&pipeline->mutex);
    pthread_cond_destroy(&pipeline->all_complete);

    free(pipeline);
}

/**
 * Get stage information
 */
int ios_pipeline_get_stage_info(ios_pipeline_t* pipeline, int stage_index,
                                  const char** command, ios_pipeline_stage_status_t* status,
                                  int* exit_code) {
    if (!pipeline || stage_index < 0 || stage_index >= pipeline->num_stages) {
        return -1;
    }

    ios_pipeline_stage_t* stage = &pipeline->stages[stage_index];

    if (command) {
        *command = stage->command;
    }

    if (status) {
        *status = atomic_load(&stage->status);
    }

    if (exit_code) {
        *exit_code = atomic_load(&stage->exit_code);
    }

    return 0;
}
