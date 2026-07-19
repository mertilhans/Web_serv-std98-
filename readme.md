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
