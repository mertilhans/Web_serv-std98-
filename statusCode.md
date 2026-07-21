# HTTP Status Codes Rehberi

HTTP status code'ları, bir HTTP sunucusunun istemciye (client) verdiği cevabın durumunu belirtir.

5 ana gruba ayrılır:

| Aralık | Anlam                   |
| ------ | ----------------------- |
| 1xx    | Bilgilendirme           |
| 2xx    | Başarılı işlemler       |
| 3xx    | Yönlendirmeler          |
| 4xx    | Client kaynaklı hatalar |
| 5xx    | Server kaynaklı hatalar |

---

# 1xx - Informational (Bilgilendirme)

| Kod | İsim                | Ne zaman kullanılır?                                                                              |
| --- | ------------------- | ------------------------------------------------------------------------------------------------- |
| 100 | Continue            | Client'ın request göndermeye devam edebileceğini belirtir. Büyük body gönderimlerinde kullanılır. |
| 101 | Switching Protocols | Protokol değişimi yapılırken kullanılır. Örneğin HTTP → WebSocket geçişi.                         |

---

# 2xx - Success (Başarılı)

| Kod | İsim       | Ne zaman kullanılır?                   | Örnek                          |
| --- | ---------- | -------------------------------------- | ------------------------------ |
| 200 | OK         | İstek başarıyla tamamlandı.            | GET ile HTML sayfası döndürmek |
| 201 | Created    | Yeni bir kaynak oluşturuldu.           | POST ile kullanıcı oluşturma   |
| 202 | Accepted   | İstek alındı ancak işlem devam ediyor. | Kuyruğa alınmış işlem          |
| 204 | No Content | İşlem başarılı ama cevap gövdesi yok.  | Başarılı DELETE                |

---

## 200 OK

En yaygın kullanılan status code'dur.

Örnek:

```
GET /index.html

HTTP/1.1 200 OK
```

Sunucu istenen kaynağı başarıyla döndürür.

---

## 201 Created

Yeni bir kaynak oluşturulduğunda kullanılır.

Örnek:

```
POST /users

HTTP/1.1 201 Created
```

Yeni kullanıcı oluşturuldu.

---

# 3xx - Redirection (Yönlendirme)

| Kod | İsim               | Ne zaman kullanılır?                     |
| --- | ------------------ | ---------------------------------------- |
| 301 | Moved Permanently  | URL kalıcı olarak değişti.               |
| 302 | Found              | Geçici yönlendirme yapılır.              |
| 303 | See Other          | Client başka bir URL'ye yönlendirilir.   |
| 304 | Not Modified       | Cache kullanılması gerektiğini belirtir. |
| 307 | Temporary Redirect | Geçici yönlendirme, HTTP method korunur. |
| 308 | Permanent Redirect | Kalıcı yönlendirme, HTTP method korunur. |

---

## 301 Moved Permanently

Kaynağın adresi kalıcı olarak değişmiştir.

Örnek:

```
GET http://example.com
```

Cevap:

```
HTTP/1.1 301 Moved Permanently

Location: https://example.com
```

Client artık yeni URL'yi kullanır.

Kullanım alanları:

* HTTP → HTTPS geçişi
* URL değiştirme
* Domain değişimi

---

## 302 Found

Geçici yönlendirme.

Örnek:

```
/maintenance

↓

/temporary-page
```

---

## 304 Not Modified

Client'ın elindeki cache günceldir.

Sunucu tekrar dosyayı göndermek zorunda kalmaz.

---

# 4xx - Client Errors (İstemci Hataları)

| Kod | İsim                   | Ne zaman kullanılır?           |
| --- | ---------------------- | ------------------------------ |
| 400 | Bad Request            | Request formatı bozuk          |
| 401 | Unauthorized           | Kimlik doğrulama gerekli       |
| 403 | Forbidden              | Yetki yok                      |
| 404 | Not Found              | Kaynak bulunamadı              |
| 405 | Method Not Allowed     | HTTP method desteklenmiyor     |
| 406 | Not Acceptable         | İstenen format desteklenmiyor  |
| 408 | Request Timeout        | Client zamanında cevap vermedi |
| 409 | Conflict               | Veri çakışması                 |
| 413 | Payload Too Large      | Request body çok büyük         |
| 414 | URI Too Long           | URL çok uzun                   |
| 415 | Unsupported Media Type | Desteklenmeyen dosya tipi      |
| 429 | Too Many Requests      | Çok fazla istek gönderildi     |

