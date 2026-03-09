//
//  libc_replacement.c
//  ios_system
//
//  Created by Nicolas Holzschuch on 30/04/2018.
//  Copyright © 2018 Nicolas Holzschuch. All rights reserved.
//
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/select.h>
#include <time.h>
#include <poll.h>
#include <dlfcn.h>  // for dlopen()/dlsym()/dlclose()

#include "ios_error.h"
#include "ios_env_manager.h"
#undef write
#undef read
#undef select
#undef pselect
#undef poll
#undef fwrite
#undef fread
#undef fgetc
#undef fgets
#undef puts
#undef fputs
#undef fputc
#undef putw
#undef putp
#undef fflush
#undef getenv
#undef setenv
#undef unsetenv

// in order to run webAssembly commands sequentially, we first stack them, then run them in command line order:
// At this point, this could just be a mutex.

int preparingWebAssemblyCommands = 0;
int orderOfWebAssemblyCommands = 0;
void startedPreparingWebAssemblyCommand(void) {
    preparingWebAssemblyCommands += 1;
    orderOfWebAssemblyCommands += 1;
}

int webAssemblyCommandOrder(void) {
    return orderOfWebAssemblyCommands;
}

void finishedPreparingWebAssemblyCommand(void) {
    if (preparingWebAssemblyCommands > 0)
        preparingWebAssemblyCommands -= 1;
}

static void executeWebAssemblyCommandsInOrder(void) {
    void (*function)(void) = NULL;
    function = dlsym(RTLD_MAIN_ONLY, "executeWebAssemblyCommands");
    if (function == NULL) {
        // more extensive search, but more expensive too.
        function = dlsym(RTLD_DEFAULT, "executeWebAssemblyCommands");
    }
    if (function != NULL) {
        while (preparingWebAssemblyCommands > 0) {
            // Empty loops create problems in Release mode.
            if (thread_stdout != NULL) { fflush(thread_stdout); }
            if (thread_stderr != NULL) { fflush(thread_stderr); }
        }
        function();
    }
    orderOfWebAssemblyCommands = 0;
}

static int ios_wait_for_fd_or_cancel(int fd, short events, int timeout_ms) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return -1;
    }

    int cancel_fd = ios_sessionCancelFD();
    if (cancel_fd < 0 || cancel_fd == fd) {
        return 0;
    }

    struct pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = events | POLLERR | POLLHUP;
    fds[0].revents = 0;
    fds[1].fd = cancel_fd;
    fds[1].events = POLLIN | POLLERR | POLLHUP;
    fds[1].revents = 0;

    for (;;) {
        int result = poll(fds, 2, timeout_ms);
        if (result < 0) {
            if (errno == EINTR && !ios_sessionCancelRequested()) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            return 0;
        }
        if (fds[1].revents != 0) {
            errno = EINTR;
            return -1;
        }
        if (fds[0].revents & POLLNVAL) {
            errno = EBADF;
            return -1;
        }
        return 0;
    }
}

// Darwin/BSD stdio exposes buffered byte count and buffer pointer in FILE.
// These fields are not portable and this helper is intentionally platform-specific.
static size_t ios_stream_buffered_input(FILE *stream) {
    return (stream != NULL && stream->_r > 0) ? (size_t)stream->_r : 0;
}

static bool ios_fgets_satisfied_by_buffer(FILE *stream, int size) {
    size_t buffered = ios_stream_buffered_input(stream);
    if (size <= 1) {
        return true;
    }
    if (buffered >= (size_t)(size - 1)) {
        return true;
    }
    if (buffered == 0) {
        return false;
    }
    return memchr(stream->_p, '\n', buffered) != NULL;
}

static bool ios_should_abort_stdio_write(FILE *stream) {
    if (!ios_sessionCancelRequested() || stream == NULL) {
        return false;
    }

    if (thread_stdout != NULL && fileno(stream) == fileno(thread_stdout)) {
        errno = EINTR;
        return true;
    }
    if (fileno(stream) == STDOUT_FILENO) {
        errno = EINTR;
        return true;
    }
    return false;
}


int printf (const char *format, ...) {
    va_list arg;
    int done;
    
    va_start (arg, format);
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        va_end(arg);
        return -1;
    }
    done = vfprintf (thread_stdout, format, arg);
    va_end (arg);
    
    return done;
}

