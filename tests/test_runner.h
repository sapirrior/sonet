#ifndef NPS_TEST_RUNNER_H
#define NPS_TEST_RUNNER_H

#include <stdio.h>
#include <string.h>

extern int tests_passed;
extern int tests_failed;

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s is false\n", __FILE__, __LINE__, #condition); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if (condition) { \
        fprintf(stderr, "FAIL %s:%d: %s is true\n", __FILE__, __LINE__, #condition); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        fprintf(stderr, "FAIL %s:%d: %s is not NULL\n", __FILE__, __LINE__, #a); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define ASSERT_NOT_NULL(a) do { \
    if ((a) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: %s is NULL\n", __FILE__, __LINE__, #a); \
        tests_failed++; \
    } else { tests_passed++; } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("Running %s...\n", #test_func); \
    test_func(); \
} while(0)

#define TEST_SUITE_MAIN(test_cases) \
    int tests_passed = 0; \
    int tests_failed = 0; \
    int main(void) { \
        test_cases(); \
        printf("\n==================================\n"); \
        printf("Tests passed: %d\n", tests_passed); \
        printf("Tests failed: %d\n", tests_failed); \
        printf("==================================\n"); \
        return tests_failed > 0 ? 1 : 0; \
    }

#endif /* NPS_TEST_RUNNER_H */
