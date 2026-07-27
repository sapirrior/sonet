#!/bin/bash
set -e

NPS=${NPS:-../nps}
# Use postman-echo.com as httpbin.org is currently flaky
HTTPBIN="https://postman-echo.com"

# ANSI colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    exit 1
}

pass() {
    echo -e "${GREEN}PASS: $1${NC}"
}

echo "Running enhanced integration tests..."

# 1. Test GET
echo -n "Test 1: GET... "
status=$(${NPS} "${HTTPBIN}/get" -w '%{http_code}' -o /dev/null -s)
[ "$status" = "200" ] || fail "GET expected 200, got $status"
pass "GET"

# 2. Test POST JSON
echo -n "Test 2: POST JSON... "
status=$(${NPS} "${HTTPBIN}/post" -j -d '{"test":"nps"}' -w '%{http_code}' -o /dev/null -s)
[ "$status" = "200" ] || fail "POST JSON expected 200, got $status"
pass "POST JSON"

# 3. Test PUT / DELETE / PATCH
echo -n "Test 3: PUT/DELETE/PATCH... "
for method in PUT DELETE PATCH; do
    m_lower=$(echo $method | tr '[:upper:]' '[:lower:]')
    status=$(${NPS} "${HTTPBIN}/${m_lower}" -X $method -w '%{http_code}' -o /dev/null -s)
    [ "$status" = "200" ] || fail "$method expected 200, got $status"
done
pass "PUT/DELETE/PATCH"

# 4. Test Custom Headers
echo -n "Test 4: Custom Headers... "
output=$(${NPS} "${HTTPBIN}/headers" -H "X-Nps-Test: enhanced-test" -s)
echo "$output" | grep -q "enhanced-test" || fail "Custom Header not found in response"
pass "Custom Headers"

# 5. Test Redirects
echo -n "Test 5: Redirects... "
# postman-echo.com/redirect-to?url=https://postman-echo.com/get
status=$(${NPS} "${HTTPBIN}/redirect-to?url=https%3A%2F%2Fpostman-echo.com%2Fget" -L -w '%{http_code}' -o /dev/null -s)
[ "$status" = "200" ] || fail "Redirect (Follow) expected 200, got $status"

# Test max-redirects
set +e
# This will redirect once. If we set max-redirs 0, it should fail if it tries to follow.
# Wait, max-redirs 0 means no redirects allowed.
${NPS} "${HTTPBIN}/redirect-to?url=https%3A%2F%2Fpostman-echo.com%2Fget" -L --max-redirs 0 -s > /dev/null 2>&1
ret=$?
set -e
[ $ret -ne 0 ] || fail "Max-redirs 0 should have failed"
pass "Redirects & Max-redirs"

# 6. Test Authentication
echo -n "Test 6: Authentication... "
# Basic Auth (postman-echo uses 'postman:password')
status=$(${NPS} "${HTTPBIN}/basic-auth" -u postman:password -w '%{http_code}' -o /dev/null -s)
[ "$status" = "200" ] || fail "Basic Auth expected 200, got $status"
pass "Authentication (Basic)"

# 7. Test Cookies
echo -n "Test 7: Cookies... "
cookie_file="nps_cookies.txt"
rm -f "$cookie_file"
# Set a cookie
${NPS} "${HTTPBIN}/cookies/set?nps=rock-solid" -c "$cookie_file" -o /dev/null -s
# Send it back (using -b to read from file)
output=$(${NPS} "${HTTPBIN}/cookies" -b "@$cookie_file" -s)
echo "$output" | grep -q "rock-solid" || fail "Cookie not found in jar"
rm -f "$cookie_file"
pass "Cookies & Cookie Jars"