// #define debugPrint
int fprintf(FILE * restrict stream, const char * restrict format, ...) {
    va_list arg;
    int done;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (thread_stdout == NULL) thread_stdout = stdout;

    va_start (arg, format);
    if (stream != NULL && ios_should_abort_stdio_write(stream)) {
        va_end(arg);
        return -1;
    }
    if (fileno(stream) == STDOUT_FILENO) done = vfprintf (thread_stdout, format, arg);
#ifndef debugPrint
    else if (fileno(stream) == STDERR_FILENO) done = vfprintf (thread_stderr, format, arg);
    // iOS, debug:
#else
    else if ((fileno(stream) == STDERR_FILENO) || (fileno(stream) == fileno(thread_stderr))) done = vfprintf (stderr, format, arg);
#endif
    else done = vfprintf (stream, format, arg);
    va_end (arg);
    
    return done;
}
int scanf (const char *format, ...) {
    int             count;
    va_list ap;
    
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stdin == NULL) thread_stdin = stdin;

    fflush(thread_stdout);
    fflush(thread_stderr);
    if (thread_stdin != NULL && ios_wait_for_fd_or_cancel(fileno(thread_stdin), POLLIN, -1) < 0) {
        return EOF;
    }
    va_start (ap, format);
    count = vfscanf (thread_stdin, format, ap);
    va_end (ap);
    return (count);
}
int ios_fflush(FILE *stream) {
    if (stream == NULL) return 0;
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;

    if (fileno(stream) == STDOUT_FILENO) return fflush(thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fflush(thread_stderr);
    return fflush(stream);
}
ssize_t ios_write(int fildes, const void *buf, size_t nbyte) {
    if (ios_sessionCancelRequested() && (fildes == STDOUT_FILENO || (thread_stdout != NULL && fildes == fileno(thread_stdout)))) {
        errno = EINTR;
        return -1;
    }
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fildes == STDOUT_FILENO) return write(fileno(thread_stdout), buf, nbyte);
    if (fildes == STDERR_FILENO) return write(fileno(thread_stderr), buf, nbyte);
    return write(fildes, buf, nbyte);
}

ssize_t ios_read(int fildes, void *buf, size_t nbyte) {
    if (thread_stdin == NULL) thread_stdin = stdin;

    int effective_fd = fildes;
    if (fildes == STDIN_FILENO && thread_stdin != NULL) {
        effective_fd = fileno(thread_stdin);
    }

    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return -1;
    }

    int flags = fcntl(effective_fd, F_GETFL, 0);
    bool is_nonblocking = (flags >= 0) && ((flags & O_NONBLOCK) != 0);
    if (!is_nonblocking && ios_wait_for_fd_or_cancel(effective_fd, POLLIN, -1) < 0) {
        return -1;
    }

    for (;;) {
        ssize_t result = read(effective_fd, buf, nbyte);
        if (result < 0 && errno == EINTR && !ios_sessionCancelRequested()) {
            if (!is_nonblocking && ios_wait_for_fd_or_cancel(effective_fd, POLLIN, -1) < 0) {
                return -1;
            }
            continue;
        }
        return result;
    }
}

int ios_select(int nfds, fd_set *restrict readfds, fd_set *restrict writefds, fd_set *restrict errorfds, struct timeval *restrict timeout) {
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return -1;
    }

    int cancel_fd = ios_sessionCancelFD();
    if (cancel_fd < 0 || cancel_fd >= FD_SETSIZE) {
        return select(nfds, readfds, writefds, errorfds, timeout);
    }

    bool caller_watched_cancel_fd = (readfds != NULL) && FD_ISSET(cancel_fd, readfds);
    fd_set local_readfds;
    if (readfds != NULL) {
        local_readfds = *readfds;
    } else {
        FD_ZERO(&local_readfds);
    }
    FD_SET(cancel_fd, &local_readfds);

    int effective_nfds = (cancel_fd >= nfds) ? cancel_fd + 1 : nfds;
    int result = select(effective_nfds, &local_readfds, writefds, errorfds, timeout);
    if (result <= 0) {
        return result;
    }

    if (FD_ISSET(cancel_fd, &local_readfds)) {
        errno = EINTR;
        return -1;
    }

    if (readfds != NULL) {
        if (!caller_watched_cancel_fd) {
            FD_CLR(cancel_fd, &local_readfds);
        }
        *readfds = local_readfds;
    }
    return result;
}

