*Bu proje, 42 müfredatının bir parçası olarak tuzan, husarpka, merilhan tarafından oluşturulmuştur.*

> Not: Subject, `README.md` dosyasının İngilizce olmasını zorunlu tutuyor — bu yüzden resmi/değerlendirilen dosya köke bırakılan İngilizce `README.md`'dir. Bu dosya (`README_TR.md`) sadece kendi ekibimiz için hazırlanmış gönüllü bir çeviridir.

# webserv

## Açıklama

**webserv**, 42 okulunun "webserv" projesi için sıfırdan, tamamen kendi yazdığımız C++98 ile geliştirilmiş bir HTTP/1.1 sunucusudur. Kendi non-blocking socket event loop'unu (her dinleme soketi, client bağlantısı ve CGI pipe'ı için tek bir `poll()`), kendi HTTP istek/yanıt ayrıştırmasını, nginx'ten ilham alan kendi config dosyası formatını ve kendi CGI/1.1 gateway'ini (fork + pipe + execve) barındırır — kod tabanının hiçbir yerinde harici bir HTTP, ağ veya template kütüphanesi kullanılmaz.

Projenin amacı, HTTP'yi gerçek bir tarayıcının, `curl`'ün ve CGI script'lerinin (PHP-CGI, Python, ...) kendi yazdığınız bir sunucuya karşı doğru çalışmasını sağlayacak kadar iyi anlamaktır: keep-alive ve pipelining, chunked transfer-encoding, virtual host'lar, statik dosya servisi, dosya upload/delete, dizin listeleme, özel error page'ler ve CGI çalıştırma — hepsi tek, non-blocking bir I/O döngüsüyle yürütülür.

Her modülün, her desteklenen HTTP status kodunun, CGI ortamının ve eklenen güvenlik sertleştirmelerinin tam teknik dökümü için bkz. **[NOTES_TR.md](NOTES_TR.md)** (İngilizcesi: [NOTES.md](NOTES.md)).

## Kullanım Talimatları

### Derleme

```sh
make            # ./webserv'i derler
make re          # temiz yeniden derleme
make clean        # object dosyalarini siler (objs/)
make fclean       # object dosyalarini VE binary'yi siler
```

`c++ -Wall -Wextra -Werror -std=c++98` ile derlenir. Object dosyalari `sources/` modul yerlesimini yansitarak `objs/` altina yerlesir (`objs/Core/`, `objs/Http/`, `objs/Utils/`, `objs/Config/`, `objs/Cgi/`); `sources/` ve `includes/` build ciktilarindan temiz kalir.

### Çalıştırma

```sh
./webserv [config dosyasi]
```

Config dosyası verilmezse `configs/default.conf` kullanılır. O dosyanın başındaki yorum bloğunda, her mandatory özelliği somut bir `curl` komutuna eşleyen hazır bir kullanım rehberi var.

### Test

`tests/` altında üç bağımsız test paketi var:

```sh
python3 tests/crash_test.py     # 35 bozuk/kotu-niyetli istek senaryosu; sunucunun asla cokmedigini dogrular
python3 tests/feature_test.py   # subject'in tum mandatory ozelliklerini configs/default.conf uzerinden uctan uca test eder
./tests/curl_tests.sh           # gelistirme boyunca biriken tam curl (+ birkac raw-socket) regresyon paketi
```

`tests/tester` ve `tests/cgi_tester`, subject ile birlikte intra'da verilen resmi Go binary'leridir; `tests/YoupiBanane/` ve `configs/tester_intra.conf` bunların gerektirdiği fixture dizini ve config'tir. Çalıştırmak için:

```sh
./webserv configs/tester_intra.conf &
tests/tester http://127.0.0.1:8083
```

## Kaynaklar

