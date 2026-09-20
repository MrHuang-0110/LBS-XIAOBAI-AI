#ifndef TEST_H
#define TEST_H
#include <stdio.h>

extern int g_test_pass;
extern int g_test_fail;

#define CHECK(cond) do { \
    if (cond) { g_test_pass++; } \
    else { g_test_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(got, want) do { \
    long _g = (long)(got), _w = (long)(want); \
    if (_g == _w) { g_test_pass++; } \
    else { g_test_fail++; printf("  FAIL %s:%d: %s (got %ld, want %ld)\n", \
                                 __FILE__, __LINE__, #got, _g, _w); } \
} while (0)

#endif
