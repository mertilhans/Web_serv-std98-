#ifndef HTTP_RESPONSE_BUILDER_HPP
#define HTTP_RESPONSE_BUILDER_HPP

#include <string>
#include "ConfigStructs.hpp"

class HttpResponseBuilder
{
public:
	static std::string	build(int statusCode, const std::string& statusText, const std::string& body,
			bool keepAlive, const std::string& contentType = "text/html");
	static std::string	buildRedirect(int code, const std::string& location, bool keepAlive);
	static std::string	buildError(int statusCode, const ServerConfig* cfg, bool keepAlive);

private:
	HttpResponseBuilder();
	~HttpResponseBuilder();
	HttpResponseBuilder(const HttpResponseBuilder& ref);
	HttpResponseBuilder&	operator=(const HttpResponseBuilder& ref);
};

#endif