int ios_pselect(int nfds, fd_set *restrict readfds, fd_set *restrict writefds, fd_set *restrict errorfds, const struct timespec *restrict timeout, const sigset_t *restrict sigmask) {
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return -1;
    }

    int cancel_fd = ios_sessionCancelFD();
    if (cancel_fd < 0 || cancel_fd >= FD_SETSIZE) {
        return pselect(nfds, readfds, writefds, errorfds, timeout, sigmask);
    }

    bool caller_watched_cancel_fd = (readfds != NULL) && FD_ISSET(cancel_fd, readfds);
    fd_set local_readfds;
    if (readfds != NULL) {
        local_readfds = *readfds;
    } else {
        FD_ZERO(&local_readfds);
    }
    FD_SET(cancel_fd, &local_readfds);

    int effective_nfds = (cancel_fd >= nfds) ? cancel_fd + 1 : nfds;
    int result = pselect(effective_nfds, &local_readfds, writefds, errorfds, timeout, sigmask);
    if (result <= 0) {
        return result;
    }

    if (FD_ISSET(cancel_fd, &local_readfds)) {
        errno = EINTR;
        return -1;
    }

    if (readfds != NULL) {
        if (!caller_watched_cancel_fd) {
            FD_CLR(cancel_fd, &local_readfds);
        }
        *readfds = local_readfds;
    }
    return result;
}

int ios_poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return -1;
    }

    int cancel_fd = ios_sessionCancelFD();
    if (cancel_fd < 0) {
        return poll(fds, nfds, timeout);
    }

    struct pollfd stack_fds[65];
    struct pollfd *local_fds = stack_fds;
    size_t needed_fds = (size_t)nfds + 1;
    bool heap_allocated = false;

    if (needed_fds > (sizeof(stack_fds) / sizeof(stack_fds[0]))) {
        if (needed_fds > (SIZE_MAX / sizeof(struct pollfd))) {
            errno = ENOMEM;
            return -1;
        }
        local_fds = malloc(needed_fds * sizeof(struct pollfd));
        if (local_fds == NULL) {
            errno = ENOMEM;
            return -1;
        }
        heap_allocated = true;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        local_fds[i] = fds[i];
        local_fds[i].revents = 0;
    }
    local_fds[nfds].fd = cancel_fd;
    local_fds[nfds].events = POLLIN | POLLERR | POLLHUP;
    local_fds[nfds].revents = 0;

    int result = poll(local_fds, nfds + 1, timeout);
    if (result <= 0) {
        if (heap_allocated) {
            free(local_fds);
        }
        return result;
    }

    if (local_fds[nfds].revents != 0) {
        errno = EINTR;
        if (heap_allocated) {
            free(local_fds);
        }
        return -1;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = local_fds[i].revents;
    }
    if (heap_allocated) {
        free(local_fds);
    }
    return result;
}

size_t ios_fwrite(const void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
#ifdef debugPrint
    return fwrite(ptr, size, nitems, stderr);
#endif
    if (stream != NULL && ios_should_abort_stdio_write(stream)) {
        return 0;
    }
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fwrite(ptr, size, nitems, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fwrite(ptr, size, nitems, thread_stderr);
    return fwrite(ptr, size, nitems, stream);
}

size_t ios_fread(void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream) {
    if (stream == NULL) {
        errno = EINVAL;
        return 0;
    }
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return 0;
    }
    size_t requested = 0;
    if (size != 0 && nitems > (SIZE_MAX / size)) {
        requested = SIZE_MAX;
    } else {
        requested = size * nitems;
    }
    if (ios_stream_buffered_input(stream) < requested &&
        ios_wait_for_fd_or_cancel(fileno(stream), POLLIN, -1) < 0) {
        return 0;
    }
    return fread(ptr, size, nitems, stream);
}

int ios_fgetc(FILE *stream) {
    if (stream == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return EOF;
    }
    if (ios_stream_buffered_input(stream) == 0 &&
        ios_wait_for_fd_or_cancel(fileno(stream), POLLIN, -1) < 0) {
        return EOF;
    }
    return fgetc(stream);
}