---

## 400 Bad Request

Client geçersiz bir request gönderdi.

Örnek:

```
GET / HTTP/1.1
```

Ama gerekli header eksik:

```
Host:
```

Cevap:

```
400 Bad Request
```

---

## 403 Forbidden

Kaynak var ancak erişim yasak.

Örnek:

```
GET /private/file.txt
```

Dosya mevcut ama kullanıcı erişemez.

Cevap:

```
403 Forbidden
```

---

## 404 Not Found

İstenen kaynak bulunamadı.

Örnek:

```
GET /missing.html
```

Dosya yok:

```
404 Not Found
```

---

## 405 Method Not Allowed

İstenen HTTP method desteklenmiyor.

Örnek:

Server sadece GET destekliyor:

```
POST /index.html
```

Cevap:

```
405 Method Not Allowed

Allow: GET
```

---

## 413 Payload Too Large

Request body çok büyük.

Örnek:

```
POST /upload

10GB file
```

Server limiti:

```
100MB
```

Cevap:

```
413 Payload Too Large
```

---

# 5xx - Server Errors (Sunucu Hataları)

| Kod | İsim                  | Ne zaman kullanılır?                |
| --- | --------------------- | ----------------------------------- |
| 500 | Internal Server Error | Beklenmeyen server hatası           |
| 501 | Not Implemented       | Özellik desteklenmiyor              |
| 502 | Bad Gateway           | Proxy yanlış cevap aldı             |
| 503 | Service Unavailable   | Server geçici olarak kullanılamıyor |
| 504 | Gateway Timeout       | Başka server cevap vermedi          |

---

## 500 Internal Server Error

Server içinde beklenmeyen hata oluştu.

Örnek:

* Kod hatası
* Memory problemi
* Exception yakalanmaması

---

## 501 Not Implemented

Server bu özelliği desteklemiyor.

Örnek:

```
TRACE / HTTP/1.1
```

Ama server TRACE desteklemiyor.

Cevap:

```
501 Not Implemented
```

---

## 503 Service Unavailable

Server geçici olarak hizmet veremiyor.

Örnek:

* Bakım
* Aşırı yük

---

# Basit HTTP Server İçin Karar Tablosu

| Durum                   | Status Code               |
| ----------------------- | ------------------------- |
| Dosya bulundu           | 200 OK                    |
| Yeni kaynak oluşturuldu | 201 Created               |
| Dosya yok               | 404 Not Found             |
| Yetki yok               | 403 Forbidden             |
| Method yanlış           | 405 Method Not Allowed    |
| Request bozuk           | 400 Bad Request           |
| Body çok büyük          | 413 Payload Too Large     |
| URL değişti             | 301 Moved Permanently     |
| Özellik desteklenmiyor  | 501 Not Implemented       |
| Server hatası           | 500 Internal Server Error |

---

# HTTP Server Akış Mantığı

```
Request geldi
      |
      v
Request parse edilebildi mi?
      |
      +---- Hayır --> 400 Bad Request
      |
      v
HTTP method destekleniyor mu?
      |
      +---- Hayır --> 405 Method Not Allowed
      |
      v
Kaynak mevcut mu?
      |
      +---- Hayır --> 404 Not Found
      |
      v
Yetki var mı?
      |
      +---- Hayır --> 403 Forbidden
      |
      v
İşlem başarılı
      |
      v
200 OK
```

---

# C++ HTTP Server İçin En Çok Kullanılan Kodlar

Bir C++ HTTP server implementasyonunda genellikle:

* `200 OK`
* `201 Created`
* `301 Moved Permanently`
* `400 Bad Request`
* `403 Forbidden`
* `404 Not Found`
* `405 Method Not Allowed`
* `413 Payload Too Large`
* `500 Internal Server Error`
* `501 Not Implemented`

status code'ları yeterli olur.
