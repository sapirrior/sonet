#include "test_runner.h"
#include "engine/utils/nps_utils.h"
#include <stdlib.h>

void test_url_parsing(void) {
    char *scheme, *host, *path;
    int port;

    // Standard HTTP URL
    ASSERT_EQ(nps_utils_parse_url("http://example.com/path", &scheme, &host, &port, &path), 0);
    ASSERT_STR_EQ(scheme, "http");
    ASSERT_STR_EQ(host, "example.com");
    ASSERT_EQ(port, 80);
    ASSERT_STR_EQ(path, "/path");
    free(scheme); free(host); free(path);

    // Standard HTTPS URL
    ASSERT_EQ(nps_utils_parse_url("https://example.com:443/", &scheme, &host, &port, &path), 0);
    ASSERT_STR_EQ(scheme, "https");
    ASSERT_STR_EQ(host, "example.com");
    ASSERT_EQ(port, 443);
    ASSERT_STR_EQ(path, "/");
    free(scheme); free(host); free(path);

    // Missing path
    ASSERT_EQ(nps_utils_parse_url("http://example.com", &scheme, &host, &port, &path), 0);
    ASSERT_STR_EQ(scheme, "http");
    ASSERT_STR_EQ(host, "example.com");
    ASSERT_EQ(port, 80);
    ASSERT_STR_EQ(path, "/");
    free(scheme); free(host); free(path);
}

void test_cases(void) {
    RUN_TEST(test_url_parsing);
}

TEST_SUITE_MAIN(test_cases)
