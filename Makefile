CC      = gcc
VERSION = $(shell cat VERSION)
CFLAGS  = -std=c11 -Wall -Wextra -Os -ffunction-sections -fdata-sections \
          -fno-ident -D_GNU_SOURCE -DNURL_VERSION=\"$(VERSION)\" \
          -Isrc -Isrc/cli -Isrc/cli/parser -Isrc/cli/runner \
          -Isrc/engine -Isrc/engine/net -Isrc/engine/tls -Isrc/engine/http -Isrc/engine/utils -Isrc/compat -Isrc/errors
LDFLAGS = -Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic -lpthread -ldl -lz \
          -Wl,--gc-sections -s

SRCS    := $(shell find src -name '*.c')
OBJS    := $(SRCS:src/%.c=build/%.o)
TARGET  ?= nurl

ifeq ($(WINDOWS),1)
  CC       = x86_64-w64-mingw32-gcc
  LDFLAGS += -lws2_32 -lshlwapi
  TARGET   = nurl.exe
endif

PREFIX ?= /usr/local

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -rf build $(TARGET)
	$(MAKE) -C tests clean

test: $(TARGET)
	$(MAKE) -C tests test

debug: CFLAGS = -std=c11 -Wall -Wextra -g3 -O0 -D_GNU_SOURCE -DNURL_VERSION=\"$(VERSION)-debug\" -Isrc -Isrc/cli -Isrc/cli/parser -Isrc/cli/runner -Isrc/engine -Isrc/engine/net -Isrc/engine/tls -Isrc/engine/http -Isrc/engine/utils -Isrc/compat -Isrc/errors
debug: LDFLAGS = -lssl -lcrypto -lpthread -ldl -lz
debug: $(TARGET)

asan: CC = clang
asan: CFLAGS = -std=c11 -Wall -Wextra -g -O1 -fno-omit-frame-pointer -D_GNU_SOURCE -DNURL_VERSION=\"$(VERSION)-asan\" -Isrc -Isrc/cli -Isrc/cli/parser -Isrc/cli/runner -Isrc/engine -Isrc/engine/net -Isrc/engine/tls -Isrc/engine/http -Isrc/engine/utils -Isrc/compat -Isrc/errors -fsanitize=address,undefined
asan: LDFLAGS = -fsanitize=address,undefined -lssl -lcrypto -lpthread -ldl -lz
asan: clean $(TARGET)

memcheck: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TARGET) https://jsonplaceholder.typicode.com/posts/1 -s > /dev/null

.PHONY: all clean debug asan memcheck install uninstall test
