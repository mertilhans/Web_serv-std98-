# WEBSERV — TEAM PROGRESS
> Her kişi kendi aşamasını tamamlayınca ilgili kutucuğu işaretler ve notunu ekler.

---

## Takım Üyeleri

| Kişi | 42 Login | Sorumluluk |
|------|----------|------------|
| Kişi 1 | `__merilhan_` | Core / epoll / Network |
| Kişi 2 | `___________` | HTTP Protocol Layer |
| Kişi 3 | `___________` | Config / CGI / Features |

---

## Kişi 1 — Core / epoll / Network

### Aşama 1.1 — Proje Altyapısı
- [ ] Makefile yazıldı (re-link yok, tüm kurallar var)
- [ ] Dizin yapısı oluşturuldu
- [ ] Tüm stub header'lar oluşturuldu
- [ ] `c++ -Wall -Wextra -Werror -std=c++98` ile derleniyor

**Not:**

---

### Aşama 1.2 — Client Struct / State Machine
- [ ] `Client` struct yazıldı
- [ ] State enum tanımlandı (READING_REQUEST, SENDING_RESPONSE, CGI_WRITING_BODY, CGI_READING_RESP)
- [ ] CGI alanları (cgi_pid, cgi_pipe_in, cgi_pipe_out, cgi_start) eklendi
- [ ] `reset()` fonksiyonu çalışıyor

**Not:**

---

### Aşama 1.3 — EpollManager
- [ ] `epoll_create1()` ile epoll fd oluşturuluyor
- [ ] `add()`, `mod()`, `del()`, `wait()` fonksiyonları çalışıyor
- [ ] Tek bir epoll instance var (birden fazla YASAK)

**Not:**

---

### Aşama 1.4 — Server + Ana Döngü
- [ ] Listen socket'lar oluşturuluyor (socket, bind, listen)
- [ ] Tüm fd'ler `O_NONBLOCK` yapılıyor (fcntl)
- [ ] `epoll_wait` hem `EPOLLIN` hem `EPOLLOUT` izliyor
- [ ] `accept()` yeni client ekliyor, epoll'a kaydediyor
- [ ] `handleRead()` — client'tan istek okunuyor
- [ ] `handleWrite()` — client'a cevap gönderiliyor
- [ ] `handleCgiRead()` — CGI stdout okunuyor
- [ ] `handleCgiWrite()` — CGI stdin'e POST body yazılıyor
- [ ] `disconnectClient()` — fd'ler kapatılıyor, map'ten siliniyor
- [ ] `pipe_to_client_` map'i ile pipe fd → client fd eşleşmesi var
- [ ] `EPOLLERR | EPOLLHUP` gelince client düzgün kapatılıyor

**Not:**

---

### Aşama 1.5 — Signal Handling
- [ ] `SIGPIPE` ignore edildi (`SIG_IGN`)
- [ ] `SIGINT` / `SIGTERM` graceful shutdown yapıyor
- [ ] Shutdown sırasında tüm fd'ler kapatılıyor

**Not:**

---

### Aşama 1.6 — CGI Timeout
- [ ] `checkCgiTimeouts()` her epoll döngüsünde çağrılıyor
- [ ] 5 saniye aşan CGI process'i `kill(SIGKILL)` ile sonlandırılıyor
- [ ] `waitpid(WNOHANG)` ile zombie process oluşmuyor
- [ ] Client'a 504 Gateway Timeout dönüyor

**Not:**

---

### Kişi 1 Genel Durum
- [ ] Birden fazla porta aynı anda bind olunabiliyor
- [ ] Stress altında crash yok
- [ ] Valgrind: memory leak yok, fd leak yok
- [ ] `siege -b -t 30S` — Availability >= %99.5

**Tamamlanma tarihi:** `___/___/______`

---

## Kişi 2 — HTTP Protocol Layer