# 8. Test TLS verify
echo -n "Test 8: TLS Verify... "
set +e
output=$(${NPS} https://expired.badssl.com/ -v 2>&1)
set -e
if echo "$output" | grep -iqE "TLS|certificate|failed|error"; then
    pass "TLS Verification"
else
    echo "Output was: $output"
    fail "TLS Verification"
fi

# 9. Test --fail flag
echo -n "Test 9: Fail flag... "
set +e
${NPS} "${HTTPBIN}/status/404" -f -s > /dev/null 2>&1
ret=$?
set -e
[ $ret -ne 0 ] || fail "FAIL_FLAG expected non-zero exit for 404, got $ret"
pass "Fail-on-error flag"

# 10. Test User-Agent
echo -n "Test 10: User-Agent... "
output=$(${NPS} "${HTTPBIN}/headers" -A "nps-test-agent" -s)
echo "$output" | grep -q "nps-test-agent" || fail "User-Agent not sent correctly"
pass "User-Agent Customization"

# 11. Test Expired Cookies Filtering (Hardcore check)
echo -n "Test 11: Expired Cookies Filtering... "
cookie_file="expired_cookies.txt"
rm -f "$cookie_file"
# Create a cookie jar file with one expired cookie and one valid cookie
# Format: domain \t include_subdomains \t path \t secure \t expiry \t name \t value
# We set one expiry to 10 (long time ago, expired) and one to 2147483647 (far future, valid)
printf "postman-echo.com\tTRUE\t/\tFALSE\t10\texpired_cookie\tshould_be_skipped\n" > "$cookie_file"
printf "postman-echo.com\tTRUE\t/\tFALSE\t2147483647\tvalid_cookie\tshould_be_sent\n" >> "$cookie_file"

output=$(${NPS} "${HTTPBIN}/cookies" -b "@$cookie_file" -s)
echo "$output" | grep -q "valid_cookie" || fail "Valid cookie was not sent"
if echo "$output" | grep -q "expired_cookie"; then
    rm -f "$cookie_file"
    fail "Expired cookie was sent but should have been filtered out"
fi
rm -f "$cookie_file"
pass "Expired Cookies Filtering"

# 12. Test Compression / Gzip Decompression
echo -n "Test 12: Compression / Gzip Decompression... "
# Request gzip compressed response and ensure it gets decompressed
status=$(${NPS} "${HTTPBIN}/gzip" --gzip -w '%{http_code}' -o /dev/null -s)
[ "$status" = "200" ] || fail "Gzip request expected 200, got $status"
output=$(${NPS} "${HTTPBIN}/gzip" --gzip -s)
echo "$output" | grep -q '"gzipped": true' || fail "Gzip response decompression failed or was not parsed"
pass "Compression / Gzip Decompression"

# 13. Test Rate Limiting
echo -n "Test 13: Rate Limiting... "
# Download something with a very low rate limit and verify it takes a significant fraction of time
start_time=$(date +%s)
# Read a small size but limit rate to 500 bytes per sec (should take at least 1-2 seconds)
${NPS} "${HTTPBIN}/bytes/1000" --limit-rate 500 -o /dev/null -s
end_time=$(date +%s)
elapsed=$((end_time - start_time))
if [ $elapsed -lt 1 ]; then
    fail "Rate limit did not throttle execution (elapsed $elapsed sec)"
fi
pass "Rate Limiting"

# 14. Test Connect Timeout (Force timeout target)
echo -n "Test 14: Connect Timeout... "
# Use a non-routable IP address like 10.255.255.1 to force a timeout, with a 1 second connect timeout limit
set +e
${NPS} "http://10.255.255.1:81" --connect-timeout 1 -s > /dev/null 2>&1
ret=$?
set -e
# Exit code should be NPS_ERR_TIMEOUT (28) or NPS_ERR_CONNECT (8) or NPS_ERR_NETWORK (2)
if [ $ret -ne 28 ] && [ $ret -ne 8 ] && [ $ret -ne 2 ]; then
    fail "Connect timeout did not exit with expected timeout/network code, got $ret"
fi
pass "Connect Timeout"

echo -e "\n${GREEN}All enhanced integration tests passed successfully!${NC}"


