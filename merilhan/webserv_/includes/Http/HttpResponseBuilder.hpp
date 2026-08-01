#ifndef HTTP_RESPONSE_BUILDER_HPP
#define HTTP_RESPONSE_BUILDER_HPP

#include <string>
#include "ConfigStructs.hpp"

// HTTP response string'lerini (status line + header'lar + govde) kurar.
// Server ve CGI glue kodu arasinda paylasilir. Stateless -- hic orneklenmez.
class HttpResponseBuilder
{
public:
	// statusText ACIKCA parametre olarak aliniyor (HttpUtils::statusText'ten
	// otomatik turetilmiyor) cunku bazi caller'lar (CGI ciktisindaki
	// "Status:" header'i gibi) kendi metnini getirebilir -- sadece
	// HttpUtils'in bildigi sabit kod tablosuyla sinirlanmamali.
	static std::string	build(int statusCode, const std::string& statusText, const std::string& body,
			bool keepAlive, const std::string& contentType = "text/html");
	static std::string	buildRedirect(int code, const std::string& location, bool keepAlive);
	// cfg->errorPages'te bu statusCode icin ozel bir sayfa varsa (root-relative
	// yol, cfg->root ile birlestirilir) onu okuyup doner; yoksa yerlesik
	// govde uretir. cfg NULL olabilir (henuz eslesen server yoksa).
	static std::string	buildError(int statusCode, const ServerConfig* cfg, bool keepAlive);

private:
	HttpResponseBuilder();
	~HttpResponseBuilder();
	HttpResponseBuilder(const HttpResponseBuilder& ref);
	HttpResponseBuilder&	operator=(const HttpResponseBuilder& ref);
};

#endif
