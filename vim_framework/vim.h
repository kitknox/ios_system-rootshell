/* vim.h - Public header for vim.framework
 *
 * This framework provides vim text editor functionality for iOS apps
 * through the ios_system framework.
 */

#ifndef VIM_FRAMEWORK_H
#define VIM_FRAMEWORK_H

#include <Foundation/Foundation.h>

/* Entry point for vim editor */
__attribute__((visibility("default")))
int vim_main(int argc, char **argv);

#endif /* VIM_FRAMEWORK_H */
