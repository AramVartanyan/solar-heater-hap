/* Created by Aram Vartanyan (c) 2026 */

/* Minimal checks for the host tests (no framework). */
#pragma once

#include <stdio.h>

static int s_checks, s_failed;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        s_checks++;                                                                 \
        if (!(cond)) {                                                              \
            s_failed++;                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        long _a = (long)(a), _b = (long)(b);                                        \
        s_checks++;                                                                 \
        if (_a != _b) {                                                             \
            s_failed++;                                                             \
            printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); \
        }                                                                           \
    } while (0)

static int test_summary(const char *name)
{
    printf("%s: %d checks, %d failed\n", name, s_checks, s_failed);
    return s_failed ? 1 : 0;
}
