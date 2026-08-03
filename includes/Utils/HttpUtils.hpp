#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <string>
#include "HttpRequest.hpp"

// Stateless HTTP-protokolu yardimcilari: durum metni, content-type
// tespiti, "header blogu + govde" ayirimi (CGI ciktisi ayirmakta
// kullanilir), location/istek path sekli dogrulamasi. Server, ConfigParser
// ve CGI arasinda paylasilir.
class HttpUtils
{
public:
	static std::string	statusText(int code);
	static std::string	contentTypeFor(const std::string& path);
	// HTTP/1.1: "Connection: close" gelmedikce keep-alive varsayilan.
	// HTTP/1.0: "Connection: keep-alive" gelmedikce close varsayilan.
	static bool			determineKeepAlive(const HttpRequest& req);
	// raw'i ILK bos satira gore header blogu / govde olarak ikiye ayirir.
	// "\r\n\r\n" ve "\n\n" ikisi de kabul edilir (hangisi ONCE geliyorsa).
	static bool			splitHeaderBody(const std::string& raw, std::string& headerBlock, std::string& body);
	// Percent-encoding (%XX) coder -- '+' KASITLI OLARAK boslu"a cevrilmiyor
	// (path icin RFC 3986'da '+' literal bir karakterdir; query string CGI'ye
	// zaten HAM/encoded haliyle veriliyor, CGI/1.1 spec'i boyle ister).
	// Bozuk bir %-dizisi (2 hex hane degil) YA DA decode sonrasi bir NULL
	// bayt (path traversal/null-injection savunmasi) bulunursa false doner.
	static bool			urlDecode(const std::string& encoded, std::string& decoded);
	// & < > " ' karakterlerini HTML entity'lerine cevirir -- kullanici
	// kontrollu veri (istek path'i, upload edilmis dosya adlari) sunucunun
	// URETTIGI HTML'e (autoindex, ozel error page) gomulmeden once BURADAN
	// gecmeli (stored/reflected XSS savunmasi).
	static std::string	htmlEscape(const std::string& raw);

private:
	// Sinif hic orneklenmez (sadece static metodlar) -- OCF yine de burada,
	// private'da, hicbir zaman kullanilmayacak sekilde tanimli tutuluyor.
	HttpUtils();
	~HttpUtils();
	HttpUtils(const HttpUtils& ref);
	HttpUtils&	operator=(const HttpUtils& ref);
};

#endif
