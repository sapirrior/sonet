CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Os -I../src -I../src/cli -I../src/cli/parser -I../src/cli/runner -I../src/engine -I../src/engine/net -I../src/engine/tls -I../src/engine/http -I../src/engine/utils -I../src/compat -I../src/errors

TESTS = test_url_parser test_headers

all: $(TESTS)

test_url_parser: test_url_parser.c ../src/engine/utils/nurl_utils.c ../src/engine/utils/nurl_buf.c
	$(CC) $(CFLAGS) $^ -o $@

test_headers: test_headers.c ../src/engine/utils/nurl_headers.c ../src/engine/utils/nurl_buf.c
	$(CC) $(CFLAGS) $^ -o $@

test: all
	./test_url_parser
	./test_headers
	bash ./integration.sh

clean:
	rm -f $(TESTS)
