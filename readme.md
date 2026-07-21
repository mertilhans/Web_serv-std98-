HTTP Request
      |
      v
1. Server seçimi
      |
      v
2. Location seçimi
      |
      v
3. Request türünü kontrol et (GET, POST, DELETE...)
      |
      v
4. Path çözümleme (gerçek dosya yolu)
      |
      v
5. Gerekli işlemi yap
      |
      +--> Dosya gönder (GET)
      |
      +--> Veri al/kaydet (POST)
      |
      +--> Dosya sil (DELETE)
      |
      +--> CGI çalıştır
      |
      v
6. HTTP Response oluştur
      |
      v
7. Client'a gönder




Webserv

main.cpp
 |
 |
ServerManager
 |
 |
+----------------+
| ConfigParser   |
+----------------+

+----------------+
| SocketManager  |
+----------------+

+----------------+
| PollManager    |
+----------------+

+----------------+
| Client         |
+----------------+

+----------------+
| HttpRequest    |
+----------------+

+----------------+
| Router         |
|                |
| matchServer()  |
| matchLocation()|
+----------------+

+----------------+
| Handler        |
|                |
| GET            |
| POST           |
| DELETE         |
| CGI            |
+----------------+

+----------------+
| Response       |
+----------------+



# Webserver — İstek İşleme Akışı

CGI ayrımı, method dispatch (GET / POST / DELETE), 404 / 405 / 413 kontrolleri ve CGI hata/timeout yolları dahil.

```mermaid
flowchart TD
    START["requested resource exists"]
    ERR404["404"]
    CGIQ{"cgi?"}
    TYPE1{"type"}
    VALIDF{"valid"}
    RUNCGI1["Run cgi with METHOD on the file"]
    IDX1{"index"}
    VALIDD{"valid"}
    RUNCGI2["Run cgi with METHOD on the file"]
    EXC1["timeout → 504 / hata → 500"]
    EXC2["timeout → 504 / hata → 500"]
    MERGE(( ))
    METHOD{"method"}
    ERR405["405"]

    START -- "false (bulunamadı)" --> ERR404
    START -- "true" --> CGIQ
    CGIQ -- "true" --> TYPE1
    CGIQ -- "false" --> MERGE
    TYPE1 -- "file" --> VALIDF
    TYPE1 -- "directory" --> IDX1
    VALIDF -- "true" --> RUNCGI1
    VALIDF -- "false" --> MERGE
    IDX1 -- "true" --> VALIDD
    IDX1 -- "false" --> MERGE
    VALIDD -- "true" --> RUNCGI2
    VALIDD -- "false" --> MERGE
    RUNCGI1 -.-> EXC1
    RUNCGI2 -.-> EXC2
    MERGE --> METHOD

    METHOD -- "other" --> ERR405

    %% ---- POST branch ----
    UPLOADNOTE["location supports upload?"]
    UPLOAD{"upload"}
    ERR403A["403"]
    SIZEOK{"body ≤ max size?"}
    ERR413["413"]
    UPLOADFILE["upload file"]

    METHOD -- "POST" --> UPLOAD
    UPLOAD -- "false" --> ERR403A
    UPLOAD -- "true" --> SIZEOK
    SIZEOK -- "false" --> ERR413
    SIZEOK -- "true" --> UPLOADFILE

    %% ---- DELETE branch ----
    DELTYPE{"type"}
    DELETEFILE["delete file"]
    URIDEL{"URI ends with '/'?"}
    ERR409["409"]
    WACCESS{"got write access?"}
    ERR403B["403"]
    TRYDEL{{"try delete the dir"}}
    ERR500["500"]
    OK204["204"]

    METHOD -- "DELETE" --> DELTYPE
    DELTYPE -- "file" --> DELETEFILE
    DELTYPE -- "dir" --> URIDEL
    URIDEL -- "false" --> ERR409
    URIDEL -- "true" --> WACCESS
    WACCESS -- "false" --> ERR403B
    WACCESS -- "true" --> TRYDEL
    TRYDEL -- "Error" --> ERR500
    TRYDEL -- "Success" --> OK204

    %% ---- GET branch ----
    GETTYPE{"type"}
    RETURNFILE["return requested file"]
    URIGET{"URI ends with '/'?"}
    ERR301["301"]
    IDXGET{"index?"}
    RETURNIDX["return index file"]
    AUTOIDX{"auto-index?"}
    ERR403C["403"]
    RETURNAUTO["return auto-index of directory"]

    METHOD -- "GET" --> GETTYPE
    GETTYPE -- "file" --> RETURNFILE
    GETTYPE -- "dir" --> URIGET
    URIGET -- "false" --> ERR301
    URIGET -- "true" --> IDXGET
    IDXGET -- "true" --> RETURNIDX
    IDXGET -- "false" --> AUTOIDX
    AUTOIDX -- "off" --> ERR403C
    AUTOIDX -- "on" --> RETURNAUTO

    classDef ok fill:#f2fbf4,stroke:#16a34a,stroke-width:1.5px;
    classDef err fill:#fdf3f3,stroke:#dc2626,stroke-width:1.5px;
    classDef exc fill:#fffaf0,stroke:#b45309,stroke-width:1.5px,stroke-dasharray:4 3;
    classDef neutral fill:#ffffff,stroke:#1f2937,stroke-width:1.5px;

    class START neutral
    class ERR404,ERR405,ERR403A,ERR413,ERR409,ERR403B,ERR500,ERR301,ERR403C err
    class RUNCGI1,RUNCGI2,UPLOADFILE,DELETEFILE,OK204,RETURNFILE,RETURNIDX,RETURNAUTO ok
    class EXC1,EXC2 exc
```

## Ek kontroller (bu akışta ele alınan)

| Kod | Ne zaman |
|---|---|
| **404** | İstenen kaynak hiç bulunamazsa |
| **405** | Location, gelen method'a (GET/POST/DELETE dışı) izin vermiyorsa |
| **413** | POST body'si, `client_max_body_size` limitini aşarsa |
| **500 / 504** | CGI script hata verirse / zaman aşımına uğrarsa |
| **409** | DELETE edilecek dizin URI'si `/` ile bitmiyorsa |
| **403** | Upload/delete/autoindex için yazma veya erişim izni yoksa |
| **301** | Dizin isteği `/` ile bitmiyorsa (redirect) |

> Not: GitHub, GitLab, Obsidian, VS Code gibi Mermaid destekleyen ortamlarda bu blok otomatik olarak görsel bir akış şeması şeklinde render edilir.