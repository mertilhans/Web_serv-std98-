#include "ConfigParser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <netdb.h>
#include <netinet/in.h>	// sockaddr_in, INADDR_ANY
#include <arpa/inet.h>	// htons
#include <sys/socket.h>
#include <limits.h> // PATH_MAX, NAME_MAX (POSIX, C++'ta karsiligi yok)

using namespace raw;

// OCF
ConfigParser::ConfigParser()
	: _pos(0)
{
}

ConfigParser::ConfigParser(const ConfigParser& ref)
{
	(void)ref;
}

ConfigParser&	ConfigParser::operator=(const ConfigParser& ref)
{
	(void)ref;
	return (*this);
}

ConfigParser::~ConfigParser()
{
}

//Private members
// Validation
bool	ConfigParser::_isValidDirPath(const std::string& path)
{
	if (path.empty()
		|| path.length() >= static_cast<size_t>(PATH_MAX))
		return (false);
	if (path.find('\0') != std::string::npos)
		return (false);

	bool	isAbsolute = (path[0] == '/');
	bool	isRelCurrent = (path.compare(0, 2, "./") == 0);
	bool	isRelParent = (path.compare(0, 3, "../") == 0);

	if (!isAbsolute && !isRelCurrent && !isRelParent)
		return (false);
	return (true);
}

bool	ConfigParser::_isValidFilename(const std::string& filename)
{
	if (filename.empty()
		|| filename.length() > static_cast<size_t>(NAME_MAX))
		return (false);
	if (filename.find('\0') != std::string::npos)
		return (false);
	if (filename.find('/') != std::string::npos
		|| filename.find("..") != std::string::npos)
		return (false);
	return (true);
}

bool	ConfigParser::_isValidLocationPath(const std::string& uri)
{
	if (uri.empty() || uri[0] != '/')
		return (false);
	if (uri.find('\0') != std::string::npos)
		return (false);
	return (true);
}

bool	ConfigParser::_isValidServerName(const std::string& name)
{
	if (name.empty())
		return (true);
	for (size_t i = 0; i < name.length(); i++)
	{
		char	ch = name[i];
		if (!std::isalnum(static_cast<unsigned char>(ch))
			&& ch != '.' && ch != '-' && ch != '_')
			return (false);
	}
	return (true);
}

// Utils
// Sadece gercek/standart HTTP hata durum kodlari kabul edilir (rastgele
// 4xx/5xx sayisi degil) -- subject "default error pages" istiyor, RFC'de
// tanimli olmayan bir kod icin sayfa uretmenin anlami yok.
bool	ConfigParser::_isValidErrorPageCode(int code) const
{
	return (code == 400 || code == 401 || code == 403 || code == 404
		|| code == 405 || code == 406 || code == 408 || code == 411
		|| code == 413 || code == 414 || code == 415 || code == 429
		|| code == 431 || code == 500 || code == 501 || code == 502
		|| code == 503 || code == 504 || code == 505 || code == 507
		|| code == 508);
}

size_t	ConfigParser::_parseSizeValue(const std::string& raw)
{
	if (raw.empty())
		throw std::runtime_error("Config: empty size value");
	char	unit = raw[raw.size() - 1];
	size_t	mult = 1;

	if (unit == 'K' || unit == 'k')
		mult = 1024UL;
	else if (unit == 'M' || unit == 'm')
		mult = 1024UL * 1024UL;
	else if (unit == 'G' || unit == 'g')
		mult = 1024UL * 1024UL * 1024UL;

	std::string	numPart = (mult > 1) ? raw.substr(0, raw.length() - 1) : raw;
	if (numPart.empty() || numPart[0] == '-')
		throw std::runtime_error("Config: invalid size value");

	std::stringstream	ss(numPart);
	size_t				value;

	if (!(ss >> value) || !ss.eof())
		throw std::runtime_error("Config: invalid size value");
	if (value == 0 || value > MAX_CLIENT_BODY_SIZE / mult)
		throw std::runtime_error("Config: size value out of range (max 1G)");
	return (value * mult);
}

bool	ConfigParser::_isNumeric(const std::string& str)
{
	if (str.empty())
		return (false);
	for (size_t i = 0; i < str.length(); i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(str[i])))
			return (false);
	}
	return (true);
}

bool	ConfigParser::_resolveIPv4(const std::string& host, uint32_t& outIp)
{
	struct addrinfo		hints;
	struct addrinfo*	res = NULL;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || res == NULL)
		return (false);

	struct sockaddr_in*	addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
	outIp = addr->sin_addr.s_addr;
	freeaddrinfo(res);
	return (true);
}

