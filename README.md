# Nurl

Nurl is a clean, fast, portable, and structured HTTP client CLI written in C. 

Unlike traditional command-line HTTP clients that clutter the terminal with complex layouts, `nurl` is built with a simple design philosophy: **plain text, structured details, and smart diagnostics by default.**

---

## 1. Project Architecture

The codebase is organized into nested modules dividing user-interface logic from the request execution engine:

```text
src/
├── main.c                  # Program entry point (WSA startup/cleanup)
├── cli/                    # CLI Interface Layer
├── cli/                    # CLI Interface Layer
│   ├── parser/             # Optimized argument parsing (nurl_cli.c)
│   └── runner/             # Dispatcher, request execution, & progress reporting
├── engine/                 # Protocol & Network Engine Layer
│   ├── nurl_engine.c       # Central engine request orchestrator (Stage-based)
│   ├── nurl_engine_request.c # Request builder & HTTP payload initialization
│   ├── nurl_multipart.c    # Multipart/form-data upload management
│   ├── nurl_ctx.c          # Engine context & state (Connection Pool)
│   ├── net/                # Buffered I/O (NutStream) & Connection Pooling
│   ├── tls/                # OpenSSL contexts & verification setup
│   ├── http/               # HTTP parser, gzip/deflate, redirects
│   └── utils/              # Cookies, base64, & NutBuf builder
└── errors/                 # Smart Error DX Layer
    ├── nurl_diag.c         # Concise Unix-style diagnostics
    └── nurl_error_handler.c # Context-aware diagnostic logic
```

---

## 2. Smart Error DX (Developer Experience)

`nurl` features a context-aware diagnostic system designed to help you solve issues quickly. Instead of cryptic error codes or bulky blocks, you get concise, standard Unix-style messages with helpful hints.

Furthermore, syntax errors or invalid flags output specific error diagnostics rather than dumping the entire help dialogue:

```text
nurl: error: option unrecognized or invalid.
      hint: run 'nurl --help' for usage.
```

If a network or file operation fails, `nurl` provides a friendly diagnostic:

```text
nurl: error: network connection reset or interrupted during the request to 'https://api.example.com'
      hint: check your internet connection or verify if the server is reachable
      hint: since you are downloading a file to disk, you can attempt to pick up where you left off by adding the --resume flag
```

---

## 3. Build Instructions

### Prerequisites
Make sure you have GCC/Clang, `libssl-dev` (OpenSSL), and `zlib1g-dev` installed.

### Compiling
To clean and build the optimized binary:
```bash
make clean
make
```

### Build Size & Portability Features
The [Makefile](Makefile) is tuned for production-grade builds:
*   **Static Linking**: Links `libssl` and `libcrypto` statically producing an executable with zero external OpenSSL dependencies.
*   **Size Optimization**: Compiled with `-Os` (size optimization) and aggressive dead-code elimination.
*   **Cross-Platform**: Full support for Linux, macOS, and Windows (MinGW).

### Testing & Debugging
`nurl` ships with a full suite of unit and integration tests to ensure reliability:
```bash
make test       # Runs the C unit test runner and bash integration suite
make asan       # Builds the project with AddressSanitizer and UndefinedBehaviorSanitizer
make debug      # Compiles a non-optimized debug build with symbols (`-g3 -O0`)
```

---

## 4. Command Usage Guide

`nurl` uses a modern, flag-triggered CLI model. There are no subcommands; behavior is determined by flags.

### 4.1. Standard REST Operations

#### GET Request
```bash
nurl https://httpbin.org/get -i
```
*(The `-i` / `--include` option outputs the HTTP response headers above the body).*

#### POST JSON Payload
```bash
nurl https://httpbin.org/post -d '{"tool": "nurl"}' -j
```
*(The `-j` flag is shorthand to attach `Content-Type: application/json` headers).*

#### Custom Methods
```bash
nurl https://httpbin.org/put -X PUT -d 'payload'
nurl https://httpbin.org/delete -X DELETE
```

---

### 4.2. Specialized Execution Modes

#### Streaming Downloads (`-D`)
Download files directly to disk. Use `--resume` to pick up a partial transfer:
```bash
nurl https://example.com/large-file.zip -D -o output.zip --resume --progress
```

#### Latency Analysis (`--ping`)
Measure host response times over TCP/TLS:
```bash
nurl https://api.github.com --ping --count 3
```

#### Dry-run Request Inspection (`--dry-run`)
Inspect generated headers and body outline **without sending any network traffic**:
```bash
nurl https://api.example.com/data --dry-run -X POST -j
```
*(Sensitive headers like `Authorization` are automatically redacted as `[hidden]`).*

#### DNS Host Resolution (`--resolve`)
```bash
nurl httpbin.org --resolve
```

---

### 4.3. First-Class Stdin & Stdout Piping

#### Piping Response to Shell
```bash
nurl https://claude.ai/install.sh -L -s | bash
```

#### Sending Stdin Payload
Automatically read payloads from `stdin` during write requests or when `-d -` is passed:
```bash
echo "hello world" | nurl https://httpbin.org/post -j
cat image.png | nurl https://api.example.com/upload --upload -
```

---

## 5. Advanced Options Reference

| Flag / Option | Description |
| :--- | :--- |
| `--gzip` | Request compression and automatically decompress payloads. |
| `-e, --referer <URL>` | Set custom `Referer` headers. |
| `-f, --fail` | Return non-zero on 4xx/5xx errors and suppress body. |
| `--retry <num>` | Number of retries on transient failures. |
| `-w, --format <str>` | Custom output format (e.g. `%{http_code} %{time_total}s`). |
| `-k, --insecure` | Skip TLS certificate validation. |
| `-L, --follow` | Follow HTTP 3xx redirections. |
| `--max-redirects <num>`, `--max-redirs <num>` | Limit the number of redirects to follow (default 10). |
| `-x, --proxy <url>` | Tunnel traffic through an HTTP proxy. |
| `--connect-to <host:port:target:target_port>` | Override the connection target without changing the Host header. |
| `-b, --cookie <val>` | Send cookies from string or file (`@file`). |
| `--session <file>` | Read and write cookies for a persistent session. |
| `--limit-rate <bytes/s>` | Throttle upload/download bandwidth. |
| `-I, --head` | Fetch document info (headers) only via HEAD request. |
| `--dump-header <file>` | Write response headers to a specified file. |
| `--upload <file>` | Upload a file using multipart/form-data. |

---

## 6. Protocol Support

`nurl` is a highly-optimized **HTTP/1.1** client:

*   **Buffered I/O (8KB)**: Unified `NutStream` abstraction reduces syscall overhead.
*   **Connection Pooling**: Features a proactive pool with 60s idle-eviction.
*   **TLS 1.2/1.3**: Fully supported via OpenSSL with automatic ALPN.
*   **Resumable Transfers**: Native support for byte-range resumes.
*   **Hardened Engine**: Robust handling of malformed responses, decompression failures, and large headers.


---

## 7. Cross-Platform Compatibility

`nurl` runs identically on **Linux, macOS, and Windows**:
*   **Windows (Winsock)**: Native support for Winsock2 and `WSAPoll`.
*   **Portable Timers**: High-resolution timing using OS-native primitives.
*   **Static Build**: Portable binary with zero runtime library dependencies.