- [RFC 7230](https://www.rfc-editor.org/rfc/rfc7230) — HTTP/1.1: Message Syntax and Routing
- [RFC 7231](https://www.rfc-editor.org/rfc/rfc7231) — HTTP/1.1: Semantics and Content (status kodlari, method'lar)
- [RFC 6585](https://www.rfc-editor.org/rfc/rfc6585) — Additional HTTP Status Codes (429, 431)
- [RFC 3875](https://www.rfc-editor.org/rfc/rfc3875) — The Common Gateway Interface (CGI) Version 1.1
- [RFC 1945](https://www.rfc-editor.org/rfc/rfc1945) — HTTP/1.0 (projenin baz aldigi referans)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — socket'ler, `poll()`, non-blocking I/O
- [NGINX dokumantasyonu](https://nginx.org/en/docs/) — davranis kiyaslamasi icin boyunca referans alindi (config sozdizimi, status kodlari, dizin listeleme/error-page semantigi)
- Her whitelist'teki syscall icin `man` sayfalari (`poll`, `fcntl`, `execve`, `waitpid`, `getaddrinfo`, ...)

### AI Kullanımı

Bu proje boyunca bir AI asistani (Claude, Claude Code uzerinden) kullanildi, her seferinde elle gozden gecirilip dogrulandiktan sonra tutuldu. Somut olarak sunlar icin kullanildi:

- **Mimari refactor**: baslangicta tek, buyuk bir `Server` sinifini, subject'in C++98 kisitlarina uygun sekilde (Orthodox Canonical Form ile struct'lardan class'lara donusum dahil) simdiki `Utils` / `Http` / `Config` / `Cgi` / `Core` modul yapisina ayirma (bkz. NOTES_TR.md).
- **Kod incelemesi / guvenlik denetimi**: somut sorunlarin bulunup duzeltilmesi — autoindex ciktisinda bir reflected/stored XSS (escape edilmemis dosya adlari), RFC 7230'a gore eksik Host-header/HTTP-version dogrulamasi, bir Content-Length ayristirma edge-case'i, ve path-traversal / null-byte-injection savunmalari.
- **Performans hata ayiklama**: chunked transfer-encoding decoder'inda gercek bir O(n²) hatasinin teshisi (buyuk chunked upload'lar her yeni TCP segmentinde bastan yeniden ayristiriliyordu) — resmi CGI tester'a karsi coklu-dakikalik bir donma olarak ortaya cikmisti — ve decoder'in kademeli (O(n)) hale getirilmesi.
- **CGI/1.1 uyumlulugu**: resmi `cgi_tester` binary'sini kontrollu ortam degiskenleriyle dogrudan test ederek tam olarak hangi `SCRIPT_NAME`/`PATH_INFO` konvansiyonunu bekledigini bulma, ve secilen konvansiyonun RFC 3875 §4.1.13'e uygun oldugunu dogrulama.
- **Test altyapisi**: yukarida listelenen uc test paketinin (`crash_test.py`, `feature_test.py`, `curl_tests.sh`) yazilmasi.

Asistan tarafindan onerilen her degisiklik, kabul edilmeden once gercek `curl`/tarayici/CGI davranisina karsi (yukaridaki bolumler icin ayrica resmi subject tester'ina karsi da) derlenip calistirilip kontrol edildi — hicbir sey incelenmeden birlestirilmedi.

## Özellikler

- Keep-alive, pipelining ve chunked transfer-encoding (istek un-chunking, kademeli/O(n)) ile HTTP/1.1
- GET, POST, DELETE, route-bazli method kisitlamasi (aksi halde 405)
- Statik dosya servisi, dizin index dosyalari, dizin listeleme (`autoindex`)
- Yapilandirilabilir bir dizine dosya upload'u, dosya silme
- HTTP redirection (ic path'ler ve mutlak `http://`/`https://` hedefleri, 301/302/303/307/308)
- Dosya uzantisina gore CGI/1.1 calistirma (fork + pipe + execve), tam ortam degiskeni seti, CGI'ye ulasmadan once chunked-govde un-chunking, non-blocking zombie reap ile CGI timeout
- Birden fazla `listen` interface:port cifti, nginx-tarzi wildcard/spesifik-IP socket merge, `server_name` + `Host` header ile opsiyonel virtual hosting
- Sadece server-seviyesinde degil, location-bazinda da uygulanan `client_max_body_size`
- Status kodu basina ozel error page, hicbiri tanimli degilse yerlesik varsayilan sayfa
- Guvenlik sertlestirmesi: path traversal, null-byte injection, percent-encoding dogrulamasi, response-splitting'e karsi guvenli redirect'ler, XSS-escape'li autoindex ciktisi, Slowloris-tarzi yavas-istek timeout'u, request-smuggling'e karsi guvenli Content-Length/Transfer-Encoding isleme

Her modulun tam dokumu, desteklenen tum HTTP status kodlarinin listesi, CGI ortam degiskeni tablosu ve her onemli tasarim kararinin arkasindaki gerekce icin [NOTES_TR.md](NOTES_TR.md) dosyasina bakin.