// Parse
ListenConfig	ConfigParser::_parseListenValue(const std::string& raw)
{
	ListenConfig	listen;
	std::string		hostPart;
	std::string		portPart;
	size_t			colon;

	colon = raw.find(':');
	if (colon == std::string::npos)
	{
		if (_isNumeric(raw))
		{
			hostPart = DEFAULT_HOST;
			portPart = raw;
		}
		else
		{
			hostPart = raw;
			portPart = DEFAULT_PORT;
		}
	}
	else
	{
		hostPart = raw.substr(0, colon);
		portPart = raw.substr(colon + 1);
	}
	std::stringstream	ss(portPart);
	long				portVal;

	if ((!(ss >> portVal) || !ss.eof())
		|| !(portVal > 0 && portVal <= 65535))
		throw std::runtime_error("Config: 'listen' invalid port (1-65535)");

	uint32_t			ip;
	if (!_resolveIPv4(hostPart, ip))
		throw std::runtime_error("Config: 'listen' unresolvable host");

	listen.ip = ip;
	listen.port = htons(static_cast<uint16_t>(portVal));
	listen.isWildcard = (listen.ip == INADDR_ANY);
	listen.raw = raw;
	return (listen);
}

void	ConfigParser::_parseLocationDirective(LocationConfig& location)
{
	std::string	key = _next();

	if (key == "root")
	{
		if (location.hasRoot)
			throw std::runtime_error("Config: location 'root' already defined");
		std::string path = _next();
		if (!_isValidDirPath(path))
			throw std::runtime_error("Config: location 'root' invalid path");
		location.root = path;
		location.hasRoot = true;
		_expect(";");
	}
	else if (key == "index")
	{
		if (location.hasIndex)
			throw std::runtime_error("Config: location 'index' already defined");
		location.index.clear();
		while (!_atEnd() && _peek() != ";")
		{
			std::string file = _next();
			if (!_isValidFilename(file))
				throw std::runtime_error("Config: location 'index' invalid filename");
			if (std::find(location.index.begin(), location.index.end(), file) != location.index.end())
				throw std::runtime_error("Config: location 'index' duplicate filename");
			location.index.push_back(file);
		}
		if (location.index.empty())
			throw std::runtime_error("Config: location 'index' empty");
		location.hasIndex = true;
		_expect(";");
	}
	else if (key == "autoindex")
	{
		if (location.hasAutoindex)
			throw std::runtime_error("Config: location 'autoindex' already defined");
		std::string value = _next();
		if (value == "on")
			location.autoindex = true;
		else if (value == "off")
			location.autoindex = false;
		else
			throw std::runtime_error("Config: location 'autoindex' expected on/off");
		location.hasAutoindex = true;
		_expect(";");
	}
	else if (key == "client_max_body_size")
	{
		if (location.hasClientMaxBodySize)
			throw std::runtime_error("Config: location 'client_max_body_size' already defined");
		location.clientMaxBodySize = _parseSizeValue(_next());
		location.hasClientMaxBodySize = true;
		_expect(";");
	}
	else if (key == "allow_methods")
	{
		if (location.hasAllowMethods)
			throw std::runtime_error("Config: location 'allow_methods' already defined");
		location.methods.clear();
		while (!_atEnd() && _peek() != ";")
		{
			std::string method = _next();
			if (method != "GET" && method != "POST" && method != "DELETE")
				throw std::runtime_error("Config: location 'allow_methods' unsupported method");
			if (std::find(location.methods.begin(), location.methods.end(), method) != location.methods.end())
				throw std::runtime_error("Config: location 'allow_methods' duplicate method");
			location.methods.push_back(method);
		}
		if (location.methods.empty())
			throw std::runtime_error("Config: location 'allow_methods' empty");
		location.hasAllowMethods = true;
		_expect(";");
	}
	else if (key == "upload_dir")
	{
		if (location.hasUploadPath)
			throw std::runtime_error("Config: location 'upload_dir' already defined");
		std::string path = _next();
		if (!_isValidDirPath(path))
			throw std::runtime_error("Config: location 'upload_dir' invalid path");
		location.uploadPath = path;
		location.hasUploadPath = true;
		_expect(";");
	}
	else if (key == "cgi_extension")
	{
		std::string ext = _next();
		std::string cgiPath = _next();

		if (ext.empty() || ext[0] != '.')
			throw std::runtime_error("Config: location 'cgi_extension' must start with '.'");
		if (!_isValidDirPath(cgiPath))
			throw std::runtime_error("Config: location 'cgi_extension' invalid path");
		if (location.cgiExtension.find(ext) != location.cgiExtension.end())
			throw std::runtime_error("Config: location 'cgi_extension' duplicate");
		location.cgiExtension[ext] = cgiPath;
		_expect(";");
	}
	else if (key == "return" || key == "redirect")
	{
		if (location.hasRedirect)
			throw std::runtime_error("Config: location 'return' already defined");

		std::string codeTok = _next();
		if (!_isNumeric(codeTok))
			throw std::runtime_error("Config: location 'return' code must be numeric");

		std::stringstream ss(codeTok);
		int code;
		if (!(ss >> code) || !ss.eof())
			throw std::runtime_error("Config: location 'return' invalid code format");

		if (!(code == 301 || code == 302 || code == 303 || code == 307 || code == 308))
			throw std::runtime_error("Config: location 'return' unsupported code");

		std::string target = _next();
		if (target.empty())
			throw std::runtime_error("Config: location 'return' empty target");

		bool isAbsolute = (target.compare(0, 7, "http://") == 0
						|| target.compare(0, 8, "https://") == 0);
		if (!isAbsolute && !_isValidLocationPath(target))
			throw std::runtime_error("Config: location 'return' invalid target path");

		location.redirectCode = code;
		location.redirectTarget = target;
		location.hasRedirect = true;
		_expect(";");
	}
	else
		throw std::runtime_error("Config: unknown location directive '" + key + "'");
}