char *ios_fgets(char *restrict s, int size, FILE *restrict stream) {
    if (stream == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return NULL;
    }
    if (!ios_fgets_satisfied_by_buffer(stream, size) &&
        ios_wait_for_fd_or_cancel(fileno(stream), POLLIN, -1) < 0) {
        return NULL;
    }
    return fgets(s, size, stream);
}

int ios_puts(const char *s) {
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (ios_sessionCancelRequested()) {
        errno = EINTR;
        return EOF;
    }
    // puts adds a newline at the end.
    int returnValue = fputs(s, thread_stdout);
    fputc('\n', thread_stdout);
    return returnValue;
}
int ios_fputs(const char* s, FILE *stream) {
#ifdef debugPrint
    return fputs(s, stderr);
#endif
    if (stream != NULL && ios_should_abort_stdio_write(stream)) {
        return EOF;
    }
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fputs(s, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fputs(s, thread_stderr);
    return fputs(s, stream);
}
int ios_fputc(int c, FILE *stream) {
#ifdef debugPrint
    return fputc(c, stderr);
#endif
    if (stream != NULL && ios_should_abort_stdio_write(stream)) {
        return EOF;
    }
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return fputc(c, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return fputc(c, thread_stderr);
    return fputc(c, stream);
}

#include <assert.h>

int ios_putw(int w, FILE *stream) {
    if (stream != NULL && ios_should_abort_stdio_write(stream)) {
        return EOF;
    }
    if (thread_stdout == NULL) thread_stdout = stdout;
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fileno(stream) == STDOUT_FILENO) return putw(w, thread_stdout);
    if (fileno(stream) == STDERR_FILENO) return putw(w, thread_stderr);
    return putw(w, stream);
}

// Fake process IDs to go with fake forking:
// You will still need to edit your code to make sure you go through both branches.
#define IOS_MAX_THREADS 128
static pthread_t thread_ids[IOS_MAX_THREADS];
static int numVariablesSet[IOS_MAX_THREADS];
static char** environment[IOS_MAX_THREADS];
static char** copyEnvironment[IOS_MAX_THREADS];
static char previousDirectory[IOS_MAX_THREADS][MAXPATHLEN];
static int previousPid[IOS_MAX_THREADS];

static int pid_overflow = 0;
static __thread pid_t current_pid = 0;
// We need to lock current_pid during operations
pthread_mutex_t pid_mtx = PTHREAD_MUTEX_INITIALIZER;
_Atomic(int) cleanup_counter = 0;
static pid_t last_allocated_pid = 0;


void makeGlobal(void) {
    copyEnvironment[current_pid] = environment[current_pid];
    environment[current_pid] = NULL; // makes it really global
}
void makeLocal(void) {
    environment[current_pid] = copyEnvironment[current_pid];
    copyEnvironment[current_pid] = NULL;
}

inline pthread_t ios_getThreadId(pid_t pid) {
    // return ios_getLastThreadId(); // previous behaviour
    if (pid >= IOS_MAX_THREADS) { return -1; }
    return thread_ids[pid];
}

void newPreviousDirectory(void) {
    // Called when a command calls "cd". Actually changes the directory for that command.
    getwd(previousDirectory[current_pid]);
}

// We do not recycle process ids too quickly to avoid collisions.
void storeEnvironment(char* envp[]);
static inline const pid_t ios_nextAvailablePid(void) {
    while (cleanup_counter > 0) { } // Don't start a command while another is ending.
    // fprintf(stderr, "Locking in ios_nextAvailablePid\n");
    pthread_mutex_lock(&pid_mtx);
    char** currentEnvironment = environmentVariables(current_pid);
    int previousPidId = current_pid;
    if (!pid_overflow && (last_allocated_pid < IOS_MAX_THREADS - 1)
        && (thread_ids[last_allocated_pid+1] == 0)) {
        current_pid = last_allocated_pid + 1;
        last_allocated_pid = current_pid;
        thread_ids[current_pid] = -1; // Not yet started
        numVariablesSet[current_pid] = 0;
        environment[current_pid] = NULL;
        storeEnvironment(currentEnvironment); // duplicate the environment variables
        getwd(previousDirectory[current_pid]); // store current working directory
        previousPid[current_pid] = previousPidId;
        // fprintf(stderr, "Returning from ios_nextAvailablePid, pid= %d\n", current_pid);
        return current_pid;
    }
    // We've already started more than IOS_MAX_THREADS threads.
    if (!pid_overflow) current_pid = 0; // first time over the limit
    pid_overflow = 1;
    while (1) {
        current_pid = last_allocated_pid + 1;
        last_allocated_pid = current_pid;
        if (current_pid >= IOS_MAX_THREADS) {
            current_pid = 1;
            last_allocated_pid = 1;
        }
        pthread_t thread_pid = ios_getThreadId(current_pid);
        if (thread_pid == 0) { // We found a not-active pid
            thread_ids[current_pid] = -1; // Not yet started
            numVariablesSet[current_pid] = 0;
            environment[current_pid] = NULL;
            storeEnvironment(currentEnvironment); // duplicate the environment variables
            getwd(previousDirectory[current_pid]); // store current working directory
            previousPid[current_pid] = previousPidId;
            // fprintf(stderr, "Returning from ios_nextAvailablePid, pid= %d\n", current_pid);
            return current_pid;
        }
        // Dangerous: if the process is already killed, this wil crash
        /*
        if (pthread_kill(thread_pid, 0) != 0) {
            thread_ids[current_pid] = 0;
            return current_pid; // not running anymore
        }
        */
    }
}

inline void ios_storeThreadId(pthread_t thread) {
    // To avoid issues when a command starts a command without forking,
    // we only store thread IDs for the first thread of the "process".
    // fprintf(stderr, "Unlocking pid %d, storing thread %x current value: %x\n", current_pid, thread,  thread_ids[current_pid]);
    if (thread_ids[current_pid] == -1) {
        thread_ids[current_pid] = thread;
    }
    pthread_mutex_unlock(&pid_mtx);
}

char* libc_getenv(const char* variableName) {
    // Use new thread-safe per-thread environment manager
    return ios_env_getenv(variableName);
}

extern void set_session_errno(int n);
int ios_setenv_pid(const char* variableName, const char* value, int overwrite, int pid) {
    if (environment[pid] != NULL) {
        if (variableName == NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        if (strlen(variableName) == 0) {
            set_session_errno(EINVAL);
            return -1;
        }
        char* position = strchr(variableName,'=');
        if (position != NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        char** envp = environment[pid];
        unsigned long varNameLen = strlen(variableName);
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if (strncmp(variableName, envp[i], varNameLen) == 0) {
                if (strlen(envp[i]) > varNameLen) {
                    if (envp[i][varNameLen] == '=') {
                        // This variable is defined in the current environment:
                        if (overwrite == 0) { return 0; }
                        envp[i] = realloc(envp[i], strlen(variableName) + strlen(value) + 2);
                        sprintf(envp[i], "%s=%s", variableName, value);
                        return 0;
                    }
                }
            }
        }
        // Not found so far, add it to the list:
        int pos = numVariablesSet[pid];
        environment[pid] = realloc(envp, (numVariablesSet[pid] + 2) * sizeof(char*));
        environment[pid][pos] = malloc(strlen(variableName) + strlen(value) + 2);
        environment[pid][pos + 1] = NULL;
        sprintf(environment[pid][pos], "%s=%s", variableName, value);
        numVariablesSet[pid] += 1;
        return 0;
    } else {
        return setenv(variableName, value, overwrite);
    }
}

int ios_setenv_parent(const char* variableName, const char* value, int overwrite) {
    return ios_setenv_pid(variableName, value, overwrite, previousPid[current_pid]);
}

int ios_setenv(const char* variableName, const char* value, int overwrite) {
    // Use new thread-safe per-thread environment manager
    return ios_env_setenv(variableName, value, overwrite);
}

int ios_putenv(char* string) {
    if (environment[current_pid] != NULL) {
        unsigned length;
        char     *temp;

        /*  Find the length of the "NAME="  */
        temp = strchr(string,'=');
        if ( temp == 0 ) {
            set_session_errno(EINVAL);
            return( -1 );
        }
        length = (unsigned) (temp - string + 1);

        /*  Scan through the environment looking for "NAME="  */
        char** envp = environment[current_pid];

        for (int i = 0; i < numVariablesSet[current_pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if ( strncmp( string, envp[i], length ) == 0 ) {
                // Found it. Copy in place.
                envp[i] = realloc(envp[i], strlen(string) + 1);
                memcpy(envp[i], string, strlen(string) + 1);
                return 0;
            }
        }
        // Not found so far, add it to the list:
        int pos = numVariablesSet[current_pid];
        environment[current_pid] = realloc(envp, (numVariablesSet[current_pid] + 2) * sizeof(char*));
        environment[current_pid][pos] = malloc(strlen(string) + 1);
        environment[current_pid][pos + 1] = NULL;
        memcpy(environment[current_pid][pos], string, strlen(string) + 1);
        numVariablesSet[current_pid] += 1;
        return 0;
    } else {
        return putenv(string);
    }
}

int ios_unsetenv_pid(const char* variableName, int pid) {
    // Someone calls unsetenv once the process has been terminated.
    // Best thing to do is erase the environment and return
    if (environment[pid] != NULL) {
        if (variableName == NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        if (strlen(variableName) == 0) {
            set_session_errno(EINVAL);
            return -1;
        }
        char* position = strchr(variableName,'=');
        if (position != NULL) {
            set_session_errno(EINVAL);
            return -1;
        }
        char** envp = environment[pid];
        unsigned long varNameLen = strlen(variableName);
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (envp[i] == NULL) { continue; }
            if (strncmp(variableName, envp[i], varNameLen) == 0) {
                if (strlen(envp[i]) > varNameLen) {
                    if (envp[i][varNameLen] == '=') {
                        // This variable is defined in the current environment:
                        free(envp[i]);
                        envp[i] = NULL;
                        if (i < numVariablesSet[pid] - 1) {
                            for (int j = i; j < numVariablesSet[pid] - 1; j++) {
                                envp[j] = envp[j+1];
                            }
                            envp[numVariablesSet[pid] - 1] = NULL;
                        }
                        numVariablesSet[pid] -= 1;
                        environment[pid] = realloc(envp, (numVariablesSet[pid] + 1) * sizeof(char*));
                        return 0;
                    }
                }
            }
        }
        /*
         for (int i = 0; i < numVariablesSet[pid]; i++) {
         char* position = strstr(envp[i],"=");
         if (strncmp(variableName, envp[i], position - envp[i]) == 0) {
         }
         } */
        // Not found:
        return 0;
    } else {
        return unsetenv(variableName);
    }
}

int ios_unsetenv_parent(const char* variableName) {
    return ios_unsetenv_pid(variableName, previousPid[current_pid]);
}

int ios_unsetenv(const char* variableName) {
    // Use new thread-safe per-thread environment manager
    return ios_env_unsetenv(variableName);
}


// store environment variables (called from execve)
// Copy the entire environment:
extern char** environ;
void resetEnvironment(pid_t pid);
void storeEnvironment(char* envp[]) {
    if (environment[current_pid] != NULL) {
        // We already allocated one environment. Let's clean it:
        resetEnvironment(current_pid);
    }
    int i = 0;
    while (envp[i] != NULL) {
        i++;
    }
    numVariablesSet[current_pid] = i;
    environment[current_pid] = malloc((numVariablesSet[current_pid] + 1) * sizeof(char*));
    for (int i = 0; i < numVariablesSet[current_pid]; i++) {
        if (envp[i] != NULL)
            environment[current_pid][i] = strdup(envp[i]);
        else
            environment[current_pid][i] = NULL;
    }
    // Keep NULL-termination:
    environment[current_pid][numVariablesSet[current_pid]] = NULL;
}

// when the command is terminated, release the environment variables that were added.
void resetEnvironment(pid_t pid) {
    if (environment[pid] != NULL) {
        // Free the variables allocated:
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (environment[pid][i] == NULL) { continue; }
            free(environment[pid][i]);
            environment[pid][i] = NULL;
        }
        free(environment[pid]);
        environment[pid] = NULL;
        numVariablesSet[pid] = 0;
    }
}

// Used by "env -i": clear all environment variables, but don't clear the environment itself
void clearEnvironment(pid_t pid) {
    if (environment[pid] != NULL) {
        // Free the variables allocated:
        for (int i = 0; i < numVariablesSet[pid]; i++) {
            if (environment[pid][i] == NULL) { continue; }
            free(environment[pid][i]);
            environment[pid][i] = NULL;
        }
        numVariablesSet[pid] = 0;
    }
}


char** environmentVariables(pid_t pid) {
    if (environment[pid] != NULL) {
        return environment[pid];
    } else {
        return environ;
    }
}

extern int chdir_nolock(const char* path); // defined in ios_system.m
void ios_releaseThread(pthread_t thread) {
    if (thread == NULL) {
        return;
    }
    // TODO: this is inefficient. Replace with NSMutableArray?
    for (int p = 0; p < IOS_MAX_THREADS; p++) {
        if (thread_ids[p] == thread) {
            // fprintf(stderr, "Found Id %d\n", p);
            // Don't reset the environment; sometimes, commands try to change the environment while it is being erased.
            // resetEnvironment(p);
            // fprintf(stderr, "Reset current directory to %s because process %d terminates\n", previousDirectory[p], p);
            current_pid = previousPid[p];
            thread_ids[p] = NULL;
            chdir_nolock(previousDirectory[p]);
            return;
        }
    }
    // fprintf(stderr, "Not found\n");
}

void ios_releaseBackgroundThread(pthread_t thread) {
    // Same as ios_releaseThread, but do not reset the directory.
    for (int p = 0; p < IOS_MAX_THREADS; p++) {
        if (thread_ids[p] == thread) {
            // fprintf(stderr, "Found Id %d\n", p);
            current_pid = previousPid[p];
            thread_ids[p] = NULL;
            return;
        }
    }
    // fprintf(stderr, "Not found\n");
}

void ios_releaseThreadId(pid_t pid) {
    // Don't reset the environment; sometimes, commands try to change the environment while it is being erased.
    // resetEnvironment(pid);
    if (thread_ids[pid] != 0) {
        // fprintf(stderr, "Locking for pid %d in ios_releaseThreadId\n", pid);
        // fprintf(stderr, "Reset current directory to %s because process %d terminates\n", previousDirectory[pid], pid);
        chdir_nolock(previousDirectory[pid]);
        current_pid = previousPid[pid];
        thread_ids[pid] = 0;
        // fprintf(stderr, "Unlocking for pid %d in ios_releaseThreadId\n", pid);
    } else {
        // fprintf(stderr, "ios_releaseThreadId: pid %d was already terminated.\n", pid);
    }
}

pid_t ios_currentPid(void) {
    return current_pid;
}

void ios_setCurrentPid(pid_t pid) {
    current_pid = pid;
}

// Note to self: do not redefine getpid() unless you have a way to make it consistent even when a "process" starts a new thread.
// 0MQ and asyncio rely on this.
pid_t fork(void) { return ios_nextAvailablePid(); } // increases current_pid by 1.
pid_t ios_fork(void) { return ios_nextAvailablePid(); } // increases current_pid by 1.
pid_t vfork(void) { return ios_nextAvailablePid(); }

// simple replacement of waitpid for swift programs
// We use "optnone" to prevent optimization, otherwise the while loops never end.
__attribute__ ((optnone)) void ios_waitpid(pid_t pid) {
    
    executeWebAssemblyCommandsInOrder();
    
    pthread_t threadToWaitFor;
    // Old system: no explicit pid, just store last thread Id.
    if ((pid == -1) || (pid == 0)) {
        threadToWaitFor = ios_getLastThreadId();
        while (threadToWaitFor != 0) {
            threadToWaitFor = ios_getLastThreadId();
        }
        return;
    }
    // New system: thread Id is store with pid:
    threadToWaitFor = ios_getThreadId(pid);
    while (threadToWaitFor != 0) {
        // -1: not started, >0 started, not finished, 0: finished
        threadToWaitFor = ios_getThreadId(pid);
    }
    // fprintf(stderr, "Returning from ios_waitpid for %d \n", pid);
    return;
}

__attribute__ ((optnone)) pid_t waitpid(pid_t pid, int *stat_loc, int options) {
    // pthread_join won't work,  because the thread might have been detached.
    // (and you can't re-join a detached thread).
    // -1 = the call waits for any child process (not good yet)
    //  0 = the call waits for any child process in the process group of the caller
    
    if (options && WNOHANG) {
        executeWebAssemblyCommandsInOrder(); // start executing webAssembly commands
        // WNOHANG: just check that the process is still running:
        pthread_t threadToWaitFor;
        if ((pid == -1) || (pid == 0)) threadToWaitFor = ios_getLastThreadId();
        else threadToWaitFor = ios_getThreadId(pid);
        if (threadToWaitFor != 0) // the process is still running
            return 0;
        else {
            if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
            fflush(thread_stdout);
            fflush(thread_stderr);
            return pid; // was "-1". See man page and https://github.com/holzschu/ios_system/issues/89
        }
    } else {
        // Wait until the process is terminated:
        ios_waitpid(pid);
        if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
        return pid;
    }
}

// The previous function is designed to override the standard version of waitpid.
// With Python 3.13, it doesn't work and the standard version is called instead.
// When that happens, we explicitly call this function (a copy of the previous one).
__attribute__ ((optnone)) pid_t ios_full_waitpid(pid_t pid, int *stat_loc, int options) {
    // pthread_join won't work,  because the thread might have been detached.
    // (and you can't re-join a detached thread).
    // -1 = the call waits for any child process (not good yet)
    //  0 = the call waits for any child process in the process group of the caller
    
    if (options && WNOHANG) {
        executeWebAssemblyCommandsInOrder(); // start executing webAssembly commands
        // WNOHANG: just check that the process is still running:
        pthread_t threadToWaitFor;
        if ((pid == -1) || (pid == 0)) threadToWaitFor = ios_getLastThreadId();
        else threadToWaitFor = ios_getThreadId(pid);
        if (threadToWaitFor != 0) // the process is still running
            return 0;
        else {
            if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
            fflush(thread_stdout);
            fflush(thread_stderr);
            return pid; // was "-1". See man page and https://github.com/holzschu/ios_system/issues/89
        }
    } else {
        // Wait until the process is terminated:
        ios_waitpid(pid);
        if (stat_loc) *stat_loc = W_EXITCODE(ios_getCommandStatus(), 0);
        return pid;
    }
}



//
void vwarn(const char *fmt, va_list args)
{
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    if (fmt != NULL)
    {
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, args);
    }
    fputs(": ", thread_stderr);
    fputs(strerror(errno), thread_stderr);
    putc('\n', thread_stderr);
}

void vwarnx(const char *fmt, va_list args)
{
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    fputs(": ", thread_stderr);
    if (fmt != NULL)
        vfprintf(thread_stderr, fmt, args);
    putc('\n', thread_stderr);
}
// void err(int eval, const char *fmt, ...);
void err(int eval, const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
    ios_exit(eval);
}
// void errc(int eval, int errorcode, const char *fmt, ...);
void errc(int eval, int errorcode, const char *fmt, ...) {
    if (thread_stderr == NULL) thread_stderr = stderr;
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        fputs(ios_progname(), thread_stderr);
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, argptr);
        fputs(": ", thread_stderr);
        fputs(strerror(errorcode), thread_stderr);
        putc('\n', thread_stderr);
        va_end(argptr);
    }
    ios_exit(eval);
}
//     void errx(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarnx(fmt, argptr);
        va_end(argptr);
    }
    ios_exit(eval);
}
//   void warn(const char *fmt, ...);
void warn(const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
}
//   void warnx(const char *fmt, ...);
void warnx(const char *fmt, ...) {
    if (fmt != NULL) {
        va_list argptr;
        va_start(argptr, fmt);
        vwarnx(fmt, argptr);
        va_end(argptr);
    }
}
// void warnc(int code, const char *fmt, ...);
void warnc(int code, const char *fmt, ...) {
    if (thread_stderr == NULL) thread_stderr = stderr;
    fputs(ios_progname(), thread_stderr);
    if (fmt != NULL)
    {
        va_list argptr;
        va_start(argptr, fmt);
        fputs(": ", thread_stderr);
        vfprintf(thread_stderr, fmt, argptr);
        vwarn(fmt, argptr);
        va_end(argptr);
    }
    fputs(": ", thread_stderr);
    fputs(strerror(code), thread_stderr);
    putc('\n', thread_stderr);
}
