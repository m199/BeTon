#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

/**
 * @brief Global debug flag.
 * Set to true to enable debug output to stdout.
 */
extern bool gIsDebug;

/**
 * @brief Debug logging macro.
 * Prints formatted output to stdout if gIsDebug is true.
 * Usage: DEBUG_PRINT("Value: %d\n", value);
 */
#define DEBUG_PRINT(fmt, ...)                                                  \
  do {                                                                         \
    if (gIsDebug) {                                                            \
      printf("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__);               \
    }                                                                          \
  } while (0)

#endif // DEBUG_H