void	ConfigParser::_parseLocationBlock(ServerConfig& server)
{
	LocationConfig	location;

	location.path = _next();
	if (!_isValidLocationPath(location.path))
		throw std::runtime_error("Config: location invalid path");
	for (size_t i = 0; i < server.locations.size(); i++)
		if (server.locations[i].path == location.path)
			throw std::runtime_error("Config: location duplicate path");

	_expect("{");
	while (!_atEnd() && _peek() != "}")
		_parseLocationDirective(location);
	_expect("}");
	server.locations.push_back(location);
}

void	ConfigParser::_parseServerDirective(ServerConfig& server)
{
	std::string	key = _next();

	if (key == "listen")
	{
		ListenConfig	listen = _parseListenValue(_next());

		for (size_t i = 0; i < server.listens.size(); i++)
		{
			if (server.listens[i].ip == listen.ip
				&& server.listens[i].port == listen.port)
				throw std::runtime_error("Config: server 'listen' duplicate in server block");
		}
		server.listens.push_back(listen);
		_expect(";");
	}
	else if (key == "server_name")
	{
		if (server.hasServerName)
			throw std::runtime_error("Config: server 'server_name' already defined");
		std::string name = _next();
		if (!_isValidServerName(name))
			throw std::runtime_error("Config: server 'server_name' invalid characters");
		server.serverName = name;
		server.hasServerName = true;
		_expect(";");
	}
	else if (key == "root")
	{
		if (server.hasRoot)
			throw std::runtime_error("Config: server 'root' already defined");
		std::string path = _next();
		if (!_isValidDirPath(path))
			throw std::runtime_error("Config: server 'root' invalid path");
		server.root = path;
		server.hasRoot = true;
		_expect(";");
	}
	else if (key == "index")
	{
		if (server.hasIndex)
			throw std::runtime_error("Config: server 'index' already defined");
		server.index.clear();
		while (!_atEnd() && _peek() != ";")
		{
			std::string	file = _next();
			if (!_isValidFilename(file))
				throw std::runtime_error("Config: server 'index' invalid filename");
			if (std::find(server.index.begin(), server.index.end(), file) != server.index.end())
				throw std::runtime_error("Config: server 'index' duplicate filename");
			server.index.push_back(file);
		}
		if (server.index.empty())
			throw std::runtime_error("Config: server 'index' empty");
		server.hasIndex = true;
		_expect(";");
	}
	else if (key == "autoindex")
	{
		if (server.hasAutoindex)
			throw std::runtime_error("Config: server 'autoindex' already defined");
		std::string	value = _next();
		if (value == "on")
			server.autoindex = true;
		else if (value == "off")
			server.autoindex = false;
		else
			throw std::runtime_error("Config: server 'autoindex' expected on/off");
		server.hasAutoindex = true;
		_expect(";");
	}
	else if (key == "client_max_body_size")
	{
		if (server.hasClientMaxBodySize)
			throw std::runtime_error("Config: server 'client_max_body_size' already defined");
		server.clientMaxBodySize = _parseSizeValue(_next());
		server.hasClientMaxBodySize = true;
		_expect(";");
	}
	else if (key == "error_page")
	{
		std::vector<int>	codes;

		while (!_atEnd() && _peek() != ";")
		{
			std::string	tok = _next();
			if (!_isNumeric(tok))
			{
				if (!_isValidDirPath(tok))
					throw std::runtime_error("Config: server 'error_page' invalid path");
				if (codes.empty())
					throw std::runtime_error("Config: server 'error_page' missing code");
				for (size_t i = 0; i < codes.size(); i++)
				{
					if (server.errorPages.find(codes[i]) != server.errorPages.end())
						throw std::runtime_error("Config: server 'error_page' duplicate code");
					server.errorPages[codes[i]] = tok;
				}
				_expect(";");
				return ;
			}
			std::stringstream	ss(tok);
			int					code;

			if (!(ss >> code) || !ss.eof())
				throw std::runtime_error("Config: server 'error_page' invalid code format");
			if (!_isValidErrorPageCode(code))
				throw std::runtime_error("Config: server 'error_page' unsupported code");
			codes.push_back(code);
		}
		throw std::runtime_error("Config: server 'error_page' missing path or ';'");
	}
	else if (key == "location")
		_parseLocationBlock(server);
	else
		throw std::runtime_error("Config: unknown server directive '" + key + "'");
}

