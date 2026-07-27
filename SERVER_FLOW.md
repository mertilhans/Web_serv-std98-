# Server.cpp — Akış Şeması

Bu doküman, `Server.hpp`/`Server.cpp` kapsamındaki (config parse ve CGI hariç, o kısımlar
arkadaşların tarafından yapılıyor) tüm akışı dört bölümde özetler:

1. Başlangıç (socket kurulumu)
2. Ana `poll()` döngüsü
3. Tek bir isteğin baştan sona işlenişi (request lifecycle)
4. Veri yapıları arası ilişki (mConfigs → mListenSockets → mPollFds → client map'leri)

---

## 1) Başlangıç — `main()` → `setupSockets()` → `run()`

```mermaid
flowchart TD
    A["main(): parser.parse(configPath)"] --> B["Server server(configs)<br/>mConfigs kopyalanır"]
    B --> C["server.setupSockets()"]
    C --> D{"mConfigs icinde<br/>her ServerConfig icin"}
    D -->|"ayni host:port zaten<br/>mListenSockets'te var"| E["existing->configs.push_back(&cfg)<br/>YENI SOCKET ACILMAZ"]
    D -->|"host:port ilk kez<br/>goruluyor"| F["listen_data(&cfg)"]
    F --> G["createListenSocket(host, port)"]
    G --> G1["addrInfoData() -> hints doldur"]
    G1 --> G2["getaddrinfo() -> aday adres listesi"]
    G2 --> G3["socket_and_bind():<br/>socket() + SO_REUSEADDR +<br/>fcntl(O_NONBLOCK) + bind()"]
    G3 --> G4["listen(fd, SOMAXCONN)"]
    G4 --> H["ListenSocket{fd, host, port,<br/>configs=[&cfg]} olustur"]
    H --> I["mListenSockets.push_back(...)"]
    E --> J
    I --> J{"mConfigs'te<br/>baska var mi?"}
    J -->|evet| D
    J -->|hayir| K["server.run()"]
    K --> L["addListeningSockets():<br/>her ListenSocket.fd -> mPollFds<br/>(POLLIN ile)"]
    L --> M["listeningSockets(): TEK poll() donguesu basliyor"]
```

**Önemli:** Aynı `host:port`'u paylaşan birden fazla `server{}` bloğu (virtual host) için
sadece **bir** gerçek socket açılır — `mListenSockets[j].configs` listesine ek `ServerConfig*`
eklenir. Bu, `bind()`'in aynı adrese ikinci kez çağrılıp "Address already in use" hatası
almasını da baştan engeller.

---

## 2) Ana Döngü — `listeningSockets()` (TEK `poll()` çağrısı)

```mermaid
flowchart TD
    A["for(;;) basla"] --> B["poll(&mPollFds[0], size, POLL_TIMEOUT_MS)"]
    B -->|"ready == -1"| C{"errno == EINTR?"}
    C -->|evet| A
    C -->|hayir| Z["throw runtime_error<br/>(main() yakalar, temiz cikis)"]
    B -->|"ready >= 0"| D["i = 0"]
    D --> E{"i < mPollFds.size()?"}
    E -->|hayir| A
    E -->|evet| F{"revents & POLLIN?"}
    F -->|evet| G["isPollin(i)"]
    F -->|hayir| H
    G --> G1{"dinleme soketi mi?"}
    G1 -->|evet| G2["acceptNewClient()<br/>yeni client kabul et"]
    G1 -->|hayir| G3["readClientData(fd)<br/>-> processClient(fd)"]
    G3 -->|"kapanmali (0 bayt / hata)"| G4["closeClient(i), erased=true"]
    G3 -->|"veri okundu"| G5["cevap hazirsa POLLOUT ekle"]
    G2 --> H{"erased mi?"}
    G4 --> H
    G5 --> H
    H -->|hayir + POLLOUT var| I["isPollout(i): writeClientData(fd)"]
    I -->|"tum veri yazildi / hata"| I2["closeClient(i), erased=true"]
    I -->|"kismi yazim, devam"| J
    H -->|erased=true| J
    I2 --> J
    J{"erased mi?"}
    J -->|hayir| K["isClientTimedOut(i)?<br/>(son aktiviteden 30sn+ gecti mi)"]
    K -->|evet| L["closeClient(i), erased=true"]
    K -->|hayir| M
    J -->|evet| M
    L --> M
    M{"erased mi?"}
    M -->|hayir| N["++i"]
    M -->|evet| O["i AYNI KALIR<br/>(erase sonrasi vector kaydi<br/>i. indekse kaydi, tekrar bak)"]
    N --> E
    O --> E
```

**Önemli:** `closeClient(i)` çağrıldığında `mPollFds.erase(mPollFds.begin()+i)` yapılır —
bu, `i+1`'deki elemanı `i`'ye kaydırır. Bu yüzden erase olduğunda `i` **artırılmaz**, aynı
indeks bir sonraki turda yeni elemanı gösterir — aksi halde bir eleman atlanırdı.

---

## 3) Tek Bir İsteğin Yaşam Döngüsü (client bağlandıktan cevap gönderilene kadar)

```mermaid
flowchart TD
    A["acceptNewClient:<br/>accept() + O_NONBLOCK +<br/>mClientListenSockets[fd]=ls +<br/>mClientLastActivity[fd]=now +<br/>mPollFds'e POLLIN ile ekle"] --> B["poll() donguleri boyunca<br/>veri geldikce readClientData(fd)"]
    B --> C["mClientReadBuffers[fd] += yeni bayt<br/>processClient(fd) cagrilir"]
    C --> D{"headersParsed?"}
    D -->|hayir| E["tryParseHeaders(fd):<br/>buffer'da \r\n\r\n var mi?"]
    E -->|hayir, eksik| F["return, bekle<br/>(bir sonraki read'i bekle)"]
    E -->|evet| G["parseRequestLine +<br/>parseHeaderLines +<br/>contentLengthCheck<br/>(Content-Length/chunked, 413 kontrolu,<br/>matchedConfig = selectServerConfig)"]
    G --> H["state.headersParsed = true"]
    D -->|evet, zaten parse edilmis| H
    H --> I{"isChunked?"}
    I -->|evet| J["tryUnchunk(buffer, decoded)"]
    J -->|CHUNK_INVALID| K["sendErrorAndCleanup(400)"]
    J -->|CHUNK_INCOMPLETE| F
    J -->|CHUNK_COMPLETE| L["client_max_body_size kontrolu (413)<br/>buffer = decoded"]
    I -->|hayir| M["isBodyComplete(fd)?<br/>(buffer.size >= contentLength)"]
    M -->|hayir| F
    M -->|evet| N
    L --> N["finalizeRequest(fd)"]
    N --> O["selectLocation(matchedConfig, path)<br/>en uzun prefix eslemesi"]
    O -->|bulunamadi| P["sendErrorAndCleanup(404)"]
    O -->|bulundu| Q{"loc->redirectTarget<br/>bos degil mi?"}
    Q -->|evet| R["sendResponseAndCleanup(<br/>buildRedirect(...)) -> 301"]
    Q -->|hayir| S{"isMethodAllowed?"}
    S -->|hayir| T["sendErrorAndCleanup(405)"]
    S -->|evet| U["controlMethod(state, loc, fd)"]
    U --> V{"method"}
    V -->|GET| W["getHandle:<br/>resolveFilePath, stat/isDir,<br/>403(izin)/index/autoindex/404,<br/>basariliysa 200"]
    V -->|POST| X["postHandle:<br/>resolveUploadPath, 400/403,<br/>writeUploadFile -> 200/201/500"]
    V -->|DELETE| Y["deleteHandle:<br/>resolveUploadPath, 404,<br/>remove() -> 200/500"]
    W --> AA["mClientWriteBuffers[fd] = response<br/>(sendResponseAndCleanup /<br/>sendErrorAndCleanup icinde)"]
    X --> AA
    Y --> AA
    K --> AA
    P --> AA
    R --> AA
    T --> AA
    AA --> AB["bir sonraki poll() turunda<br/>POLLOUT set edilir (isPollin sonunda)"]
    AB --> AC["isPollout(i) -> writeClientData(fd)<br/>write() ile client'a gonderilir"]
    AC -->|"tum veri yazildi"| AD["closeClient(i)<br/>(Connection: close, HTTP/1.1<br/>ama biz her istekte kapatiyoruz)"]
```

---

## 4) Veri Yapıları Arası İlişki

```mermaid
flowchart LR
    subgraph Config["Config dosyasi -> ServerConfig"]
        MC["mConfigs: vector&lt;ServerConfig&gt;<br/>(her server{} bloğu icin 1 eleman)"]
    end

    subgraph Listen["Fiziksel socket'ler"]
        MLS["mListenSockets: vector&lt;ListenSocket&gt;<br/>(benzersiz host:port basina 1 eleman)"]
    end

    subgraph Poll["poll() listesi"]
        MPF["mPollFds: vector&lt;pollfd&gt;<br/>(dinleme fd'leri + tum aktif client fd'leri)"]
    end

    subgraph ClientMaps["Client basina durum (fd -> ...)"]
        CRB["mClientReadBuffers[fd]"]
        CWB["mClientWriteBuffers[fd]"]
        CS["mClientStates[fd]: ClientRequestState<br/>(headersParsed, request, contentLength,<br/>isChunked, matchedConfig)"]
        CLS["mClientListenSockets[fd] -> ListenSocket*"]
        CLA["mClientLastActivity[fd]: time_t"]
    end

    MC -->|"setupSockets() gruplar"| MLS
    MLS -->|"addListeningSockets()"| MPF
    MLS -->|"acceptNewClient() ile<br/>her yeni client'a atanir"| CLS
    MPF -->|"acceptNewClient()<br/>ile client fd eklenir"| CRB
    CLS -->|"selectServerConfig()<br/>Host header ile secim"| MC
```

---

## Özet — Fonksiyon Katmanları

| Katman | Fonksiyonlar |
|---|---|
| Socket kurulumu | `addrInfoData`, `socket_and_bind`, `createListenSocket`, `listen_data`, `setupSockets`, `addListeningSockets` |
| Ana döngü | `run`, `listeningSockets`, `isPollin`, `isPollout`, `isClientTimedOut`, `closeClient` |
| Client I/O | `acceptNewClient`, `readClientData`, `writeClientData` |
| Request parse | `tryParseHeaders`, `parseRequestLine`, `parseHeaderLines`, `contentLengthCheck`, `isBodyComplete`, `tryUnchunk`, `processClient` |
| Routing | `selectServerConfig`, `selectLocation`, `isPathPrefixMatch`, `isMethodAllowed` |
| Path güvenliği | `hasDotDotSegment`, `joinPath`, `resolveFilePath`, `resolveUploadPath` |
| Method handler'lar | `finalizeRequest`, `controlMethod`, `getHandle`, `postHandle`, `deleteHandle` |
| Dosya I/O | `serveStaticFile`, `writeUploadFile`, `buildAutoindex`, `readWholeFile` |
| Response üretimi | `buildResponse`, `buildRedirect`, `buildErrorResponse`, `getContentType`, `statusTextFor`, `sendResponseAndCleanup`, `sendErrorAndCleanup` |
