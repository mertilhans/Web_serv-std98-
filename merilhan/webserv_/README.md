*This project has been created as part of the 42 curriculum by tuzan, husarpka, merilhan.*

# webserv

## Description

**webserv** is a fully custom HTTP/1.1 server written from scratch in C++98, built for the 42 school "webserv" project. It implements its own non-blocking socket event loop (single `poll()` for every listening socket, client connection and CGI pipe), its own HTTP request/response parsing, its own nginx-inspired configuration file format, and its own CGI/1.1 gateway (fork + pipe + execve) — no external HTTP, networking, or templating libraries are used anywhere in the codebase.

The goal of the project is to understand HTTP well enough to reimplement the parts of it that make a real browser, `curl`, and CGI scripts (PHP-CGI, Python, ...) work correctly against a server you wrote yourself: keep-alive and pipelining, chunked transfer-encoding, virtual hosts, static file serving, file uploads/deletes, directory listing, custom error pages, and CGI execution — all driven by a single, non-blocking I/O loop.

For the full technical breakdown of every module, every supported HTTP status code, the CGI environment, and the security hardening that was added, see **[NOTES.md](NOTES.md)**.

## Instructions

### Build

```sh
make            # builds ./webserv
make re          # clean rebuild
make clean        # removes object files (objs/)
make fclean       # removes object files AND the binary
```

Compiled with `c++ -Wall -Wextra -Werror -std=c++98`. Object files are placed under `objs/`, mirroring the `sources/` module layout (`objs/Core/`, `objs/Http/`, `objs/Utils/`, `objs/Config/`, `objs/Cgi/`); `sources/` and `includes/` stay free of build artifacts.

### Run

```sh
./webserv [configuration file]
```

If no configuration file is given, `configs/default.conf` is used. See that file's header comment for a full, ready-to-try usage guide (every mandatory feature mapped to a concrete `curl` command).

### Test

Three independent test suites are included under `tests/`:

```sh
python3 tests/crash_test.py     # 35 malformed/malicious-request scenarios; asserts the server never dies
python3 tests/feature_test.py   # exercises every mandatory subject feature end-to-end against configs/default.conf
./tests/curl_tests.sh           # the full curl (+ a few raw-socket) regression sweep used throughout development
```

`tests/tester` and `tests/cgi_tester` are the official Go binaries provided with the subject on the intranet; `tests/YoupiBanane/` and `configs/tester_intra.conf` are the fixture directory and configuration they require. Run with:

```sh
./webserv configs/tester_intra.conf &
tests/tester http://127.0.0.1:8083
```

## Resources

- [RFC 7230](https://www.rfc-editor.org/rfc/rfc7230) — HTTP/1.1: Message Syntax and Routing
- [RFC 7231](https://www.rfc-editor.org/rfc/rfc7231) — HTTP/1.1: Semantics and Content (status codes, methods)
- [RFC 6585](https://www.rfc-editor.org/rfc/rfc6585) — Additional HTTP Status Codes (429, 431)
- [RFC 3875](https://www.rfc-editor.org/rfc/rfc3875) — The Common Gateway Interface (CGI) Version 1.1
- [RFC 1945](https://www.rfc-editor.org/rfc/rfc1945) — HTTP/1.0 (used as the project's baseline reference)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — sockets, `poll()`, non-blocking I/O
- [NGINX documentation](https://nginx.org/en/docs/) — used throughout as the reference implementation to compare behaviour against (config syntax, status codes, directory-listing/error-page semantics)
- `man` pages for every whitelisted syscall (`poll`, `fcntl`, `execve`, `waitpid`, `getaddrinfo`, ...)

### AI usage

An AI assistant (Claude, via Claude Code) was used throughout this project, always reviewed and validated manually before being kept. Concretely, it was used for:

- **Architecture refactor**: splitting an initially monolithic `Server` class into the current `Utils` / `Http` / `Config` / `Cgi` / `Core` module layout (see NOTES.md), including converting structs to classes with Orthodox Canonical Form where required by the subject's C++98 constraints.
- **Code review / security audit**: identifying and helping fix concrete issues — a reflected/stored XSS in the autoindex output (unescaped filenames), a missing Host-header/HTTP-version validation gap versus RFC 7230, a Content-Length parsing edge case, and path-traversal / null-byte-injection defenses.
- **Performance debugging**: diagnosing a real O(n²) bug in the chunked transfer-encoding decoder (large chunked uploads were being re-parsed from byte 0 on every incoming TCP segment) after it surfaced as a multi-minute hang against the official CGI tester, and rewriting the decoder to be incremental (O(n)).
- **CGI/1.1 compliance**: working out, by directly probing the official `cgi_tester` binary with controlled environment variables, the exact `SCRIPT_NAME`/`PATH_INFO` convention it expects, and confirming via RFC 3875 §4.1.13 that the chosen convention is spec-compliant.
- **Test infrastructure**: writing the three test suites listed above (`crash_test.py`, `feature_test.py`, `curl_tests.sh`).

Every change proposed by the assistant was compiled, run, and checked against real `curl`/browser/CGI behaviour (and, for the sections above, against the official subject tester) before being accepted — nothing was merged unreviewed.

## Features

- HTTP/1.1 with keep-alive, pipelining, and chunked transfer-encoding (request un-chunking, incremental/O(n))
- GET, POST, DELETE, per-route method restriction (405 otherwise)
- Static file serving, directory index files, directory listing (`autoindex`)
- File uploads to a configurable directory, file deletion
- HTTP redirection (internal paths and absolute `http://`/`https://` targets, 301/302/303/307/308)
- CGI/1.1 execution by file extension (fork + pipe + execve), full environment variable set, chunked-body un-chunking before the CGI sees it, CGI timeout with non-blocking zombie reaping
- Multiple `listen` interface:port pairs, nginx-style wildcard/specific-IP socket merge, optional virtual hosting via `server_name` + `Host` header
- Configurable `client_max_body_size`, enforced per-location (not just per-server)
- Custom error pages per status code, with a built-in default page when none is configured
- Security hardening: path traversal, null-byte injection, percent-encoding validation, response-splitting-safe redirects, XSS-escaped autoindex output, Slowloris-style slow-request timeout, request-smuggling-safe Content-Length/Transfer-Encoding handling

See [NOTES.md](NOTES.md) for the full module-by-module breakdown, the complete list of supported HTTP status codes, the CGI environment variable table, and the reasoning behind every non-obvious design decision.
