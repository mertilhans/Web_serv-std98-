# WEBSERV — DEV_DOC
## C++98 | epoll | 3 Kişilik Geliştirme Kılavuzu

---

## İçindekiler

1. [Proje Genel Bakış](#1-proje-genel-bakış)
2. [Dizin Yapısı](#2-dizin-yapısı)
3. [Mimari Diyagram](#3-mimari-diyagram)
4. [Kişi 1 — Core / epoll / Network](#4-kişi-1--core--epoll--network)
5. [Kişi 2 — HTTP Protocol Layer](#5-kişi-2--http-protocol-layer)
6. [Kişi 3 — Config / CGI / Features](#6-kişi-3--config--cgi--features)
7. [Entegrasyon Noktaları](#7-entegrasyon-noktaları)
8. [Subject Kritik Kuralları](#8-subject-kritik-kuralları)
9. [Test ve Evaluation Checklist](#9-test-ve-evaluation-checklist)

---

## 1. Proje Genel Bakış

| Alan | Detay |
|------|-------|
| Dil | C++98 |
| I/O Multiplexing | `epoll` (Linux) |
| HTTP | GET, POST, DELETE |
| CGI | PHP, Python (fork + execve + pipe) |
| Config | nginx-benzeri syntax |
| Derleme | `c++ -Wall -Wextra -Werror -std=c++98` |
| Çalıştırma | `./webserv [config_file]` |

### Altın Kurallar (bunlar ihlal edilirse grade 0)
- `epoll_wait()` ana döngüde HEM read HEM write'ı aynı anda izlemeli
- `read/write` yapmadan önce MUTLAKA epoll üzerinden geçmeli
- `errno` kontrolü `read/write` sonrası YASAK
- `fork()` sadece CGI için kullanılabilir
- Tüm socket/pipe fd'leri `O_NONBLOCK` olmalı

---

## 2. Dizin Yapısı

```
webserv/
├── Makefile
├── webserv.conf              ← default config dosyası
├── include/
│   ├── Server.hpp            (Kişi 1)
│   ├── EpollManager.hpp      (Kişi 1)
│   ├── Client.hpp            (Kişi 1)
│   ├── HttpRequest.hpp       (Kişi 2)
│   ├── HttpResponse.hpp      (Kişi 2)
│   ├── HttpHandler.hpp       (Kişi 2)
│   ├── MimeTypes.hpp         (Kişi 2)
│   ├── ConfigParser.hpp      (Kişi 3)
│   ├── ServerConfig.hpp      (Kişi 3)
│   ├── LocationConfig.hpp    (Kişi 3)
│   └── CgiHandler.hpp        (Kişi 3)
├── src/
│   ├── main.cpp              (Kişi 1)
│   ├── Server.cpp            (Kişi 1)
│   ├── EpollManager.cpp      (Kişi 1)
│   ├── Client.cpp            (Kişi 1)
│   ├── HttpRequest.cpp       (Kişi 2)
│   ├── HttpResponse.cpp      (Kişi 2)
│   ├── HttpHandler.cpp       (Kişi 2)
│   ├── MimeTypes.cpp         (Kişi 2)
│   ├── ConfigParser.cpp      (Kişi 3)
│   ├── ServerConfig.cpp      (Kişi 3)
│   └── CgiHandler.cpp        (Kişi 3)
└── www/
    ├── html/                 ← static dosyalar
    ├── error_pages/          ← 400, 403, 404, 405, 500, 504
    └── cgi-bin/              ← CGI scriptleri
```

---

## 3. Mimari Diyagram

```
                        ┌─────────────────────────────┐
                        │         main.cpp             │
                        │  ConfigParser → Server start │
                        └──────────────┬──────────────┘
                                       │
                        ┌──────────────▼──────────────┐
                        │       Server (Kişi 1)        │
                        │   listen_fd'leri oluştur     │
                        │   EpollManager'a kaydet      │
                        └──────────────┬──────────────┘
                                       │
                        ┌──────────────▼──────────────┐
                        │     EpollManager (Kişi 1)    │
                        │       epoll_wait loop        │
                        └──┬──────────┬─────────┬─────┘
                           │          │         │
               EPOLLIN  ───┘      EPOLLOUT   EPOLLIN
               (accept)           (write)    (pipe_out)
                           │          │         │
              ┌────────────▼──┐  ┌────▼────┐  ┌▼──────────────┐
              │ Client (K.1)  │  │Client   │  │CgiHandler(K.3)│
              │ read request  │  │send res │  │read cgi output│
              └───────┬───────┘  └─────────┘  └───────────────┘
                      │
         ┌────────────▼────────────┐
         │   HttpRequest (Kişi 2)  │
         │   parse method/headers  │
         └────────────┬────────────┘
                      │
          ┌───────────┼───────────┐
          │           │           │
     ┌────▼───┐  ┌────▼───┐  ┌───▼────────┐
     │  GET   │  │  POST  │  │   CGI?     │
     │(Kişi 2)│  │(Kişi 2)│  │ (Kişi 3)  │
     └────┬───┘  └────┬───┘  └───┬────────┘
          │           │          │
     ┌────▼───────────▼──────────▼────┐
     │     HttpResponse (Kişi 2)      │
     │  build response → client buf   │
     └────────────────────────────────┘
```

---

## 4. Kişi 1 — Core / epoll / Network

> Sunucunun kalbi. Diğer iki kişi bu katmana bağlıdır.
> Başlangıç tarihi: **İlk gün — Kişi 1 olmadan diğerleri test edemez.**

---

### Aşama 1.1 — Proje Altyapısı (1. Gün)

**Görev:** Makefile + dizin yapısı + stub header'lar

```makefile
# Makefile
NAME    = webserv
CXX     = c++
CXXFLAGS= -Wall -Wextra -Werror -std=c++98
SRCS    = src/main.cpp src/Server.cpp src/EpollManager.cpp \
          src/Client.cpp src/HttpRequest.cpp src/HttpResponse.cpp \
          src/HttpHandler.cpp src/MimeTypes.cpp \
          src/ConfigParser.cpp src/ServerConfig.cpp src/CgiHandler.cpp
OBJS    = $(SRCS:.cpp=.o)
INCS    = -Iinclude

all: $(NAME)
$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCS) -c $< -o $@
clean:
	rm -f $(OBJS)
fclean: clean
	rm -f $(NAME)
re: fclean all
.PHONY: all clean fclean re
```

**Tüm header'ların stub hallerini oluştur** → diğer kişiler include edebilsin.

---

### Aşama 1.2 — Client Struct / State Machine (1-2. Gün)

**Dosya:** `include/Client.hpp`, `src/Client.cpp`

```cpp
// Client.hpp
#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <ctime>

enum ClientState {
    READING_REQUEST,
    SENDING_RESPONSE,
    CGI_WRITING_BODY,
    CGI_READING_RESP
};

struct Client {
    int         fd;
    ClientState state;
    std::string req_buf;       // ham HTTP isteği birikir
    std::string res_buf;       // gönderilecek HTTP cevabı
    size_t      res_sent;      // kaç byte gönderildi

    // CGI alanları
    pid_t       cgi_pid;
    int         cgi_pipe_in;   // pipe_in[1]  → CGI stdin'e yaz
    int         cgi_pipe_out;  // pipe_out[0] → CGI stdout'undan oku
    std::string cgi_body_buf;  // POST body (CGI'ya yazılacak)
    size_t      cgi_body_sent;
    time_t      cgi_start;

    Client(int fd);
    void reset();
    bool isCgi() const;
};

#endif
```

---

### Aşama 1.3 — EpollManager (2-3. Gün)

**Dosya:** `include/EpollManager.hpp`, `src/EpollManager.cpp`

```cpp
// EpollManager.hpp
#ifndef EPOLLMANAGER_HPP
#define EPOLLMANAGER_HPP

#include <sys/epoll.h>
#include <map>
#include "Client.hpp"

#define MAX_EVENTS 512

class EpollManager {
public:
    EpollManager();
    ~EpollManager();

    void add(int fd, uint32_t events);
    void mod(int fd, uint32_t events);
    void del(int fd);
    int  wait(epoll_event *events, int timeout_ms);

    int epfd;
};

#endif
```

```cpp
// EpollManager.cpp
#include "EpollManager.hpp"
#include <stdexcept>

EpollManager::EpollManager() {
    epfd = epoll_create1(0);
    if (epfd < 0)
        throw std::runtime_error("epoll_create1 failed");
}

EpollManager::~EpollManager() {
    close(epfd);
}

void EpollManager::add(int fd, uint32_t events) {
    epoll_event ev;
    ev.events  = events;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

void EpollManager::mod(int fd, uint32_t events) {
    epoll_event ev;
    ev.events  = events;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

void EpollManager::del(int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
}

int EpollManager::wait(epoll_event *events, int timeout_ms) {
    return epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);
}
```

---

### Aşama 1.4 — Server + Ana Döngü (3-5. Gün)

**Dosya:** `include/Server.hpp`, `src/Server.cpp`, `src/main.cpp`

```cpp
// Server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <map>
#include "EpollManager.hpp"
#include "Client.hpp"
#include "ServerConfig.hpp"  // Kişi 3'ten gelecek

class Server {
public:
    Server(const std::vector<ServerConfig> &configs);
    ~Server();
    void run();

private:
    EpollManager                 epoll_;
    std::vector<int>             listen_fds_;
    std::map<int, Client*>       clients_;       // fd → Client
    std::map<int, int>           pipe_to_client_; // pipe_fd → client_fd
    std::vector<ServerConfig>    configs_;

    void setupListenSockets();
    void acceptNewClient(int listen_fd);
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleCgiRead(int fd);
    void handleCgiWrite(int fd);
    void disconnectClient(int fd);
    void checkCgiTimeouts();
    int  makeNonBlocking(int fd);
    ServerConfig& findConfig(const std::string &host, int port);
};

#endif
```

```cpp
// src/Server.cpp — Ana döngü (kritik kısım)

void Server::run() {
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_.wait(events, 1000); // 1 sn timeout (CGI check için)

        for (int i = 0; i < n; i++) {
            int fd     = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (isListenFd(fd)) {
                acceptNewClient(fd);
            }
            else if (ev & EPOLLIN) {
                if (pipe_to_client_.count(fd))
                    handleCgiRead(fd);      // CGI stdout
                else
                    handleRead(fd);         // client request
            }
            else if (ev & EPOLLOUT) {
                if (pipe_to_client_.count(fd))
                    handleCgiWrite(fd);     // CGI stdin (POST body)
                else
                    handleWrite(fd);        // client response
            }
            else if (ev & (EPOLLERR | EPOLLHUP)) {
                disconnectClient(fd);
            }
        }

        checkCgiTimeouts(); // 5 sn aşan CGI'ları kill et
    }
}
```

```cpp
// acceptNewClient
void Server::acceptNewClient(int listen_fd) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&addr, &addrlen);
    if (client_fd < 0) return;

    makeNonBlocking(client_fd);
    clients_[client_fd] = new Client(client_fd);
    epoll_.add(client_fd, EPOLLIN);
}
```

```cpp
// handleRead — client'tan HTTP isteği oku
void Server::handleRead(int fd) {
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n <= 0) {              // 0 = bağlantı kapandı, -1 = hata
        disconnectClient(fd);
        return;
    }

    Client *c = clients_[fd];
    c->req_buf.append(buf, n);

    // İstek tamamlandı mı? (Kişi 2'nin parser'ına sor)
    if (HttpRequest::isComplete(c->req_buf)) {
        processRequest(c);
    }
}
```

```cpp
// handleWrite — client'a HTTP cevabı gönder
void Server::handleWrite(int fd) {
    Client *c = clients_[fd];
    ssize_t n = write(fd,
        c->res_buf.c_str() + c->res_sent,
        c->res_buf.size()  - c->res_sent);

    if (n <= 0) {
        disconnectClient(fd);
        return;
    }
    c->res_sent += n;

    if (c->res_sent >= c->res_buf.size()) {
        // Tüm cevap gönderildi
        epoll_.mod(fd, EPOLLIN); // tekrar istek bekle
        c->reset();
    }
}
```

```cpp
// checkCgiTimeouts — 5 sn aşan CGI'ları öldür
void Server::checkCgiTimeouts() {
    time_t now = time(NULL);
    for (std::map<int,Client*>::iterator it = clients_.begin();
         it != clients_.end(); ++it) {
        Client *c = it->second;
        if (c->cgi_pid > 0 && now - c->cgi_start > 5) {
            kill(c->cgi_pid, SIGKILL);
            waitpid(c->cgi_pid, NULL, WNOHANG);
            c->cgi_pid = -1;
            // 504 Gateway Timeout gönder
            c->res_buf = HttpResponse::makeError(504);
            c->res_sent = 0;
            c->state = SENDING_RESPONSE;
            epoll_.mod(c->fd, EPOLLOUT);
        }
    }
}
```

---

### Aşama 1.5 — Signal Handling (5. Gün)

```cpp
// main.cpp
#include <signal.h>

static bool g_running = true;

static void sigHandler(int) { g_running = false; }

int main(int argc, char **argv) {
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGPIPE, SIG_IGN);  // broken pipe'da crash olmaz

    std::string config_path = (argc > 1) ? argv[1] : "webserv.conf";
    ConfigParser parser(config_path);
    std::vector<ServerConfig> configs = parser.parse();

    Server server(configs);
    server.run();
    return 0;
}
```

---

### Kişi 1 Teslim Kriterleri

- [ ] Makefile çalışıyor, re-link yok
- [ ] Server birden fazla porta bind olabiliyor
- [ ] epoll loop hem EPOLLIN hem EPOLLOUT izliyor
- [ ] Tüm fd'ler O_NONBLOCK
- [ ] Client bağlanıp kopunca crash yok
- [ ] CGI timeout mekanizması var

---

## 5. Kişi 2 — HTTP Protocol Layer

> HTTP isteğini parse et, metoda göre işle, cevap oluştur.
> Başlangıç tarihi: **Kişi 1 ile paralel başla** (stub Client.hpp yeterli).

---

### Aşama 2.1 — HttpRequest Parser (1-3. Gün)

**Dosya:** `include/HttpRequest.hpp`, `src/HttpRequest.cpp`

```cpp
// HttpRequest.hpp
#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>

struct HttpRequest {
    std::string method;        // GET POST DELETE
    std::string uri;           // /index.html
    std::string query_string;  // ?foo=bar
    std::string version;       // HTTP/1.1
    std::map<std::string, std::string> headers;
    std::string body;
    bool        is_valid;
    int         error_code;    // 400, 405, 413 vs

    HttpRequest();
    static HttpRequest parse(const std::string &raw);
    static bool        isComplete(const std::string &raw);
    size_t             contentLength() const;
    bool               isChunked() const;
    std::string        host() const;
};

#endif
```

```cpp
// src/HttpRequest.cpp — Temel parse mantığı

bool HttpRequest::isComplete(const std::string &raw) {
    // Header sonu: \r\n\r\n
    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos)
        return false;

    // Content-Length varsa body tamamlandı mı?
    HttpRequest tmp = parse(raw);
    if (tmp.headers.count("content-length")) {
        size_t clen = tmp.contentLength();
        size_t body_start = header_end + 4;
        return (raw.size() - body_start) >= clen;
    }
    return true; // body yoksa header yeterliydi
}

HttpRequest HttpRequest::parse(const std::string &raw) {
    HttpRequest req;
    req.is_valid = false;

    // 1. Request line: "GET /path HTTP/1.1\r\n"
    size_t first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos) return req;

    std::string request_line = raw.substr(0, first_line_end);
    // method, uri, version'ı ayır
    // ... parse işlemi

    // 2. Headers
    size_t header_end = raw.find("\r\n\r\n");
    std::string headers_raw = raw.substr(first_line_end + 2,
                                         header_end - first_line_end - 2);
    // "Key: Value\r\n" formatında parse et

    // 3. Body
    req.body = raw.substr(header_end + 4);

    // 4. Query string'i URI'dan ayır
    size_t q = req.uri.find('?');
    if (q != std::string::npos) {
        req.query_string = req.uri.substr(q + 1);
        req.uri = req.uri.substr(0, q);
    }

    req.is_valid = true;
    return req;
}
```

---

### Aşama 2.2 — HttpResponse Builder (2-3. Gün)

**Dosya:** `include/HttpResponse.hpp`, `src/HttpResponse.cpp`

```cpp
// HttpResponse.hpp
#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <map>

class HttpResponse {
public:
    int         status_code;
    std::string status_text;
    std::map<std::string, std::string> headers;
    std::string body;

    HttpResponse(int code = 200);
    void setHeader(const std::string &key, const std::string &val);
    void setBody(const std::string &content, const std::string &mime);
    std::string build() const;

    // Hazır yanıtlar
    static std::string makeError(int code, const std::string &page = "");
    static std::string makeRedirect(int code, const std::string &location);

    static std::string statusText(int code);
};

#endif
```

```
// Desteklenmesi gereken status kodları:
200 OK
201 Created
204 No Content
301 Moved Permanently
302 Found
400 Bad Request
403 Forbidden
404 Not Found
405 Method Not Allowed
408 Request Timeout
413 Payload Too Large
500 Internal Server Error
501 Not Implemented
502 Bad Gateway
504 Gateway Timeout
```

---

### Aşama 2.3 — HttpHandler (GET/POST/DELETE) (3-5. Gün)

**Dosya:** `include/HttpHandler.hpp`, `src/HttpHandler.cpp`

```cpp
// HttpHandler.hpp
#ifndef HTTPHANDLER_HPP
#define HTTPHANDLER_HPP

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

class HttpHandler {
public:
    // Ana dispatcher — CGI mi, static mi, upload mı?
    static std::string handle(const HttpRequest &req,
                               const ServerConfig &cfg,
                               bool &is_cgi_out);

private:
    static std::string handleGet(const HttpRequest &req,
                                  const LocationConfig &loc);
    static std::string handlePost(const HttpRequest &req,
                                   const LocationConfig &loc);
    static std::string handleDelete(const HttpRequest &req,
                                     const LocationConfig &loc);

    static std::string serveFile(const std::string &path);
    static std::string serveDirectory(const std::string &path,
                                       const std::string &uri);
    static std::string autoIndex(const std::string &dir_path,
                                  const std::string &uri);
    static bool        isCgi(const std::string &path,
                              const LocationConfig &loc);
};

#endif
```

```
GET Akışı:
1. URI'yi dosya yoluna çevir (root + uri)
2. Dosya var mı? → stat()
3. Dizin ise → autoindex açık mı? → listing yap, değilse index dosyası
4. Dosya ise → okuyup 200 response oluştur
5. Yok ise → 404

POST Akışı:
1. Content-Length kontrolü (max_body_size aş? → 413)
2. Upload isteği mi? → body'yi dosyaya kaydet → 201
3. CGI ise → CgiHandler'a devret

DELETE Akışı:
1. Dosya var mı? → unlink() → 204
2. Yok ise → 404
3. Dizin ise → 403
```

---

### Aşama 2.4 — MimeTypes + AutoIndex (4. Gün)

**Dosya:** `include/MimeTypes.hpp`, `src/MimeTypes.cpp`

```cpp
// Desteklenmesi gereken MIME tipleri:
.html  → text/html
.css   → text/css
.js    → application/javascript
.json  → application/json
.png   → image/png
.jpg   → image/jpeg
.gif   → image/gif
.ico   → image/x-icon
.pdf   → application/pdf
.txt   → text/plain
.py    → text/x-python
.php   → application/x-httpd-php
*      → application/octet-stream
```

**AutoIndex HTML Şablonu:**
```html
<!DOCTYPE html>
<html>
<head><title>Index of {URI}</title></head>
<body>
<h1>Index of {URI}</h1>
<hr><pre>
<a href="../">../</a>
{dosya listesi}
</pre><hr>
</body>
</html>
```

---

### Kişi 2 Teslim Kriterleri

- [ ] GET, POST, DELETE methodları çalışıyor
- [ ] Bilinmeyen method → 405 döner, crash yok
- [ ] Eksik/hatalı header → 400 döner
- [ ] Content-Length body boyutuna uymuyor → 400
- [ ] max_body_size aşıldı → 413
- [ ] Dosya yok → 404, dizin ama autoindex kapalı → 403
- [ ] AutoIndex dizin listesi oluşturuyor
- [ ] Tüm status code metinleri doğru

---

## 6. Kişi 3 — Config / CGI / Features

> Config olmadan server ayağa kalkmaz. CGI olmadan bonus gitmez.
> Başlangıç tarihi: **Kişi 1 ile paralel başla.**

---

### Aşama 3.1 — ServerConfig + LocationConfig Struct (1. Gün)

**Dosya:** `include/ServerConfig.hpp`, `include/LocationConfig.hpp`

```cpp
// LocationConfig.hpp
#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <string>
#include <vector>

struct LocationConfig {
    std::string              path;           // /images/
    std::string              root;           // /var/www/html
    std::string              index;          // index.html
    std::vector<std::string> allowed_methods;// GET POST DELETE
    bool                     autoindex;      // on/off
    std::string              cgi_extension;  // .php .py
    std::string              cgi_path;       // /usr/bin/python3
    std::string              upload_dir;     // /var/www/uploads
    std::string              redirect;       // /new/path (301)
    size_t                   max_body_size;  // bytes

    LocationConfig();
};

#endif
```

```cpp
// ServerConfig.hpp
#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include "LocationConfig.hpp"

struct ServerConfig {
    std::string                  host;           // 0.0.0.0
    int                          port;           // 8080
    std::vector<std::string>     server_names;   // example.com
    std::map<int, std::string>   error_pages;    // 404 → /errors/404.html
    size_t                       max_body_size;  // 1MB default
    std::vector<LocationConfig>  locations;

    ServerConfig();
    const LocationConfig* matchLocation(const std::string &uri) const;
};

#endif
```

---

### Aşama 3.2 — ConfigParser (2-3. Gün)

**Dosya:** `include/ConfigParser.hpp`, `src/ConfigParser.cpp`

**Config Dosyası Formatı:**
```nginx
server {
    listen 8080;
    server_name localhost example.com;
    max_body_size 1048576;

    error_page 404 /errors/404.html;
    error_page 500 /errors/500.html;

    location / {
        root /var/www/html;
        index index.html;
        methods GET POST;
        autoindex off;
    }

    location /upload {
        root /var/www;
        methods POST;
        upload_dir /var/www/uploads;
        max_body_size 10485760;
    }

    location /cgi-bin {
        root /var/www/cgi-bin;
        methods GET POST;
        cgi_extension .py;
        cgi_path /usr/bin/python3;
    }

    location /old {
        redirect /new;
    }
}

server {
    listen 9090;
    server_name test.local;

    location / {
        root /var/www/test;
        index index.html;
        methods GET;
        autoindex on;
    }
}
```

```cpp
// ConfigParser.hpp
class ConfigParser {
public:
    ConfigParser(const std::string &path);
    std::vector<ServerConfig> parse();

private:
    std::string              content_;
    size_t                   pos_;

    void               skipWhitespace();
    std::string        nextToken();
    void               parseServer(ServerConfig &cfg);
    void               parseLocation(LocationConfig &loc);
    void               expect(const std::string &token);
    std::runtime_error error(const std::string &msg);
};
```

**Parse Adımları:**
```
1. Dosyayı oku, yorumları (#) temizle
2. Token'lara böl (boşluk/newline'a göre)
3. "server" token → ServerConfig bloğu aç
4. "listen", "server_name" vb. → ilgili alana yaz
5. "location /path" token → LocationConfig bloğu aç
6. Her alan için validation yap
7. Aynı port'ta iki server → hata ver
```

---

### Aşama 3.3 — CgiHandler (3-5. Gün)

**Dosya:** `include/CgiHandler.hpp`, `src/CgiHandler.cpp`

```cpp
// CgiHandler.hpp
#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
#include "Client.hpp"

class CgiHandler {
public:
    // fork + execve + pipe kurulumu
    // pipe_in[1] ve pipe_out[0] epoll'a eklenecek
    static bool launch(Client &client,
                       const HttpRequest &req,
                       const LocationConfig &loc,
                       int epfd);

private:
    static void buildEnv(const HttpRequest &req,
                         const LocationConfig &loc,
                         const std::string &script_path,
                         std::vector<std::string> &env_storage,
                         std::vector<char*> &envp);
};

#endif
```

```cpp
// src/CgiHandler.cpp
bool CgiHandler::launch(Client &client, const HttpRequest &req,
                         const LocationConfig &loc, int epfd)
{
    // 1. Script yolunu oluştur
    std::string script_path = loc.root + req.uri;

    // 2. Pipe'ları kur
    int pipe_in[2], pipe_out[2];
    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0)
        return false;

    // 3. Fork
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // CHILD
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0],  STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);

        // CGI çalışma dizinine git
        chdir(loc.root.c_str());

        // Env hazırla
        std::vector<std::string> env_storage;
        std::vector<char*> envp;
        buildEnv(req, loc, script_path, env_storage, envp);

        char *argv[] = {
            const_cast<char*>(loc.cgi_path.c_str()),
            const_cast<char*>(script_path.c_str()),
            NULL
        };
        execve(argv[0], argv, envp.data());
        exit(1); // execve başarısız olursa
    }

    // PARENT
    close(pipe_in[0]);
    close(pipe_out[1]);

    // Non-blocking yap
    fcntl(pipe_in[1],  F_SETFL, O_NONBLOCK);
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

    client.cgi_pid      = pid;
    client.cgi_pipe_in  = pipe_in[1];
    client.cgi_pipe_out = pipe_out[0];
    client.cgi_start    = time(NULL);

    // POST body varsa CGI stdin'e yazılacak
    if (!req.body.empty()) {
        client.cgi_body_buf  = req.body;
        client.cgi_body_sent = 0;
        client.state = CGI_WRITING_BODY;

        // pipe_in epoll'a ekle (EPOLLOUT)
        epoll_event ev;
        ev.events  = EPOLLOUT;
        ev.data.fd = pipe_in[1];
        epoll_ctl(epfd, EPOLL_CTL_ADD, pipe_in[1], &ev);
    } else {
        close(pipe_in[1]);
        client.cgi_pipe_in = -1;
        client.state = CGI_READING_RESP;
    }

    // pipe_out epoll'a ekle (EPOLLIN)
    epoll_event ev2;
    ev2.events  = EPOLLIN;
    ev2.data.fd = pipe_out[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, pipe_out[0], &ev2);

    return true;
}
```

```cpp
// CGI Ortam Değişkenleri (buildEnv)
void CgiHandler::buildEnv(const HttpRequest &req,
                            const LocationConfig &loc,
                            const std::string &script_path,
                            std::vector<std::string> &storage,
                            std::vector<char*> &envp)
{
    // Zorunlu CGI/1.1 değişkenleri
    storage.push_back("REQUEST_METHOD="  + req.method);
    storage.push_back("QUERY_STRING="    + req.query_string);
    storage.push_back("SCRIPT_FILENAME=" + script_path);
    storage.push_back("SCRIPT_NAME="     + req.uri);
    storage.push_back("PATH_INFO="       + req.uri);
    storage.push_back("SERVER_PROTOCOL=" + req.version);
    storage.push_back("SERVER_SOFTWARE=webserv/1.0");
    storage.push_back("GATEWAY_INTERFACE=CGI/1.1");

    if (req.headers.count("content-type"))
        storage.push_back("CONTENT_TYPE=" + req.headers.at("content-type"));
    if (!req.body.empty())
        storage.push_back("CONTENT_LENGTH=" +
            std::to_string((long long)req.body.size()));
    if (req.headers.count("host"))
        storage.push_back("HTTP_HOST=" + req.headers.at("host"));

    for (size_t i = 0; i < storage.size(); i++)
        envp.push_back(const_cast<char*>(storage[i].c_str()));
    envp.push_back(NULL);
}
```

---

### Aşama 3.4 — Error Pages + Redirect (5. Gün)

```cpp
// ServerConfig::matchLocation — en uzun prefix eşleşmesi
const LocationConfig* ServerConfig::matchLocation(const std::string &uri) const {
    const LocationConfig *best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < locations.size(); i++) {
        const std::string &lpath = locations[i].path;
        if (uri.substr(0, lpath.size()) == lpath) {
            if (lpath.size() > best_len) {
                best_len = lpath.size();
                best = &locations[i];
            }
        }
    }
    return best;
}
```

```
Redirect akışı:
1. matchLocation → location.redirect dolu mu?
2. Evet → 301 Moved Permanently + Location header
3. HttpResponse::makeRedirect(301, loc.redirect)
```

---

### Kişi 3 Teslim Kriterleri

- [ ] Config dosyası parse ediliyor
- [ ] Aynı port iki kez tanımlanırsa hata
- [ ] Yanlış config → server başlamıyor, açıklayıcı hata
- [ ] CGI GET ve POST ile çalışıyor
- [ ] CGI sonsuz döngü → 5 sn sonra kill → 504
- [ ] CGI hata çıkarsa → 502 Bad Gateway
- [ ] Redirect çalışıyor (301)
- [ ] error_page directive çalışıyor
- [ ] Location matching en uzun prefix'e göre

---

## 7. Entegrasyon Noktaları

### Kişi 1 ↔ Kişi 2

```
Kişi 1 (Server::handleRead) → HttpRequest::parse() çağırır
Kişi 1 (Server::processRequest) → HttpHandler::handle() çağırır
Kişi 2 → sonucu string olarak döner
Kişi 1 → client.res_buf'a yazar, EPOLLOUT'a alır
```

### Kişi 1 ↔ Kişi 3

```
Kişi 1 (main.cpp) → ConfigParser::parse() çağırır
Kişi 1 (Server) → ServerConfig listesi alır
Kişi 1 (handleRead) → CgiHandler::launch() çağırır (CGI ise)
Kişi 3 → pipe fd'lerini client'a atar, epoll'a ekler
```

### Kişi 2 ↔ Kişi 3

```
Kişi 2 (HttpHandler::handle) → LocationConfig alır (Kişi 3'ten)
Kişi 2 → location'a göre CGI mi, static mi, upload mı karar verir
Kişi 3 (CgiHandler) → Kişi 2'nin parse ettiği HttpRequest alır
```

### Entegrasyon Sırası

```
Hafta 1: Her kişi kendi modülünü stub ile kurar
Hafta 2: Kişi 1 + Kişi 2 entegrasyon (static GET çalışsın)
Hafta 3: Kişi 3 entegrasyon (config → gerçek port, CGI)
Hafta 4: Test, edge case, siege testi
```

---

## 8. Subject Kritik Kuralları

### Anında 0 Alan Durumlar

| Kural | Kim Sorumlu |
|-------|-------------|
| epoll'suz read/write | Kişi 1 |
| errno okuma sonrası | Kişi 1 + 2 |
| Tek epoll yoksa (birden fazla) | Kişi 1 |
| epoll aynı anda read+write izlemiyor | Kişi 1 |
| Segfault / crash | Herkes |
| Memory leak | Herkes |

### Evaluation Sheet Soruları

```
✓ "Hangi I/O multiplexing kullandınız?" → epoll
✓ "epoll nasıl çalışır?" → Kişi 1 açıklar
✓ "Tek epoll_wait mı?" → Evet, Server::run() içinde
✓ "read/write epoll'dan geçiyor mu?" → Evet, EPOLLIN/EPOLLOUT kontrol
✓ "errno kontrol ediyor musunuz?" → Hayır (grade 0 olur)
✓ "CGI nasıl çalışıyor?" → Kişi 3 açıklar
```

---

## 9. Test ve Evaluation Checklist

### Temel Testler

```bash
# 1. Sunucuyu başlat
./webserv webserv.conf

# 2. Basit GET
curl -i http://localhost:8080/

# 3. POST (upload)
curl -X POST -H "Content-Type: text/plain" \
     --data "hello world" http://localhost:8080/upload

# 4. DELETE
curl -X DELETE http://localhost:8080/upload/file.txt

# 5. Bilinmeyen method (crash olmamalı)
curl -X PATCH http://localhost:8080/
# → 405 Method Not Allowed

# 6. Büyük body (max_body_size aşımı)
curl -X POST -H "Content-Type: text/plain" \
     --data "$(python3 -c 'print("A"*10000000)')" \
     http://localhost:8080/
# → 413 Payload Too Large

# 7. Var olmayan URL
curl -i http://localhost:8080/yok
# → 404 Not Found

# 8. Farklı host ile birden fazla server
curl --resolve example.com:8080:127.0.0.1 http://example.com/
```

### CGI Testleri

```bash
# Python CGI
curl http://localhost:8080/cgi-bin/hello.py

# PHP CGI
curl http://localhost:8080/cgi-bin/hello.php

# POST ile CGI
curl -X POST --data "name=42" http://localhost:8080/cgi-bin/form.py

# Sonsuz döngü (timeout testi)
curl http://localhost:8080/cgi-bin/infinite.py
# → 5 saniyede 504 dönmeli
```

### Siege Stress Testi

```bash
# Önce siege kur
brew install siege    # macOS
apt install siege     # Ubuntu

# %99.5 availability testi
siege -b -t 30S http://localhost:8080/
# Availability: >= 99.50%
# Yanıt vermeyi bırakırsa → FAIL
```

### Bellek Sızıntısı Testi

```bash
valgrind --leak-check=full --track-fds=yes ./webserv webserv.conf
# Ctrl+C ile kapat
# "All heap blocks were freed" görünmeli
```

---

## Başlangıç Takvimi (Öneri)

| Gün | Kişi 1 | Kişi 2 | Kişi 3 |
|-----|--------|--------|--------|
| 1 | Makefile + dizin + stub headerlar | HttpRequest parse başlangıç | ServerConfig + LocationConfig struct |
| 2-3 | EpollManager + Client struct | HttpRequest parse tamamla | ConfigParser token parse |
| 3-4 | Server socket + accept | HttpResponse builder | ConfigParser blok parse |
| 4-5 | epoll ana döngü | GET/POST/DELETE handler | CgiHandler launch |
| 5-6 | CGI pipe epoll entegrasyonu | AutoIndex + MimeTypes | CGI env + redirect |
| 7 | **Entegrasyon: K1+K2 static GET** | ← | → |
| 8-9 | **Entegrasyon: K3 config + CGI** | ← | → |
| 10-14 | Test, debug, siege | Test, edge case | Test, timeout, error pages |
