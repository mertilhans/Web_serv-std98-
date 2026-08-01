#include "HttpUtils.hpp"
#include <map>
#include <cctype>

struct StatusEntry
{
	int         code;
	const char  *text;
};

static const StatusEntry	kStatusTable[] =
{
	{ 200, "OK" },
	{ 201, "Created" },
	{ 301, "Moved Permanently" },
	{ 302, "Found" },
	{ 303, "See Other" },
	{ 307, "Temporary Redirect" },
	{ 308, "Permanent Redirect" },
	{ 400, "Bad Request" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 411, "Length Required" },
	{ 413, "Payload Too Large" },
	{ 414, "URI Too Long" },
	{ 431, "Request Header Fields Too Large" },
	{ 500, "Internal Server Error" },
	{ 501, "Not Implemented" },
	{ 502, "Bad Gateway" },
	{ 504, "Gateway Timeout" },
	{ 505, "HTTP Version Not Supported" }
};

static std::map<std::string, std::string>	buildMimeTypes()
{
	std::map<std::string, std::string>	mimeType;

	mimeType["html"] = "text/html";
	mimeType["htm"]  = "text/html";
	mimeType["css"]  = "text/css";
	mimeType["js"]   = "application/javascript";
	mimeType["txt"]  = "text/plain";
	mimeType["json"] = "application/json";
	mimeType["png"]  = "image/png";
	mimeType["jpg"]  = "image/jpeg";
	mimeType["jpeg"] = "image/jpeg";
	mimeType["gif"]  = "image/gif";
	mimeType["svg"]  = "image/svg+xml";
	mimeType["ico"]  = "image/x-icon";
	mimeType["pdf"]  = "application/pdf";
	return (mimeType);
}

// OCF -- private, hic cagrilmaz (sinif sadece static metodlarla kullanilir).
HttpUtils::HttpUtils()
{
}

HttpUtils::~HttpUtils()
{
}

HttpUtils::HttpUtils(const HttpUtils& ref)
{
	(void)ref;
}

HttpUtils&	HttpUtils::operator=(const HttpUtils& ref)
{
	(void)ref;
	return (*this);
}

std::string	HttpUtils::statusText(int code)
{
	size_t	count = sizeof(kStatusTable) / sizeof(kStatusTable[0]);

	for (size_t i = 0; i < count; i++)
	{
		if (kStatusTable[i].code == code)
			return (kStatusTable[i].text);
	}
	return ("Error");
}

std::string	HttpUtils::contentTypeFor(const std::string& path)
{
	static const std::map<std::string, std::string>	mimeTypes = buildMimeTypes();

	size_t	dot = path.find_last_of('.');
	if (dot == std::string::npos)
		return ("application/octet-stream");

	std::string	ext = path.substr(dot + 1);
	std::map<std::string, std::string>::const_iterator	it = mimeTypes.find(ext);
	if (it != mimeTypes.end())
		return (it->second);
	return ("application/octet-stream");
}

static int	hexDigitValue(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 10);
	return (-1);
}

bool	HttpUtils::urlDecode(const std::string& encoded, std::string& decoded)
{
	decoded.clear();
	decoded.reserve(encoded.size());

	for (size_t i = 0; i < encoded.size(); ++i)
	{
		char	ch = encoded[i];

		if (ch != '%')
		{
			decoded += ch;
			continue;
		}

		// "%" son iki hane olmadan bitiyorsa ya da hex olmayan karakter
		// iceriyorsa bozuk bir encoding -- reddediyoruz.
		if (i + 2 >= encoded.size())
			return (false);

		int	hi = hexDigitValue(encoded[i + 1]);
		int	lo = hexDigitValue(encoded[i + 2]);
		if (hi < 0 || lo < 0)
			return (false);

		char	decodedChar = static_cast<char>((hi << 4) | lo);
		if (decodedChar == '\0')
			return (false); // null-byte injection savunmasi

		decoded += decodedChar;
		i += 2;
	}
	return (true);
}

std::string	HttpUtils::htmlEscape(const std::string& raw)
{
	std::string	escaped;

	escaped.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); ++i)
	{
		switch (raw[i])
		{
			case '&':	escaped += "&amp;"; break;
			case '<':	escaped += "&lt;"; break;
			case '>':	escaped += "&gt;"; break;
			case '"':	escaped += "&quot;"; break;
			case '\'':	escaped += "&#39;"; break;
			default:	escaped += raw[i];
		}
	}
	return (escaped);
}

bool	HttpUtils::determineKeepAlive(const HttpRequest& req)
{
	std::string	connVal = req.getHeader("Connection");

	if (req.getVersion() == "HTTP/1.1")
		return (connVal != "close" && connVal != "Close");
	return (connVal == "keep-alive" || connVal == "Keep-Alive");
}

bool	HttpUtils::splitHeaderBody(const std::string& raw, std::string& headerBlock, std::string& body)
{
	size_t	crlf = raw.find("\r\n\r\n");
	size_t	lf = raw.find("\n\n");
	size_t	sep;
	size_t	sepLen;

	if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf))
	{
		sep = crlf;
		sepLen = 4;
	}
	else
	{
		sep = lf;
		sepLen = 2;
	}
	if (sep == std::string::npos)
		return (false);

	headerBlock = raw.substr(0, sep);
	body = raw.substr(sep + sepLen);
	return (true);
}
