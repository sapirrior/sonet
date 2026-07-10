#include "test_runner.h"
#include "engine/utils/nurl_headers.h"
#include <stdlib.h>
#include <string.h>

void test_header_serialization(void) {
    NutHeaderMap *m = nurl_headermap_new();
    ASSERT_NOT_NULL(m);
    
    nurl_headermap_set(m, "Content-Type", "application/json");
    nurl_headermap_set(m, "accept", "text/html"); // case-insensitive handling
    
    char *serialized = nurl_headermap_serialize(m);
    ASSERT_NOT_NULL(serialized);
    
    // Check if both headers are present (order might vary, but typical implementation appends)
    ASSERT_TRUE(strstr(serialized, "Content-Type: application/json\r\n") != NULL);
    ASSERT_TRUE(strstr(serialized, "Accept: text/html\r\n") != NULL); // Capitalized correctly
    
    free(serialized);
    nurl_headermap_free(m);
}

void test_cases(void) {
    RUN_TEST(test_header_serialization);
}

TEST_SUITE_MAIN(test_cases)
