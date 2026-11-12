/* Host tests: counting the checks. */
#pragma once

#include <stdio.h>

static int t_checks, t_failed;

#define CHECK(cond) \
    do { \
        t_checks++; \
        if (!(cond)) { \
            t_failed++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* Prints the summary; the exit code of the test. */
static inline int test_result(const char *name)
{
    printf("%s: %d checks, %d failed\n", name, t_checks, t_failed);
    return t_failed ? 1 : 0;
}
