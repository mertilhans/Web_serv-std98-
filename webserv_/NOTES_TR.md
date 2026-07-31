# webserv — Notlar

Bu dosya, projenin derin teknik referansidir: her modulun ne yaptigi, HTTP davranisinin tam olarak nasil ve neden bu sekilde implemente edildigi, tam CGI ortami, desteklenen her status kodu, config dosyasi formati ve eklenen guvenlik sertlestirmeleri. [README_TR.md](README_TR.md) kisa giris noktasidir; gerekceler burada.

## İçindekiler

1. [Temel kavramlar](#temel-kavramlar)
2. [Tasarim felsefesi](#tasarim-felsefesi)
3. [Dizin yerlesimi](#dizin-yerlesimi)
4. [Modul referansi](#modul-referansi)
5. [Event loop: tek `poll()` ile non-blocking I/O](#event-loop)
6. [HTTP/1.1 ozellik destegi](#http11-ozellik-destegi)
7. [Desteklenen HTTP status kodlari](#desteklenen-http-status-kodlari)
8. [Config dosyasi referansi](#config-dosyasi-referansi)
9. [CGI/1.1](#cgi11)
10. [Guvenlik sertlestirmesi](#guvenlik-sertlestirmesi)
11. [Onemli tasarim kararlari](#onemli-tasarim-kararlari)
12. [Test](#test)

---

## Temel kavramlar

Bu bolum, projede gecen ve genel olarak webserv/HTTP dunyasinda bilinmesi gereken TUM temel kavramlari, koddan bagimsiz, salt egitici olarak anlatir. Amac: "biz ne yaptik" degil, "bu kavramlar ne anlama geliyor, neden var" sorusunu cevaplamak.

### HTTP nedir, nasil calisir

HTTP (HyperText Transfer Protocol), bir **client** (tarayici, curl, CGI test araci) ile bir **server** arasinda **istek/yanit (request/response)** ciftleri seklinde calisan, **stateless** (durumsuz) bir metin-tabanli protokoldur. "Stateless" demek: sunucu, bir istekten bir sonrakine hicbir sey hatirlamaz (aksi bir mekanizma -- cookie/session -- eklenmedikce); her istek kendi basina tam olarak yorumlanabilir olmalidir. Bu proje boyunca "keep-alive" ile "ayni TCP baglantisini acik tutmak" ile "state tutmak" birbirine KARISTIRILMAMALI -- keep-alive sadece baglanti/soketin yeniden kullanilmasidir, HTTP'nin stateless dogasini degistirmez.

Bir HTTP istegi 3 parcadan olusur:

1. **Istek satiri**: `METHOD PATH HTTP/VERSION` (orn. `GET /index.html HTTP/1.1`)
2. **Header'lar**: `Anahtar: Deger` seklinde satirlar (`Host`, `Content-Length`, `Content-Type`, `Connection`, ...), bos bir satirla (`\r\n\r\n`) biter
3. **Govde** (opsiyonel): POST gibi metodlarda gonderilen veri

Bir HTTP yaniti da benzer yapida: `HTTP/VERSION STATUS_KODU STATUS_METNI` satiri + header'lar + bos satir + govde.

### HTTP versiyon farkları

| Versiyon | Temel fark |
|---|---|
| HTTP/0.9 | Sadece `GET`, header yok, sadece HTML donebilir -- tarihsel, artik kullanilmiyor |
| HTTP/1.0 | Header'lar eklendi, ama VARSAYILAN olarak her istek icin YENI bir TCP baglantisi acilip kapatilir (`Connection: keep-alive` ACIKCA istenirse baglanti acik kalabilir) |
| **HTTP/1.1** (bu proje bunu hedefliyor) | **Keep-alive varsayilan** (aksi `Connection: close` ile istenmedikce baglanti acik kalir), **pipelining** (ayni baglantida bekelemeden ardisik istek gonderme), **chunked transfer-encoding** (govde uzunlugu onceden bilinmeden gonderilebilir), `Host` header'i ZORUNLU (virtual hosting'in temeli) |
| HTTP/2 | Ikili (binary) framing, tek baglantida coklu istek/yanitin PARALEL (multiplexed) gitmesi, header sikistirma -- bu proje kapsaminda DEGIL |
| HTTP/3 | HTTP/2'nin TCP yerine QUIC/UDP uzerinde calisan hali -- bu proje kapsaminda DEGIL |

Subject, HTTP/1.0'i "referans nokta" olarak onerip tam RFC uyumu istemiyor; biz HTTP/1.1'i (keep-alive+pipelining+chunked dahil) hedef aldik cunku gercek tarayicilar/curl varsayilan olarak bunu konusuyor.

### URI/URL'in parcalari

Bir URL soyle parcalanir: `scheme://host:port/path?query#fragment`

Ornek: `http://127.0.0.1:8080/cgi-bin/hello.py?name=webserv`

| Parca | Bu ornekte | Aciklama |
|---|---|---|
| **scheme** | `http` | Hangi protokol kullanilacak (`http`, `https`, `ftp`, ...) |
| **host** | `127.0.0.1` | Hangi sunucuya baglanilacak (IP veya domain adi) |
| **port** | `8080` | Sunucudaki hangi kapiya (soket) baglanilacak -- verilmezse http icin varsayilan 80'dir |
| **path** | `/cgi-bin/hello.py` | Sunucudaki hangi kaynak isteniyor |
| **query string** | `name=webserv` | `?` sonrasi, `anahtar=deger&anahtar2=deger2` seklinde ek parametreler -- CGI'de `QUERY_STRING` olarak aynen iletilir |
| **fragment** | *(bu ornekte yok, orn. `#section1`)* | `#` sonrasi -- TARAYICIDA kalir, SUNUCUYA hic gonderilmez (CGI/webserv'in gormesi imkansizdir) |

Bizim projede `HttpRequest::parseRequestLine`, istek satirindaki hedefi `?` karakterinden path ve query olarak ikiye boler; path'i URL-decode eder (`%20` gibi percent-encoding'i coz"er, cunku dosya sistemine erisimde kullanilir), query'yi ise KASITLI OLARAK ham/encoded birakir (CGI/1.1 spec'i boyle ister).

### Percent-encoding (URL encoding) nedir

URL'lerde bosluk, Turkce karakter, `?`/`&`/`#` gibi ozel anlami olan karakterler DOGRUDAN yazilamaz -- bunlarin yerine `%` + iki hex hane (`%20` = bosluk, `%C3%BC` = "ü" UTF-8'de) yazilir. Sunucu bunu "decode" ederek gercek karaktere cevirir. Bizim `HttpUtils::urlDecode`'umuz bunu yapar, ayrica decode SONRASI bir null bayt (`%00`) veya bozuk bir `%` dizisi (2 hex hane degil) cikarsa istegi reddeder -- bu, "null-byte injection" adi verilen bir saldiri sinifina karsi savunmadir (asagida ayrintili).

### Client-Server modeli ve socket

Bir **socket**, isletim sisteminin agdan gelen/giden veriyi okuyup yazmak icin verdigin bir "kapi" (dosya tanimlayicisi/file descriptor, `int`). Sunucu tarafinda akis soyledir:

1. `socket()` -- bir socket olustur
2. `bind()` -- bu socket'i belirli bir IP+port'a bagla
3. `listen()` -- gelen baglanti taleplerini kabul etmeye basla
4. `accept()` -- bir client baglanmaya calistiginda, o baglanti icin YENI bir socket (fd) dondur -- asil veri okuma/yazma BUNUN uzerinden olur, dinleme socket'i uzerinden degil
5. `read()`/`write()` -- o client fd'si uzerinden veri al/gonder
6. `close()` -- baglantiyi kapat

Client tarafinda ise `connect()` ile sunucuya baglanilir. Bu, alttan TCP'nin "3-way handshake"i (SYN → SYN-ACK → ACK) ile kurulur -- bu adim isletim cekirdegi (kernel) tarafindan otomatik yapilir, bizim kodumuz bunu gormez, sadece `accept()`'in donmesini bekler.

### Blocking vs non-blocking I/O

**Blocking (varsayilan) davranis**: `read(fd, ...)` cagrisi, o fd'de veri gelene kadar (ya da baglanti kapanana kadar) THREAD'I DURDURUR -- program o satirda "asili" kalir. Tek thread'li bir sunucuda bu FELAKET olur: bir client yavassa (ya da hic veri gondermezse), o TEK thread tikanir, DIGER TUM client'lar bekler.

**Non-blocking davranis**: fd, `fcntl(fd, F_SETFL, O_NONBLOCK)` ile isaretlenir. Artik `read()`/`write()`, veri HAZIR DEGILSE THREAD'I DURDURMAZ, hemen `-1` (errno=EAGAIN/EWOULDBLOCK, ama biz subject kurali geregi errno'ya BAKMIYORUZ) donup devam eder. Ama bu da tek basina yetmez: "veri gelince haber ver" mekanizmasi olmadan, sürekli read() deneyip "veri var mi" diye sormak (busy-loop/polling) CPU'yu bosuna tuketirdi.

### `poll()` (ve `select`/`epoll`/`kqueue`) neden var

`poll()`, TEK bir cagriyla, "bu fd listesindeki HANGILERI su an okumaya/yazmaya HAZIR?" sorusunu isletim cekirdegine sorar ve cekirdek, hazir olan biri olana kadar thread'i (verimli sekilde, CPU harcamadan) bekletir. Hazir olan(lar) geldiginde donup bize SADECE onlari soyler -- biz de SADECE o fd'ler uzerinde `read`/`write` yapariz. Bu sayede TEK bir thread, YUZLERCE client'i, hicbirini bloklamadan, hicbirini bosuna yoklamadan (busy-loop yapmadan) yonetebilir. `select()`/`epoll()`/`kqueue()` ayni sorunu cozen farkli API'lerdir (epoll Linux'e, kqueue BSD/macOS'a ozel, select en eski/en sinirli olanidir); subject `poll()`'u onceden secmis olsa da hepsini esdeger sayiyor.

Subject'in en katı kurallarindan biri: **TEK bir `poll()` cagrisi TUM fd'ler icin** kullanilmali (her client icin ayri poll() DEGIL), ve **`poll()` "hazir" demeden ASLA `read`/`write` yapilmamali** -- bu ikisi bizim projede `Server::listeningSockets()`'teki TEK dongude, TEK `mPollFds` vektoruyle saglaniyor.

### Dosya tanimlayici (file descriptor) kavrami

Linux'ta "her sey bir dosyadir" felsefesi geregi, bir socket, bir pipe, gercek bir disk dosyasi, hepsi ayni turden bir tam sayi (`int`) ile -- **file descriptor (fd)** -- temsil edilir. `poll()` de bu yuzden hem socket'leri hem CGI pipe'larini ayni listede, ayni sekilde izleyebilir.

### CGI (Common Gateway Interface) nedir

CGI, bir web sunucusunun, statik bir dosya donmek yerine **bir PROGRAM CALISTIRIP onun ciktisini** yanit olarak donmesini saglayan, cok eski (1990'lardan) ama hala calisan bir standarttir. Akis: sunucu `fork()` ile kendini ikiye boler, child process `execve()` ile baska bir programa (python3, php-cgi, ...) DONUSUR; istek bilgisi (method, path, header'lar) CGI'ye **ortam degiskenleri (environment variables)** olarak, govde ise CGI'nin **stdin**'i uzerinden verilir; CGI'nin **stdout**'una yazdigi (header+bos satir+govde) sunucu tarafindan okunup client'a HTTP yaniti olarak iletilir. Bu sayede sunucu, "PHP nasil calisir" ya da "Python nasil calisir" bilmek ZORUNDA degildir -- sadece bu basit stdin/stdout/env-var sozlesmesine uyar.

### `fork()`, `execve()`, zombie process, sinyaller

- **`fork()`**: mevcut process'i, bellegi/durumu ile birlikte AYNEN kopyalayarak IKI ayri process yaratir (parent + child); donus degeriyle hangisinin "sen" oldugun anlasilir (child'da 0, parent'ta child'in pid'i).
- **`execve()`**: cagiran process'in kendisini, TAMAMEN BASKA bir programla (kod, bellek, hersey) DEGISTIRIR -- geri donmez (basarili olursa).
- **Zombie process**: bir child process bitince (exit), isletim sistemi onun cikis kodunu parent `waitpid()` ile "toplayana" (reap) kadar process tablosunda bir "zombi" olarak tutar. Parent hic `waitpid()` cagirmazsa, bu zombi'ler birikip kaynak sizdirir. Biz `waitpid(pid, &status, WNOHANG)` kullaniyoruz -- `WNOHANG` sayesinde child henuz bitmemisse BLOKLAMADAN hemen doner, subject'in "hicbir blocking cagri yok" kuralina uyulur; hemen toplanamayanlar bir listede tutulup her dongude tekrar denenir.
- **Sinyal (signal)**: isletim sisteminin bir process'e "bir sey oldu" demesinin yolu (orn. `SIGKILL` = hemen oldur, `SIGPIPE` = "kapali bir pipe/soket'e yazmaya calistin"). Biz `SIGPIPE`'i `SIG_IGN` ile BASTAN yok sayiyoruz -- yoksa bir CGI erken cikip pipe'ini kapattiginda, bir sonraki `write()` bizim TUM sunucumuzu (varsayilan SIGPIPE davranisiyla) oldururdu.

### Keep-alive ve pipelining

- **Keep-alive**: HTTP/1.0'da her istek icin ayri bir TCP baglantisi acilip kapaniyordu -- handshake+yavas-baslangic maliyeti her seferinde tekrarlaniyordu. HTTP/1.1 ile, bir yanit bittikten sonra baglanti ACIK BIRAKILIR, ayni baglanti uzerinden bir sonraki istek de gonderilebilir -- `Connection: close` gelmedikce.
- **Pipelining**: keep-alive'in bir adim ilerisi -- client, ILK istegin YANITINI BEKLEMEDEN, ayni baglantiya IKINCI bir istegi de hemen ardindan gonderebilir. Sunucu bunlari SIRAYLA isleyip sirayla yanitlamalidir. Bizim projede bu, `poll()`'un zaten okunmus (buffer'da bekleyen) baytlari tekrar bildirmeyecegi gercegiyle basa cikilarak saglaniyor: bir yanit yazimi bitince, buffer'da tam bir sonraki istek varsa hemen islenir.

### Chunked transfer-encoding nedir

Normalde bir HTTP govdesinin uzunlugu `Content-Length` header'iyla ONCEDEN bildirilir. Ama govde DINAMIK uretiliyorsa (orn. bir CGI script'i calisirken govdeyi parca parca uretiyorsa), uzunluk ONCEDEN bilinemez. **Chunked transfer-encoding**, govdeyi "parca (chunk) + parca + ... + bitis isareti" seklinde gonderme yontemidir: her parca, kendi uzunlugunu (HEX olarak) bir satirda soyleyip ardindan o kadar bayt verir; uzunlugu `0` olan bir "parca", govdenin bittigini belirtir. Sunucu bunu **un-chunk** ederek (parcalari birlestirip) normal bir govdeye cevirmelidir -- subject bunu acikca istiyor, ozellikle CGI'ye govde verirken (CGI, chunked FORMATINI degil, DUZ bir bayt akisini + EOF'u bekler).

### MIME type (Content-Type) nedir

Tarayici, aldigi govdenin bir HTML sayfa mi, bir resim mi, bir CSS dosyasi mi oldugunu, dosya UZANTISINDAN degil, `Content-Type` HEADER'INDAN anlar (orn. `text/html`, `text/css`, `image/png`). Biz bunu dosya uzantisina bakarak (`HttpUtils::contentTypeFor`) belirliyoruz ve her statik yanitta bu header'i ekliyoruz -- eklemesek tarayici HTML'i duz metin gibi gosterebilirdi.

### Virtual hosting (sanal sunucu) kavrami

Ayni IP+port'u dinleyen TEK bir fiziksel sunucunun, `Host` header'ina gore FARKLI web sitelerini (farkli `root`, farkli icerik) sunmasidir -- orn. `ornek1.com` ve `ornek2.com` ayni sunucuda, ayni portta barinabilir, hangisinin cevap verecegi istekteki `Host: ornek1.com` header'iyla belirlenir. Subject bunu ISTEMIYOR (kapsam disi sayiyor) ama yasaklamiyor da -- biz `server_name` + `Host` header eslesmesiyle ekledik.

### Status kodu kategorileri

| Kategori | Anlami |
|---|---|
| 1xx | Bilgilendirme (orn. `100 Continue` -- "govdeyi gonder, hazirim") |
| 2xx | Basari (`200 OK`, `201 Created`) |
| 3xx | Yonlendirme (`301`, `302`, ...) |
| 4xx | Client'in hatasi (`400 Bad Request`, `404 Not Found`, ...) |
| 5xx | Sunucunun hatasi (`500 Internal Server Error`, `502 Bad Gateway`, ...) |

### Bu projede isledigimiz temel saldiri/guvenlik kavramlari

- **Path traversal**: `../` ile, sunucunun izin verdigi kok dizinin (`root`) DISINA cikip sistemdeki baska dosyalara (orn. `/etc/passwd`) erismeye calisma girisimi.
- **Null-byte injection**: eski/bazi C tabanli sistemlerde bir string'in `\0` (null) bayttan sonrasini "yoksaymasindan" faydalanarak, gorunen bir dosya adinin (`resim.jpg%00.php` gibi) gercekte baska bir seye cozulmesini saglama girisimi.
- **Reflected/stored XSS (Cross-Site Scripting)**: kullanici kontrollu bir verinin (dosya adi, path), sunucunun urettigi bir HTML sayfaya HTML-ozel karakterleri (`<`, `>`, ...) ESCAPE EDILMEDEN gomulmesi -- boylece bir saldirganin `<script>` gibi bir icerigi baska bir kullanicinin tarayicisinda CALISTIRABILMESI.
- **Request smuggling**: `Content-Length` ve `Transfer-Encoding` header'lari CELISKILI verildiginde, sunucu ile ARADAK bir proxy'nin govdenin NEREDE bittigini FARKLI yorumlamasi -- bu farktan faydalanip bir istegin icine gizlice baska bir istek "kacirilabilir".
- **Slowloris**: bir istegi ASLA TAMAMLAMADAN, cok kucuk parcalar (orn. saniyede 1 bayt) gonderip baglantiyi olabildigince uzun sure MESGUL tutmaya calisan bir saldiri -- amac, sunucunun sinirli sayidaki baglanti/kaynagini bu sekilde tuketmek.

---

## Tasarim felsefesi

Proje, tek, buyuk bir `Server` sinifindan kucuk, tek-sorumluluklu modullere bilincli bir refactor'den gecti, iki kurala uyularak:

- **Utils siniflari stateless'tir.** `StringUtils`, `NetUtils`, `FsUtils`, `HttpUtils` sadece `static` metod barindirir, hic ornek yaratilmaz, cagrilar arasinda hicbir state tutulmaz. Sadece moduller arasi tekrari kaldirmak icin var (orn. `NetUtils::ipToString` eskiden uc ayri yerde kopyalanmisti).
- **Geri kalan her sey gercek bir sinif**, Orthodox Canonical Form ile (constructor, destructor, copy constructor, copy-assignment operator) — nesne gercekte hic kopyalanmasa bile, bu durumda subject'in C++98 kisitlarina uygun olarak `private` tutuluyor.

Hicbir yerde `namespace` kullanilmiyor (proje kisiti); C++'in normalde anonim namespace'e basvuracagi yerlerde dosya-kapsamli `static` kullaniliyor.

## Dizin yerlesimi

```
includes/            sources/
├── Utils/            ├── Utils/          StringUtils, NetUtils, FsUtils, HttpUtils
├── Http/              ├── Http/           HttpRequest, HttpResponseBuilder
├── Config/            ├── Config/         ConfigParser, ConfigStructs, ListenTable
├── Cgi/               ├── Cgi/            CgiProcess
└── Core/              ├── Core/           Client, Router, RequestHandler, Server, ServerCgi
                        └── main.cpp
```

`Makefile`, her alt dizin icin bir `-I` ekliyor, boylece her `#include "X.hpp"` duz dosya adi olarak kaliyor — fiziksel yerlesim, hicbir include yolunun alt dizini yazmasina gerek kalmadan organize ediliyor. Object dosyalari, ayni yerlesimi yansitarak `objs/` altina insa ediliyor, `sources/`/`includes/` build ciktilarindan temiz kaliyor.

## Modul referansi

### Utils (stateless)

| Sınıf | Sorumluluk |
|---|---|
| `StringUtils` | `toLower`/`toUpper`, `trimLeadingSpaces` (header degeri kirpma), `isNumeric` |
| `NetUtils` | `ipToString` (elle `ntohl` + bicimlendirme — `inet_ntoa` subject'in whitelist'inde yok), `resolveIPv4` (`getaddrinfo` sarmalayicisi), `setNonBlocking` (`F_GETFL`→OR→`F_SETFL` kalibi, listen socket'leri, kabul edilen client socket'leri ve CGI pipe'lari arasinda paylasilir) |
| `FsUtils` | `readWholeFile` (`open`/`read`/`close` ile, `ifstream` degil — subject'in fonksiyon whitelist'i disinda kalmasi bilincli), `hasDotDotSegment` (path traversal kontrolu), `exists`/`isDirectory`/`isRegularFile`/`isReadable` (`stat`/`access` sarmalayicilari) |
| `HttpUtils` | `statusText`, `contentTypeFor` (uzanti→MIME haritasi), `determineKeepAlive`, `splitHeaderBody` (CGI ciktisi ayristirma), `urlDecode` (null-byte/bozuk-dizi reddi ile percent-decode), `htmlEscape` (uretilen HTML icin XSS savunmasi) |

### Http

- **`HttpRequest`** — istek satiri + header parse'ini yonetir. `parseRequestLine` path'i URL-decode eder (bozuk encoding veya decode sonrasi null bayt varsa reddeder) ama query string'i KASITLI OLARAK encoded birakir, cunku CGI/1.1 `QUERY_STRING`'in ham halini bekler.
- **`HttpResponseBuilder`** — giden her yaniti kurar: `build()` (normal yanit), `buildRedirect()` (301/302/303/307/308, hem mutlak `http(s)://` hedeflerini hem ic path'leri kabul eder, config'in verdigini oldugu gibi `Location`'a yazar), `buildError()` (yapilandirilmis bir `error_page` dosyasina bakar, tanimli degilse veya dosya okunamiyorsa yerlesik, kod icinde sabit bir HTML sayfasina duser).

### Config

- **`ConfigParser`** — nginx'ten ilham alan config-dosyasi grameri. Inheritance'i parse aninda cozer (orn. bir location'in `client_max_body_size`'i, override edilmemisse server blogundan doldurulur, boylece runtime her zaman TEK bir nihai efektif deger gorur, asla `unset` degil).
- **`ConfigStructs`** — `ServerConfig` / `LocationConfig` / `ListenConfig`, duz veri siniflari.
- **`ListenTable`** — parse edilmis TUM server'lar arasinda topoloji-seviyesi dogrulama: gercek `(ip, port, server_name)` belirsizligini reddeder, ve nginx-tarzi wildcard/spesifik-IP socket merge'ini (`RealListen`) kurar: bir portta hem `0.0.0.0` hem spesifik bir IP varsa, sadece **tek** bir gercek socket acilir (wildcard); spesifik kayitlar `accept()` aninda `getsockname()` ile cozulen salt routing metadata'sina donusur.

### Cgi

- **`CgiProcess`** — tek bir CGI cagrisinin butun fork/pipe/execve yasam donguşu, bir alan yigini yerine gercek bir sinif olarak: `start()` (fork + pipe + dup2 + chdir + execve), `writeStdin()` / `readStdout()` (poll dongusunden cagrilan, birer non-blocking artis), `terminate()` (SIGKILL + kalan pipe'lari kapatma, hem 10sn timeout hem erken client-disconnect abort yollarinda kullanilir).

### Core

- **`Client`** — tek bir baglantinin butun durumu tek yerde (onceki bir iterasyondaki sekiz paralel `std::map<int, X>`'in yerini alir): tamponlar, parse edilmis `HttpRequest`, keep-alive/close bayraklari, Slowloris zamanlayicisi, ve kademeli chunked-decode durumu (`chunkParseOffset`, `chunkedBody`).
- **`Router`** — `ListenTable`'i sararak "bu `(port, local IP, Host header)` icin hangi `ServerConfig`" ve "bu path icin hangi `LocationConfig`" (en uzun-prefix eslesmesi, nginx tarzi) sorularini cevaplar.
- **`RequestHandler`** — GET/POST/DELETE dosya sistemi mantigi (statik dosya servisi, index fallback, dizin listeleme, upload yazma, silme), bir `Client&`/`LocationConfig*` uzerinde calisan stateless static metodlar.
- **`Server`** — salt orkestrasyon: tek `poll()` dongusu, `std::map<int, Client>` defter tutma, `Router`/`RequestHandler`/`CgiProcess`'e dispatch. Kendi ic is mantigi yok.

## Event loop

Tek bir `std::vector<struct pollfd>`, her dinleme soketini, her kabul edilmis client soketini ve her CGI pipe fd'sini (stdin VE stdout, ayri ayri takip edilir) tutar. Dongu basina TEK bir `poll()` cagrisi hepsini yonetir — bu, degerlendirme sirasinda acikca kontrol edilen sert bir subject gereksinimidir.

Boyunca uyulan kurallar:
- Bir socket veya pipe'ta, o fd `poll()` tarafindan HAZIR olarak bildirilmeden `read`/`write`/`recv`/`send` asla denenmez.
- `errno`, bir `read`/`write` cagrisindan SONRA davranisi degistirmek icin ASLA incelenmez — sadece donus degeri (`<= 0`, ya da kismi bir sayi) kullanilir. Kod tabanindaki diger tek iki `errno` kullanimi: `poll()`'un kendisi `-1` donunce `EINTR` (bir read/write cagrisi degil), ve socket-kurulum hata *mesajlarinda* `strerror(errno)` (davranis dallanmasi degil, tanı metni) — ikisi de subject'in yasakladigi kalip degil.
- Normal disk dosyalari (statik icerik, upload, error page icin `open`/`read`/`stat`) `poll()`'a hic girmeden senkron okunur — subject bunu acikca istisna tutuyor.
- `fork()` SADECE CGI icin kullanilir (tek cagri noktasi, `CgiProcess::start` icinde).

Gelistirme sirasinda duzeltilen, belirgin olmayan bir hata: bir pipe'in yazan ucu kapanip biriken veri tamamen tuketildiginde `poll()` sadece `POLLHUP` dondurebiliyor (`POLLIN` olmadan) — sadece `POLLIN` kontrolu CPU'yu mesgul eden bir donguye yol aciyordu; dispatch artik `POLLIN | POLLHUP | POLLERR`'i birlikte kontrol ediyor.

## HTTP/1.1 ozellik destegi

- **Keep-alive**: HTTP/1.1 `Connection: close` gelmedikce varsayilan olarak keep-alive'dir; HTTP/1.0 `Connection: keep-alive` gelmedikce varsayilan olarak close'dur.
- **Pipelining**: `poll()` bir socket'in okuma tamponunda zaten oturan baytlari tekrar bildirmeyecegi icin, sunucu bir yanit yazimi bitince, tam bir sonraki istek zaten biriktirilmisse hemen istek islemeyi tekrar tetikler.
- **Chunked transfer-encoding** (istek govdesi): **kademeli** decode edilir — `Client::chunkParseOffset`/`chunkedBody`, ham chunked akisin ne kadarinin zaten dogrulandigini hatirlar, boylece buyuk bir chunked govde her gelen TCP segmentinde bastan yeniden ayristirilmak yerine toplamda O(n) maliyete mal olur (bu duzeltmenin hikayesi icin bkz. [Onemli tasarim kararlari](#onemli-tasarim-kararlari)).
- **`Expect: 100-continue`**: bir govde beklendiginde taninir; sunucu hemen ara bir `HTTP/1.1 100 Continue` yaniti kuyruklar, nihai yanittan ayridir, ve bu ara yazmanin baglantiyi kapatmasina izin vermemeye dikkat edilir.
- **Request smuggling savunmasi**: hem `Content-Length` hem `Transfer-Encoding: chunked` varsa, chunked framing her zaman kazanir ve `Content-Length` tamamen yok sayilir (RFC 7230 §3.3.3).
- **Host header**: HTTP/1.1 istekleri icin zorunlu (yoksa 400); HTTP/1.0 icin gerekli degil.
- **Desteklenmeyen HTTP versiyonu**: `HTTP/1.0`/`HTTP/1.1` disindaki her versiyon `505` alir.

## Desteklenen HTTP status kodlari

| Kod | Anlami | Ne zaman donuluyor |
|---|---|---|
| 200 | OK | Basarili GET/CGI/ustune-yazma-upload |
| 201 | Created | *Yeni* bir dosyanin basarili upload'u |
| 301/302/303/307/308 | Yonlendirmeler | `return`/`redirect` direktifi; sondaki slash'siz dizin erisimi de bir 301 doner |
| 400 | Bad Request | Bozuk istek satiri/header'lar, HTTP/1.1'de eksik Host, gecersiz Content-Length, gecersiz chunked framing, null-byte/bozuk percent-encoding |
| 403 | Forbidden | Statik dosya var ama okunamiyor (izin reddi); POST/DELETE icin `upload_dir` tanimli degil |
| 404 | Not Found | Eslesen dosya yok; index dosyasi olmayan ve `autoindex off` olan dizin (bkz. tasarim kararlari) |
| 405 | Method Not Allowed | Method, location'in `allow_methods`'unda degil |
| 411 | Length Required | *(status tablosunda tanimli; otomatik tetiklenmiyor — bkz. tasarim kararlari)* |
| 413 | Payload Too Large | Govde, *efektif* (location-farkinda) `client_max_body_size`'i asiyor |
| 414 | URI Too Long | Istek satiri ic boyut sinirini asiyor |
| 431 | Request Header Fields Too Large | Header blogu ic boyut sinirini asiyor |
| 500 | Internal Server Error | CGI pipe/fork hatasi, upload yazma hatasi |
| 501 | Not Implemented | *(status tablosunda ayrilmis)* |
| 502 | Bad Gateway | CGI, header+govde olarak ayristirilamayan bir cikti uretti |
| 504 | Gateway Timeout | CGI calisma suresini asti |
| 505 | HTTP Version Not Supported | HTTP/1.0 veya HTTP/1.1 disindaki her versiyon |

## Config dosyasi referansi

NGINX'in `server { location { } }` yapisindan ilham alinmistir.

**Server-seviyesi direktifler**: `listen` (tekrarlanabilir), `server_name`, `error_page <kod...> <yol>` (tekrarlanabilir), `root`, `index`, `autoindex`, `client_max_body_size` (varsayilan `1M`).

**Location-seviyesi direktifler** (override edilmedikce server'in degerini miras alir): `allow_methods`, `root`, `index`, `autoindex`, `client_max_body_size`, `upload_dir` (POST/DELETE depolamasini acar), `cgi_extension <uzanti> <yorumlayici-yolu>` (tekrarlanabilir, uzanti-bazli CGI dispatch), `return <kod> <hedef>` (hedef ic bir path veya mutlak bir `http://`/`https://` URL olabilir).

`ListenTable` tarafindan baslangicta zorlanan topoloji kurallari (parse aninda reddedilir, runtime'da asla): bir server icinde tekrarlanan `(ip, port)`, ve iki farkli server'in hem ayni `(ip, port)` HEM ayni `server_name`'i paylasmasi (belirsiz — virtual hosting devrede olsa bile Host header onlari ayirt edemezdi). Tam islenmis, uc-senaryolu bir ornek icin `configs/default.conf`'a (tek-server wildcard+spesifik listen, iki-server wildcard/spesifik-IP merge, ozel error page'li tek spesifik-IP server) ve demo config'ten ayri tutulan kasitli-gecersiz/edge-case config'ler icin `configs/conflict.conf` / `configs/vhost_conflict.conf` / `configs/vhost_scenarios.conf`'a bakin.

## CGI/1.1

Cagirma: `fork()` → iki `pipe()` (stdin, stdout) → child `dup2` + `chdir(scriptDir)` + `execve(yorumlayici, [yorumlayici, script-taban-adi], envp)` yapar; parent her iki pipe ucunu da non-blocking yapar ve tek `poll()`'a kaydeder. CGI-basina 10 saniyelik bir timeout (`SIGKILL` + non-blocking `waitpid(WNOHANG)` reap donguşu, her ana dongu turunde tekrar denenir, asla blocking bir bekleme degildir) bir istegin takilmis bir script'te sonsuza kadar asilı kalamayacagini garanti eder.

### Sağlanan ortam değişkenleri

`REQUEST_METHOD`, `SCRIPT_NAME` *(kasitli olarak bos — asagiya bakin)*, `SCRIPT_FILENAME`, `PATH_INFO`, `PATH_TRANSLATED`, `REQUEST_URI`, `QUERY_STRING` (spec geregi percent-encoded birakilir), `CONTENT_LENGTH`, `CONTENT_TYPE`, `SERVER_PROTOCOL`, `SERVER_NAME`, `SERVER_PORT`, `REMOTE_ADDR`, `GATEWAY_INTERFACE`, `SERVER_SOFTWARE`, `REDIRECT_STATUS` (php-cgi bu olmadan calismayi reddeder), `PATH`, artı her istek header'i `HTTP_<AD>` olarak yeniden sunulur (tire → alt cizgi, buyuk harf).

**`SCRIPT_NAME` neden bos ve `PATH_INFO` neden tam path'i tasiyor**: RFC 3875 §4.1.13, script path'in geri kalanindan ayri olarak tanimlanmadiginda `SCRIPT_NAME`'in bos olmasina acikca izin veriyor — bu projenin CGI eslemesi tamamen uzanti-bazli ve tum cozulmus path uzerinde calisiyor, yani raporlanacak ayri bir "script adi vs. ekstra path" siniri yok. Bu tam konvansiyon, subject'in kendi resmi `tests/cgi_tester` binary'sine dogrudan prob yapilarak (`env -i` ile elle kurulmus ortamlar besleyip hangi `SCRIPT_NAME`/`PATH_INFO` eslesmesini kabul ettigi gozlemlenerek) dogrulandi.

Chunked istek govdeleri, CGI onlari gormeden ONCE sunucu tarafindan tamamen un-chunk edilir (CGI stdin'de her zaman duz bir bayt akisi alir, parent yazma ucunu kapatinca EOF ile biter — subject'in tam istedigi sey bu). CGI ciktisi ilk bos satirda header blogu + govde olarak ayristirilir; CGI'nin kendi ciktisinda `Content-Length` yoksa, govde stdout EOF'undan once gelen her sey olur.

## Guvenlik sertlestirmesi

| Endişe | Savunma |
|---|---|
| Path traversal (`../`) | `FsUtils::hasDotDotSegment`, **decode edilmis** path uzerinde kontrol edilir |
| Null-byte injection (`%00` veya ham) | `HttpUtils::urlDecode` decode edilmis bir null bayti reddeder; ham path de dogrudan kontrol edilir |
| Reflected/stored XSS | `HttpUtils::htmlEscape`, autoindex dizin listelemesine uygulanir (hem gosterilen path hem her dosya adi — dosya adlari `readdir()`'den gelir, ki bu saldirgan tarafindan upload edilmis icerik barindirabilir) |
| Request smuggling | Transfer-Encoding, ikisi de varsa Content-Length'i her zaman gecersiz kilar (RFC 7230 §3.3.3) |
| Bozuk Content-Length | Parse edilmeden once tum-rakam oldugu dogrulanir; curuk bir deger sessizce `0`'a degil `400`'e donusur |
| Sinirsiz header/URI boyutu (bellek DoS) | `414`/`431` boyut sinirlari, asiri buyuk veri uzerinde herhangi bir parse islemi yapilmadan ONCE kontrol edilir |
| Sinirsiz chunked govde (bellek DoS) | Efektif `client_max_body_size`, sadece bitis chunk'i gelince degil, chunk'lar geldikce kademeli olarak kontrol edilir |
| Slowloris (bayt-bayt header sizdirma) | **Ayri** bir mutlak zamanlayici (`Client::requestStartTime`, sadece yeni bir istek basladiginda yenilenir, ASLA bayt-basina), idle-aktivite timeout'undan bagimsiz olarak toplam header-tamamlama suresini sinirlar |
| CGI takilmasi / zombie process'ler | 10sn CGI timeout + `SIGKILL` + non-blocking reap; `SIGPIPE` global olarak yok sayilir, boylece erken cikan bir CGI'nin broken-pipe sinyali sunucuyu asla cokertemez |
| Cokme guvenligi | Her syscall hata yolu ele alinip bir HTTP hata yanitina veya temiz bir baglanti kopmasina cevrilir — sunucu 35 bozuk/kotu-niyetli istek senaryosuyla (`tests/crash_test.py`) tek bir cokme olmadan stres testine tabi tutuldu |

## Onemli tasarim kararlari

- **Chunked decode, gercek bir O(n²) hatasi bulunduktan sonra kademeli hale getirildi.** Orijinal implementasyon, her gelen `read()` olayinda biriken chunked tamponun TAMAMINI bastan yeniden ayristiriyordu. Bu, kucuk boyutlarda gorunmezdi ama olcekte felaketti: 20MB'lik bir chunked upload **20.2 saniye** surdu; ayni 20MB bilinen bir `Content-Length` ile gonderildiginde 1.5 saniyenin altinda surdu. Bu, subject'in resmi CGI tester'i 100MB'lik bir payload ile calistirilirken kesfedildi — bu, birkac dakika suren bir donmaya yol acmisti. Duzeltmeden sonra (cagri-basina bastan degil devam eden, client-basina `chunkParseOffset`/`chunkedBody` durumu), ayni 100MB'lik chunked upload ~1.3 saniyede tamamlaniyor — dogrudan olcumle dogrulanan dogrusal olcekleme.
- **`client_max_body_size`, sadece server degil *location* seviyesinde cozuluyor.** Yukaridaki duzeltmeyle birlikte iliskili bir hata daha yakalandi: govde-boyutu kontrolu, eslesen *location*'un kendi override'i olsa bile `ServerConfig::clientMaxBodySize`'i okuyordu, location-ozel siniri sessizce yok sayiyordu. `Server::effectiveMaxBodySize()` artik once eslesen location'i cozuyor.
- **Index dosyasi olmayan ve `autoindex off` olan bir dizine erisim `403` degil `404` donuyor.** Nginx'in gercek davranisi burada `403`'tur; bu proje bilerek bunun yerine `404` donuyor, cunku `403` bir saldirgana dizinin var oldugunu dogrulniyor, `404` ise hicbir sey ifsa etmiyor — bilgi-ifsasi nedenleriyle secilmis, nginx referansindan kucuk, kasitli bir sapma.
- **IP-basina bir rate limiter (token bucket, `429 Too Many Requests`) implemente edildi, sonra kaldirildi.** Dogru calisiyordu (dogrulandi: normal trafik `200` kaliyordu, 100-token'lik bir bucket'a karsi 200-isteklik bir patlama, asan kisim icin dogru sekilde `429` donuyordu), ama subject rate limiting istemiyor, ve projeyi gercekten gerekli olana odaklamak icin kaldirildi. Zaten gercek bir DDoS korumasi da degil — gercekten dagitik bir saldiri (cok sayida kaynak IP) bir network/altyapi-katmani sorunudur, tek-process bir HTTP sunucusunun cozebilecegi bir sey degil.
- **Beklenmeyen govde tasiyan `GET`/`DELETE` istekleri reddedilmiyor, kabul ediliyor.** RFC 7231 §4.3.1, bir `GET` govdesinin tanimli bir anlami olmadigini soyluyor ama yasaklamiyor da; bu, subject'in hic istemedigi yeni bir red kurali uydurmaktansa gercek-dunya sunucu davranisina (NGINX de kabul ediyor) daha yakin.

## Test

- **`tests/crash_test.py`** — 35 senaryo (rastgele binary veri, null bayt, derin path traversal, bozuk/asiri-buyuk chunked encoding, asiri-buyuk header/URI, istek-ortasinda ani kopmalar, 150-baglantilik bir patlama, yavas bayt-bayt gonderimler, ...), her senaryoda tek bir sey dogrulaniyor: sunucu process'i hala ayakta mi.
- **`tests/feature_test.py`** — subject'in tum mandatory ozelliklerini uctan uca test eden 21 senaryo (statik dosyalar, redirect'ler, upload/ustune-yazma/silme, method kisitlamasi, govde-boyutu siniri, chunked upload, CGI GET/POST, ozel/varsayilan error page'ler, autoindex, coklu-port dinleme, wildcard/spesifik-IP virtual-host merge, keep-alive + gercek pipelining).
- **`tests/curl_tests.sh`** — gelistirme boyunca biriken, gunluk `curl` (artı `curl`'un uretemedigi protokol-seviyesi durumlar icin birkac raw-socket `nc` cagrisi) regresyon paketi.
- **`tests/tester` / `tests/cgi_tester`** — subject ile birlikte intra'da dagitilan resmi Go binary'leri, gerektirdikleri `tests/YoupiBanane/` fixture agaciyla `configs/tester_intra.conf`'a karsi calistirilir.

Bu dort paketin hepsi, istek parse'ini, routing'i, CGI'yi veya poll dongusunu etkileyen her degisiklikten sonra calistirilir.
