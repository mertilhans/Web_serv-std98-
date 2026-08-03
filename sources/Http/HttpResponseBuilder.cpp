#include "HttpResponseBuilder.hpp"
#include "HttpUtils.hpp"
#include "FsUtils.hpp"
#include <sstream>

HttpResponseBuilder::HttpResponseBuilder()
{
}

HttpResponseBuilder::~HttpResponseBuilder()
{
}

HttpResponseBuilder::HttpResponseBuilder(const HttpResponseBuilder& ref)
{
	(void)ref;
}

HttpResponseBuilder&	HttpResponseBuilder::operator=(const HttpResponseBuilder& ref)
{
	(void)ref;
	return (*this);
}

std::string	HttpResponseBuilder::build(int statusCode, const std::string& statusText,
		const std::string& body, bool keepAlive, const std::string& contentType)
{
	std::ostringstream	response;

	response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
		<< "Content-Length: " << body.size() << "\r\n"
		<< "Content-Type: " << contentType << "\r\n"
		<< "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n"
		<< "\r\n"
		<< body;
	return (response.str());
}

std::string	HttpResponseBuilder::buildRedirect(int code, const std::string& location, bool keepAlive)
{
	std::ostringstream	redirect;

	redirect << "HTTP/1.1 " << code << " " << HttpUtils::statusText(code) << "\r\n"
		<< "Location: " << location << "\r\n"
		<< "Content-Length: 0\r\n"
		<< "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n"
		<< "\r\n";
	return (redirect.str());
}

std::string	HttpResponseBuilder::buildError(int statusCode, const ServerConfig* cfg, bool keepAlive)
{
	std::string	statusText = HttpUtils::statusText(statusCode);

	if (cfg)
	{
		std::map<int, std::string>::const_iterator	it = cfg->errorPages.find(statusCode);
		if (it != cfg->errorPages.end())
		{
			std::string	relative = it->second;
			if (!relative.empty() && relative[0] == '/')
				relative = relative.substr(1);

			std::string	fullPath = cfg->root;
			if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
				fullPath += "/";
			fullPath += relative;

			std::string	content;
			if (FsUtils::readWholeFile(fullPath, content))
				return (build(statusCode, statusText, content, keepAlive));
		}
	}

	std::ostringstream	body;
	body << "<html><body><h1>" << statusCode << " " << statusText << "</h1></body></html>";

	return (build(statusCode, statusText, body.str(), keepAlive));
}
