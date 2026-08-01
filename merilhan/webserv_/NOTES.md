# webserv — Notes

This document is the deep technical reference for the project: what each module does, exactly which HTTP behaviour is implemented and why, the full CGI environment, every supported status code, the configuration file format, and the security hardening that was added. The [README.md](README.md) is the short entry point; this file is where the reasoning lives.

## Table of contents

1. [Design philosophy](#design-philosophy)
2. [Directory layout](#directory-layout)
3. [Module reference](#module-reference)
4. [The event loop: non-blocking I/O with a single `poll()`](#the-event-loop)
5. [HTTP/1.1 feature support](#http11-feature-support)
6. [Supported HTTP status codes](#supported-http-status-codes)
7. [Configuration file reference](#configuration-file-reference)
8. [CGI/1.1](#cgi11)
9. [Security hardening](#security-hardening)
10. [Notable design decisions](#notable-design-decisions)
11. [Testing](#testing)

---

## Design philosophy

The project went through a deliberate refactor from a single, large `Server` class into small, single-responsibility modules, following two rules:

- **Utils classes are stateless.** `StringUtils`, `NetUtils`, `FsUtils`, `HttpUtils` contain only `static` methods, are never instantiated, and hold no state between calls. They exist purely to remove duplication between modules (e.g. `NetUtils::ipToString` used to be copy-pasted in three places).
- **Everything else is a real class**, with Orthodox Canonical Form (constructor, destructor, copy constructor, copy-assignment operator) even where the object is never actually copied — kept `private` in that case, per the subject's C++98 constraints.

No `namespace` is used anywhere (project constraint); file-scope `static` is used instead where C++ would normally reach for an anonymous namespace.

## Directory layout

```
includes/            sources/
├── Utils/            ├── Utils/          StringUtils, NetUtils, FsUtils, HttpUtils
├── Http/              ├── Http/           HttpRequest, HttpResponseBuilder
├── Config/            ├── Config/         ConfigParser, ConfigStructs, ListenTable
├── Cgi/               ├── Cgi/            CgiProcess
└── Core/              ├── Core/           Client, Router, RequestHandler, Server, ServerCgi
                        └── main.cpp
```

The `Makefile` adds one `-I` per subdirectory, so every `#include "X.hpp"` stays a bare filename — the physical layout is organized without any include path ever needing to spell out the subdirectory. Object files build into `objs/`, mirroring this same layout, keeping `sources/`/`includes/` free of build artifacts.

## Module reference

### Utils (stateless)

| Class | Responsibility |
|---|---|
| `StringUtils` | `toLower`/`toUpper`, `trimLeadingSpaces` (header-value trimming), `isNumeric` |
| `NetUtils` | `ipToString` (manual `ntohl` + formatting — `inet_ntoa` is not on the subject's whitelist), `resolveIPv4` (`getaddrinfo` wrapper), `setNonBlocking` (the `F_GETFL`→OR→`F_SETFL` pattern, shared by listen sockets, accepted client sockets, and CGI pipes) |
| `FsUtils` | `readWholeFile` (via `open`/`read`/`close`, not `ifstream` — kept off the subject's function whitelist deliberately), `hasDotDotSegment` (path traversal check), `exists`/`isDirectory`/`isRegularFile`/`isReadable` (`stat`/`access` wrappers) |
| `HttpUtils` | `statusText`, `contentTypeFor` (extension→MIME map), `determineKeepAlive`, `splitHeaderBody` (CGI output parsing), `urlDecode` (percent-decoding with null-byte/malformed-sequence rejection), `htmlEscape` (XSS defense for generated HTML) |

### Http

- **`HttpRequest`** — owns request-line + header parsing. `parseRequestLine` URL-decodes the path (rejecting malformed encoding or a decoded null byte) but deliberately leaves the query string encoded, since CGI/1.1 expects `QUERY_STRING` raw.
- **`HttpResponseBuilder`** — builds every outgoing response: `build()` (normal response), `buildRedirect()` (301/302/303/307/308, accepts both absolute `http(s)://` targets and internal paths, writes whatever the config gave verbatim into `Location`), `buildError()` (looks up a configured `error_page` file and falls back to a built-in, hard-coded HTML page if none is configured or the file can't be read).

### Config

- **`ConfigParser`** — the nginx-inspired config-file grammar. Resolves inheritance at parse time (e.g. a location's `client_max_body_size` is filled in from its server block if not overridden, so the runtime always sees one final effective value, never `unset`).
- **`ConfigStructs`** — `ServerConfig` / `LocationConfig` / `ListenConfig`, plain data classes.
- **`ListenTable`** — topology-level validation across *all* parsed servers: rejects true `(ip, port, server_name)` ambiguity, and builds the nginx-style wildcard/specific-IP socket merge (`RealListen`): if a port has both `0.0.0.0` and a specific IP, only **one** real socket is opened (the wildcard); the specific entries become pure routing metadata resolved at `accept()` time via `getsockname()`.

### Cgi

- **`CgiProcess`** — the entire fork/pipe/execve lifecycle for one CGI invocation, as a real class instead of a bag of fields: `start()` (fork + pipe + dup2 + chdir + execve), `writeStdin()` / `readStdout()` (one non-blocking increment each, called from the poll loop), `terminate()` (SIGKILL + close remaining pipes, used by both the 10s timeout and early client-disconnect abort paths).

### Core

- **`Client`** — all per-connection state in one place (replacing eight parallel `std::map<int, X>`s from an earlier iteration): buffers, the parsed `HttpRequest`, keep-alive/close flags, the Slowloris timer, and the incremental chunked-decode state (`chunkParseOffset`, `chunkedBody`).
- **`Router`** — wraps `ListenTable` to answer "which `ServerConfig` for this `(port, local IP, Host header)`" and "which `LocationConfig` for this path" (longest-prefix match, nginx style).
- **`RequestHandler`** — the GET/POST/DELETE filesystem logic (static file serving, index fallback, directory listing, upload write, delete), stateless static methods operating on a `Client&`/`LocationConfig*`.
- **`Server`** — pure orchestration: the single `poll()` loop, `std::map<int, Client>` bookkeeping, dispatch to `Router`/`RequestHandler`/`CgiProcess`. Contains no business logic of its own.

## The event loop

A single `std::vector<struct pollfd>` holds every listening socket, every accepted client socket, and every CGI pipe fd (stdin *and* stdout, tracked separately). One `poll()` call per loop iteration drives all of it — this is a hard subject requirement, checked explicitly during evaluation.

Rules followed throughout:
- No `read`/`write`/`recv`/`send` is ever attempted on a socket or pipe without that fd having just been reported ready by `poll()`.
- `errno` is **never** inspected after a `read`/`write` call to change behaviour — only the return value (`<= 0`, or a partial count) is used. The only two other uses of `errno` in the whole codebase are `EINTR` after `poll()` itself returns `-1` (not a read/write call), and `strerror(errno)` in socket-setup error *messages* (diagnostic text, not a behaviour branch) — neither is the pattern the subject forbids.
- Regular disk files (`open`/`read`/`stat` for static content, uploads, error pages) are read synchronously without going through `poll()`, which the subject explicitly exempts.
- `fork()` is used **only** for CGI (one call site, in `CgiProcess::start`).

A known non-obvious bug fixed during development: `poll()` can report only `POLLHUP` (without `POLLIN`) once a pipe's writer closes and the buffered data has been fully drained — checking for `POLLIN` alone caused a CPU-spinning loop; the dispatch now checks `POLLIN | POLLHUP | POLLERR` together.

## HTTP/1.1 feature support

- **Keep-alive**: HTTP/1.1 defaults to keep-alive unless `Connection: close` is sent; HTTP/1.0 defaults to close unless `Connection: keep-alive` is sent.
- **Pipelining**: because `poll()` won't re-signal bytes that are already sitting in a socket's read buffer, the server re-invokes request processing immediately after a response finishes writing, in case a full next request is already buffered.
- **Chunked transfer-encoding** (request body): decoded **incrementally** — `Client::chunkParseOffset`/`chunkedBody` remember how much of the raw chunked stream has already been validated, so a large chunked body costs O(n) total instead of being re-parsed from byte 0 on every incoming TCP segment (see [Notable design decisions](#notable-design-decisions) for the story behind this fix).
- **`Expect: 100-continue`**: recognized when a body is expected; the server queues an interim `HTTP/1.1 100 Continue` response immediately, distinct from the final response, and is careful not to let the interim write close the connection.
- **Request smuggling defense**: if both `Content-Length` and `Transfer-Encoding: chunked` are present, chunked framing always wins and `Content-Length` is ignored entirely (RFC 7230 §3.3.3).
- **Host header**: mandatory for HTTP/1.1 requests (400 if missing); not required for HTTP/1.0.
- **Unsupported HTTP version**: any version other than `HTTP/1.0`/`HTTP/1.1` gets `505`.

## Supported HTTP status codes

| Code | Meaning | When it's returned |
|---|---|---|
| 200 | OK | Successful GET/CGI/overwrite-upload |
| 201 | Created | Successful upload of a *new* file |
| 301/302/303/307/308 | Redirects | `return`/`redirect` directive; directory access without a trailing slash also issues a 301 |
| 400 | Bad Request | Malformed request line/headers, missing Host on HTTP/1.1, invalid Content-Length, invalid chunked framing, null-byte/malformed percent-encoding |
| 403 | Forbidden | Static file exists but isn't readable (permission denied); no `upload_dir` configured for POST/DELETE |
| 404 | Not Found | No matching file; directory with no index file and `autoindex off` (see design decisions) |
| 405 | Method Not Allowed | Method not in the location's `allow_methods` |
| 411 | Length Required | *(defined in the status table; not automatically triggered — see design decisions)* |
| 413 | Payload Too Large | Body exceeds the *effective* (location-aware) `client_max_body_size` |
| 414 | URI Too Long | Request line exceeds an internal size cap |
| 431 | Request Header Fields Too Large | Header block exceeds an internal size cap |
| 500 | Internal Server Error | CGI pipe/fork failure, upload write failure |
| 501 | Not Implemented | *(reserved in the status table)* |
| 502 | Bad Gateway | CGI produced output that couldn't be split into header+body |
| 504 | Gateway Timeout | CGI exceeded its execution timeout |
| 505 | HTTP Version Not Supported | Any version other than HTTP/1.0 or HTTP/1.1 |

## Configuration file reference

Inspired by NGINX's `server { location { } }` structure.

**Server-level directives**: `listen` (repeatable), `server_name`, `error_page <code...> <path>` (repeatable), `root`, `index`, `autoindex`, `client_max_body_size` (default `1M`).

**Location-level directives** (inherit the server's value unless overridden): `allow_methods`, `root`, `index`, `autoindex`, `client_max_body_size`, `upload_dir` (enables POST/DELETE storage), `cgi_extension <ext> <interpreter-path>` (repeatable, extension-based CGI dispatch), `return <code> <target>` (target may be an internal path or an absolute `http://`/`https://` URL).

Topology rules enforced by `ListenTable` at startup (reject at parse time, never at runtime): duplicate `(ip, port)` within one server, and two different servers sharing both the same `(ip, port)` *and* the same `server_name` (ambiguous — Host header couldn't disambiguate them even if virtual hosting is in play). See `configs/default.conf` for a fully worked, three-scenario example (single-server wildcard+specific listen, two-server wildcard/specific-IP merge, single specific-IP server with custom error pages), and `configs/conflict.conf` / `configs/vhost_conflict.conf` / `configs/vhost_scenarios.conf` for the intentionally-invalid/edge-case configs kept separate from the demo config.

## CGI/1.1

Invocation: `fork()` → two `pipe()`s (stdin, stdout) → child does `dup2` + `chdir(scriptDir)` + `execve(interpreter, [interpreter, scriptBasename], envp)`; parent sets both pipe ends non-blocking and registers them with the single `poll()`. A per-CGI 10-second timeout (`SIGKILL` + non-blocking `waitpid(WNOHANG)` reap loop, retried every main-loop iteration so it's never a blocking wait) guarantees a request can never hang forever on a stuck script.

### Environment variables provided

`REQUEST_METHOD`, `SCRIPT_NAME` *(intentionally empty — see below)*, `SCRIPT_FILENAME`, `PATH_INFO`, `PATH_TRANSLATED`, `REQUEST_URI`, `QUERY_STRING` (left percent-encoded, per spec), `CONTENT_LENGTH`, `CONTENT_TYPE`, `SERVER_PROTOCOL`, `SERVER_NAME`, `SERVER_PORT`, `REMOTE_ADDR`, `GATEWAY_INTERFACE`, `SERVER_SOFTWARE`, `REDIRECT_STATUS` (php-cgi refuses to run without it), `PATH`, plus every request header re-exposed as `HTTP_<NAME>` (dashes → underscores, uppercased).

**Why `SCRIPT_NAME` is empty and `PATH_INFO` carries the full path**: RFC 3875 §4.1.13 explicitly permits `SCRIPT_NAME` to be empty when the script isn't identified separately from the rest of the path — this project's CGI matching is purely extension-based over the whole resolved path, so there's no separate "script name vs. extra path" boundary to report. This exact convention was confirmed against the subject's own official `tests/cgi_tester` binary by direct probing (feeding it hand-built environments via `env -i` and observing which `SCRIPT_NAME`/`PATH_INFO` pairing it accepts).

Chunked request bodies are fully un-chunked by the server before the CGI ever sees them (the CGI always gets a plain byte stream on stdin, ended by EOF when the parent closes the write end — exactly what the subject asks for). CGI output is split into header block + body on the first blank line; if the CGI's own output has no `Content-Length`, the body is whatever arrives before stdout EOF.

## Security hardening

| Concern | Defense |
|---|---|
| Path traversal (`../`) | `FsUtils::hasDotDotSegment`, checked on the **decoded** path |
| Null-byte injection (`%00` or raw) | `HttpUtils::urlDecode` rejects a decoded null byte; the raw path is also checked directly |
| Reflected/stored XSS | `HttpUtils::htmlEscape` applied to the autoindex directory listing (both the displayed path and every filename — filenames come from `readdir()`, which can contain attacker-uploaded content) |
| Request smuggling | Transfer-Encoding always overrides Content-Length when both are present (RFC 7230 §3.3.3) |
| Malformed Content-Length | Validated as all-digits before parsing; a garbage value is `400`, not silently coerced to `0` |
| Unbounded header/URI size (memory DoS) | `414`/`431` size caps, checked before any parsing work is done on the oversized data |
| Unbounded chunked body (memory DoS) | The effective `client_max_body_size` is checked incrementally as chunks arrive, not only once the terminating chunk shows up |
| Slowloris (byte-at-a-time header trickle) | A **separate** absolute timer (`Client::requestStartTime`, refreshed only at the start of a new request, never per-byte) bounds total time-to-complete-headers, independent of the idle-activity timeout |
| CGI hang / zombie processes | 10s CGI timeout + `SIGKILL` + non-blocking reap; `SIGPIPE` is globally ignored so a CGI closing its end early can never crash the server via a broken-pipe signal |
| Crash safety | Every syscall failure path is handled and converted into an HTTP error response or a clean disconnect — the server was stress-tested with 35 malformed/malicious request scenarios (`tests/crash_test.py`) without a single crash |

## Notable design decisions

- **Chunked decode was rewritten to be incremental after a real O(n²) bug was found.** The original implementation re-parsed the *entire* accumulated chunked buffer from byte 0 on every incoming `read()` event. This was invisible at small sizes but catastrophic at scale: a 20 MB chunked upload took **20.2 seconds**; the same 20 MB sent with a known `Content-Length` took under 1.5 seconds. This was discovered while running the subject's official CGI tester with a 100 MB payload, which hung for several minutes. After the fix (per-`Client` `chunkParseOffset`/`chunkedBody` state, resumed rather than restarted on each call), the same 100 MB chunked upload completes in ~1.3 seconds — linear scaling confirmed by direct measurement.
- **`client_max_body_size` is resolved at the *location* level, not just the server level.** A related bug was caught alongside the fix above: the body-size check was reading `ServerConfig::clientMaxBodySize` even when the matched *location* had its own override, silently ignoring the location-specific limit. `Server::effectiveMaxBodySize()` now resolves the matching location first.
- **Directory access with no index file and `autoindex off` returns `404`, not `403`.** Nginx's real behaviour here is `403`; this project deliberately returns `404` instead, on the reasoning that `403` confirms to an attacker that the directory exists at all, while `404` reveals nothing — a small, intentional divergence from the nginx reference, chosen for information-disclosure reasons.
- **A per-IP rate limiter (token bucket, `429 Too Many Requests`) was implemented and then removed.** It worked correctly (verified: normal traffic stayed `200`, a 200-request burst against a 100-token bucket correctly returned `429` for the overflow), but the subject does not ask for rate limiting, and it was removed to keep the project scoped to what's actually required. It is not true DDoS protection either way — a genuinely distributed attack (many source IPs) is a network/infrastructure-layer problem, not something a single-process HTTP server can solve.
- **`GET`/`DELETE` requests carrying an unexpected body are accepted, not rejected.** RFC 7231 §4.3.1 says a `GET` body has no defined semantics but doesn't forbid one; this matches real-world server behaviour (NGINX accepts it too) more closely than inventing a new rejection rule the subject never asked for.

## Testing

- **`tests/crash_test.py`** — 35 scenarios (garbage binary data, null bytes, deep path traversal, malformed/oversized chunked encoding, oversized headers/URIs, abrupt mid-request disconnects, a 150-connection burst, slow byte-at-a-time sends, ...), asserting only one thing per scenario: the server process is still alive afterwards.
- **`tests/feature_test.py`** — 21 scenarios exercising every mandatory subject feature end-to-end (static files, redirects, upload/overwrite/delete, method restriction, body-size limit, chunked upload, CGI GET/POST, custom/default error pages, autoindex, multi-port listening, wildcard/specific-IP virtual-host merge, keep-alive + true pipelining).
- **`tests/curl_tests.sh`** — the day-to-day `curl` (plus a handful of raw-socket `nc` calls for protocol-level cases `curl` can't produce) regression sweep accumulated during development.
- **`tests/tester` / `tests/cgi_tester`** — the official Go binaries distributed with the subject on the intranet, run against `configs/tester_intra.conf` with the `tests/YoupiBanane/` fixture tree they require.

All four suites are run after any change that touches request parsing, routing, CGI, or the poll loop.