void	ConfigParser::_parseServerBlock()
{
	ServerConfig	server;
	server.id = static_cast<int>(_servers.size());

	_expect("server");
	_expect("{");
	while (!_atEnd() && _peek() != "}")
		_parseServerDirective(server);
	_expect("}");

	if (server.listens.empty())
		server.listens.push_back(_parseListenValue(DEFAULT_PORT));
	_applyInheritance(server);
	_servers.push_back(server);
}

void	ConfigParser::_applyInheritance(ServerConfig& server)
{
	for (size_t i = 0; i < server.locations.size(); i++)
	{
		LocationConfig&	location = server.locations[i];

		if (!location.hasRoot)
			location.root = server.root;
		if (!location.hasIndex)
			location.index = server.index;
		if (!location.hasAutoindex)
			location.autoindex = server.autoindex;
		if (!location.hasClientMaxBodySize)
			location.clientMaxBodySize = server.clientMaxBodySize;
	}
}

// Tokenize
bool	ConfigParser::_atEnd() const
{
	return (_pos >= _tokens.size());
}

std::string	ConfigParser::_next()
{
	if (_atEnd())
		throw std::runtime_error("Config: unexpected EOF");
	return (_tokens[_pos++]);
}

std::string	ConfigParser::_peek() const
{
	if (_atEnd())
		return ("");
	return (_tokens[_pos]);
}

void	ConfigParser::_expect(const std::string& token)
{
	std::string	tok = _next();

	if (tok != token)
		throw std::runtime_error("Config: expected '" + token + "', found '" + tok + "'");
}

void	ConfigParser::_tokenize(const std::string& content)
{
	std::string	buff;

	for (size_t i = 0; i < content.size(); i++)
	{
		char	ch = content[i];

		if (ch == '#')
		{
			while (i < content.size() && content[i] != '\n')
				i++;
			continue ;
		}
		if (ch == '{' || ch == '}' || ch == ';')
		{
			if (!buff.empty())
			{
				_tokens.push_back(buff);
				buff.clear();
			}
			_tokens.push_back(std::string(1, ch));
		}
		else if (std::isspace(static_cast<unsigned char>(ch)))
		{
			if (!buff.empty())
			{
				_tokens.push_back(buff);
				buff.clear();
			}
		}
		else
			buff += ch;
	}
	if (!buff.empty())
		_tokens.push_back(buff);
}

// Public members
void	ConfigParser::parseFile(const std::string& path)
{
	std::ifstream	file(path.c_str());

	if (!file.is_open())
		throw std::runtime_error("Config: could not open file");

	std::ostringstream	ss;

	ss << file.rdbuf();
	if (file.bad() || (file.fail() && !file.eof()))
		throw std::runtime_error("Config: read error");

	_tokenize(ss.str());
	_pos = 0;
	while (!_atEnd())
		_parseServerBlock();
}

// Getter
const std::vector<ServerConfig>&	ConfigParser::getServers() const
{
	return (_servers);
}

std::vector<ServerConfig>&	ConfigParser::getServers()
{
	return (_servers);
}