### Aşama 2.1 — HttpRequest Parser
- [ ] Request line parse ediliyor (method, URI, version)
- [ ] Headers parse ediliyor (key: value formatı)
- [ ] Body ayrıştırılıyor (Content-Length'e göre)
- [ ] Query string URI'dan ayrılıyor (`?` sonrası)
- [ ] `isComplete()` — header sonu `\r\n\r\n` ve body tamamlanmasını kontrol ediyor
- [ ] Hatalı istek → `is_valid = false`, `error_code = 400`
- [ ] Büyük/küçük harf duyarsız header anahtarları

**Not:**

---

### Aşama 2.2 — HttpResponse Builder
- [ ] Status line oluşturuluyor (`HTTP/1.1 200 OK`)
- [ ] Default headerlar ekleniyor (`Content-Type`, `Content-Length`, `Connection`)
- [ ] `build()` tek string döndürüyor
- [ ] `makeError(code)` hazır hata cevabı oluşturuyor
- [ ] `makeRedirect(code, location)` redirect cevabı oluşturuyor
- [ ] Tüm status kodları doğru metinlerle eşleşiyor:
  - [ ] 200 OK
  - [ ] 201 Created
  - [ ] 204 No Content
  - [ ] 301 Moved Permanently
  - [ ] 302 Found
  - [ ] 400 Bad Request
  - [ ] 403 Forbidden
  - [ ] 404 Not Found
  - [ ] 405 Method Not Allowed
  - [ ] 413 Payload Too Large
  - [ ] 500 Internal Server Error
  - [ ] 502 Bad Gateway
  - [ ] 504 Gateway Timeout

**Not:**

---

### Aşama 2.3 — GET Handler
- [ ] URI → dosya yolu çevrimi yapılıyor (root + uri)
- [ ] Dosya varsa okunup 200 dönüyor
- [ ] Dizinse → index dosyası aranıyor
- [ ] Dizin + autoindex açıksa → listing dönüyor
- [ ] Dizin + autoindex kapalıysa → 403 dönüyor
- [ ] Dosya yoksa → 404 dönüyor
- [ ] Yetki yoksa → 403 dönüyor

**Not:**

---

### Aşama 2.4 — POST Handler
- [ ] Content-Length kontrolü yapılıyor
- [ ] max_body_size aşılırsa → 413 dönüyor
- [ ] Upload isteği → dosya kaydediliyor → 201 dönüyor
- [ ] CGI isteği → CgiHandler'a devrediliyor

**Not:**

---

### Aşama 2.5 — DELETE Handler
- [ ] Dosya varsa `unlink()` → 204 dönüyor
- [ ] Dosya yoksa → 404 dönüyor
- [ ] Dizin silinmeye çalışılırsa → 403 dönüyor

**Not:**

---

### Aşama 2.6 — MimeTypes + AutoIndex
- [ ] Yaygın MIME tipleri tanımlı (.html, .css, .js, .png, .jpg, .pdf vb.)
- [ ] Bilinmeyen uzantı → `application/octet-stream`
- [ ] AutoIndex HTML sayfası oluşturuluyor (dizin listesi)
- [ ] AutoIndex'te dosya adları tıklanabilir link olarak çıkıyor

**Not:**

---

### Kişi 2 Genel Durum
- [ ] GET, POST, DELETE — `curl` ile test edildi
- [ ] Bilinmeyen method (PATCH, PUT) → 405, crash yok
- [ ] Hatalı header → 400
- [ ] Büyük body → 413
- [ ] `curl -i` ile response headerları doğru görünüyor
- [ ] Tarayıcıda statik site açılıyor

**Tamamlanma tarihi:** `___/___/______`

---

## Kişi 3 — Config / CGI / Features

### Aşama 3.1 — ServerConfig + LocationConfig
- [ ] `ServerConfig` struct: host, port, server_names, error_pages, max_body_size, locations
- [ ] `LocationConfig` struct: path, root, index, allowed_methods, autoindex, cgi_extension, cgi_path, upload_dir, redirect, max_body_size
- [ ] `matchLocation()` — en uzun prefix eşleşmesi yapıyor

**Not:**

---

### Aşama 3.2 — ConfigParser
- [ ] Dosya okunuyor, yorumlar (#) temizleniyor
- [ ] `server { }` bloğu parse ediliyor
- [ ] `location /path { }` bloğu parse ediliyor
- [ ] `listen` → port ayarlanıyor
- [ ] `server_name` → birden fazla isim destekleniyor
- [ ] `error_page 404 /path` → error_pages map'e ekleniyor
- [ ] `max_body_size` → byte olarak parse ediliyor
- [ ] `methods GET POST DELETE` → allowed_methods'a ekleniyor
- [ ] `autoindex on/off` → bool'a çevriliyor
- [ ] `cgi_extension` ve `cgi_path` parse ediliyor
- [ ] `upload_dir` parse ediliyor
- [ ] `redirect /yeni/yol` parse ediliyor
- [ ] Aynı port iki kez → hata mesajıyla çıkış
- [ ] Geçersiz config → açıklayıcı hata, server başlamıyor

**Not:**

---

### Aşama 3.3 — CgiHandler
- [ ] `pipe(pipe_in)` ve `pipe(pipe_out)` oluşturuluyor
- [ ] `fork()` yapılıyor
- [ ] Child: `dup2` ile pipe'lar stdin/stdout'a bağlanıyor
- [ ] Child: `chdir()` ile CGI dizinine geçiliyor
- [ ] Child: `execve()` ile CGI çalıştırılıyor
- [ ] Parent: kullanılmayan pipe uçları kapatılıyor
- [ ] Parent: `fcntl(O_NONBLOCK)` uygulanıyor
- [ ] `pipe_in[1]` epoll'a EPOLLOUT ile ekleniyor (POST body için)
- [ ] `pipe_out[0]` epoll'a EPOLLIN ile ekleniyor
- [ ] CGI env değişkenleri doğru ayarlanıyor:
  - [ ] REQUEST_METHOD
  - [ ] QUERY_STRING
  - [ ] SCRIPT_FILENAME
  - [ ] CONTENT_TYPE
  - [ ] CONTENT_LENGTH
  - [ ] HTTP_HOST
  - [ ] SERVER_PROTOCOL
  - [ ] GATEWAY_INTERFACE
- [ ] CGI çıkışında `waitpid(WNOHANG)` çağrılıyor (zombie yok)
- [ ] CGI hata kodu != 0 → 502 Bad Gateway

**Not:**

---

### Aşama 3.4 — Error Pages + Redirect
- [ ] Config'deki `error_page` directive'i çalışıyor
- [ ] Özel error page dosyası varsa okunup dönüyor
- [ ] Dosya yoksa default HTML error page oluşturuluyor
- [ ] `redirect` → 301 + Location header dönüyor
- [ ] Tarayıcıda redirect test edildi

**Not:**

---

### Kişi 3 Genel Durum
- [ ] Birden fazla server block çalışıyor (farklı portlar)
- [ ] `curl --resolve` ile farklı server_name test edildi
- [ ] CGI GET ile çalışıyor
- [ ] CGI POST ile çalışıyor
- [ ] CGI timeout (sonsuz döngü) → 5sn → 504
- [ ] Hatalı config → server başlamıyor, açıklayıcı hata

**Tamamlanma tarihi:** `___/___/______`

---

## Entegrasyon Testleri

> Üç kişi de kendi aşamalarını bitirince bu bölümü birlikte doldurun.

### K1 + K2 Entegrasyonu (Statik Sunucu)
- [ ] `curl http://localhost:PORT/` → 200 + HTML
- [ ] `curl http://localhost:PORT/yok` → 404
- [ ] `curl -X DELETE http://localhost:PORT/dosya` → 204
- [ ] `curl -X PATCH http://localhost:PORT/` → 405
- [ ] Tarayıcıda statik site açılıyor

### K3 Entegrasyonu (Config + CGI)
- [ ] Config dosyasıyla birden fazla port açılıyor
- [ ] CGI endpoint'i çalışıyor
- [ ] Redirect çalışıyor
- [ ] Custom error page görünüyor
- [ ] Upload çalışıyor

### Siege Stress Testi
- [ ] `siege -b -t 30S http://localhost:PORT/` komutu çalıştırıldı
- [ ] Availability: `_______%` (>= %99.5 olmalı)
- [ ] Response time: `_______ms`
- [ ] Uzun süre sonra memory artmıyor (valgrind kontrol edildi)

---

## Evaluation Hazırlık

> Evaluator bunları soracak — herkes cevabını bilmeli.

| Soru | Cevap Verecek Kişi |
|------|--------------------|
| Hangi I/O multiplexing kullandınız? | Kişi 1 |
| epoll nasıl çalışır, select'ten farkı? | Kişi 1 |
| Tek epoll_wait mı kullanıyorsunuz? | Kişi 1 |
| read/write epoll'dan geçiyor mu? | Kişi 1 |
| errno read/write sonrası kontrol ediliyor mu? | Kişi 1 |
| HTTP request nasıl parse ediliyor? | Kişi 2 |
| GET/POST/DELETE farkları nedir? | Kişi 2 |
| CGI nasıl çalışıyor? | Kişi 3 |
| fork() neden sadece CGI için? | Kişi 3 |
| Config dosyası nasıl parse ediliyor? | Kişi 3 |

---

## Notlar

> Toplantı kararları, değişen mimari kararlar, önemli bug'lar buraya yazılır.

```
Tarih: ___/___/______
Not:


Tarih: ___/___/______
Not:


Tarih: ___/___/______
Not:
```
