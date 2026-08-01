#include "HttpRequest.hpp"
#include "HttpUtils.hpp"
#include "StringUtils.hpp"
#include "FsUtils.hpp"
#include <sstream>

HttpRequest::HttpRequest()
	: _method(""), _path(""), _query(""), _version(""), _headers()
{
}

HttpRequest::~HttpRequest()
{
}

HttpRequest::HttpRequest(const HttpRequest& ref)
	: _method(ref._method), _path(ref._path), _query(ref._query),
	_version(ref._version), _headers(ref._headers)
{
}

HttpRequest&	HttpRequest::operator=(const HttpRequest& ref)
{
	if (this != &ref)
	{
		_method = ref._method;
		_path = ref._path;
		_query = ref._query;
		_version = ref._version;
		_headers = ref._headers;
	}
	return (*this);
}

bool	HttpRequest::parseRequestLine(const std::string& line)
{
	std::istringstream	iss(line);
	std::string			rawTarget;

	if (!(iss >> _method >> rawTarget >> _version))
		return (false);

	std::string	extra;
	if (iss >> extra)
		return (false);

	size_t	qpos = rawTarget.find('?');
	std::string	rawPath;

	if (qpos != std::string::npos)
	{
		// Query string KASITLI OLARAK decode edilmiyor -- CGI/1.1 spec'i
		// QUERY_STRING'in ham/encoded haliyle CGI'ye verilmesini ister.
		_query = rawTarget.substr(qpos + 1);
		rawPath = rawTarget.substr(0, qpos);
	}
	else
	{
		_query = "";
		rawPath = rawTarget;
	}

	// path ise dosya sistemi erisiminde kullanilacagi icin decode ediliyor
	// (orn. "%20" -> ' '); bozuk bir encoding ya da decode sonrasi bir NULL
	// bayt (null-byte injection) varsa istek reddediliyor.
	if (!HttpUtils::urlDecode(rawPath, _path))
		return (false);
	if (_path.find('\0') != std::string::npos)
		return (false);

	return (true);
}

bool	HttpRequest::parseHeaderLines(const std::string& headerBlock)
{
	size_t	lineStart = headerBlock.find("\r\n");

	if (lineStart == std::string::npos)
		return (true);
	lineStart += 2;

	while (lineStart < headerBlock.size())
	{
		size_t	lineEnd = headerBlock.find("\r\n", lineStart);
		if (lineEnd == std::string::npos)
			lineEnd = headerBlock.size();

		std::string	line = headerBlock.substr(lineStart, lineEnd - lineStart);

		size_t	colon = line.find(':');
		if (colon == std::string::npos)
			return (false);

		std::string	name = line.substr(0, colon);
		if (name.empty() || name.find_first_of(" \t") != std::string::npos)
			return (false);

		std::string	value = StringUtils::trimLeadingSpaces(line.substr(colon + 1));

		_headers[name] = value;

		lineStart = lineEnd + 2;
	}

	return (true);
}

const std::string&	HttpRequest::getMethod() const
{
	return (_method);
}

const std::string&	HttpRequest::getPath() const
{
	return (_path);
}

const std::string&	HttpRequest::getQuery() const
{
	return (_query);
}

const std::string&	HttpRequest::getVersion() const
{
	return (_version);
}

const std::map<std::string, std::string>&	HttpRequest::getHeaders() const
{
	return (_headers);
}

std::string	HttpRequest::getHeader(const std::string& name) const
{
	std::map<std::string, std::string>::const_iterator	it = _headers.find(name);

	if (it == _headers.end())
		return ("");
	return (it->second);
}
