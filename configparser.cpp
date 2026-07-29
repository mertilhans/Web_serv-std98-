#include "configparser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <climits>
#include <netdb.h>
#include <cstring>

#define DEFAULT_CLIENT_MAX_BODY_SIZE (1024UL * 1024UL)
#define MAX_CLIENT_BODY_SIZE (1024UL * 1024UL * 1024UL) // 1G üst sınır (Tuzan'ınkiyle aynı)

LocationConfig::LocationConfig()
	: hasAllowMethods(false), hasRoot(false), hasIndex(false),
	  hasAutoindex(false), autoindex(false), hasClientMaxBodySize(false),
	  hasUploadPath(false), hasCgiExtension(false), hasRedirect(false),
	  redirectCode(0)
{
	// Tuzan'ın LocationConfig ctor'ındaki gibi: methods hiç tanımlanmazsa
	// bomboş kalıp her isteğe 405 dönme tuzağına düşmesin diye GET default.
	methods.push_back("GET");
}

ServerConfig::ServerConfig()
	: hasListen(false), port(8080), hasServerName(false),
	  hasClientMaxBodySize(false), clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE)
{
}

ConfigParser::ConfigParser()
	: _pos(0)
{
}

ConfigParser::~ConfigParser()
{
}

// ---- Okuma + tokenize ----

std::string ConfigParser::_readFile(const std::string &path) const
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		throw std::runtime_error(path + ": cannot open config file");

	std::ostringstream buffer;
	buffer << file.rdbuf();
	if (file.bad())
		throw std::runtime_error(path + ": read error");
	return buffer.str();
}

void ConfigParser::_tokenize(const std::string &raw)
{
	std::string current;
	int line = 1;
	bool inComment = false;

	for (size_t i = 0; i < raw.size(); ++i)
	{
		char c = raw[i];

		if (c == '\n')
		{
			inComment = false;
			if (!current.empty())
			{
				Token t; t.value = current; t.line = line;
				_tokens.push_back(t);
				current.clear();
			}
			++line;
			continue;
		}
		if (inComment)
			continue;
		if (c == '#')
		{
			if (!current.empty())
			{
				Token t; t.value = current; t.line = line;
				_tokens.push_back(t);
				current.clear();
			}
			inComment = true;
			continue;
		}
		if (std::isspace(static_cast<unsigned char>(c)))
		{
			if (!current.empty())
			{
				Token t; t.value = current; t.line = line;
				_tokens.push_back(t);
				current.clear();
			}
			continue;
		}
		if (c == '{' || c == '}' || c == ';')
		{
			if (!current.empty())
			{
				Token t; t.value = current; t.line = line;
				_tokens.push_back(t);
				current.clear();
			}
			Token sym; sym.value = std::string(1, c); sym.line = line;
			_tokens.push_back(sym);
			continue;
		}
		current += c;
	}
	if (!current.empty())
	{
		Token t; t.value = current; t.line = line;
		_tokens.push_back(t);
	}
}

// ---- Token akışı yardımcıları ----

bool ConfigParser::_atEnd() const
{
	return _pos >= _tokens.size();
}

const std::string &ConfigParser::_peek() const
{
	static const std::string empty;
	if (_atEnd())
		return empty;
	return _tokens[_pos].value;
}

std::string ConfigParser::_next()
{
	if (_atEnd())
		throw std::runtime_error(_path + ": unexpected end of file");
	return _tokens[_pos++].value;
}

void ConfigParser::_expect(const std::string &value)
{
	if (_atEnd())
		throw std::runtime_error(_path + ": unexpected end of file, expected '" + value + "'");
	if (_tokens[_pos].value != value)
		_throwSyntaxErrorAt(_tokens[_pos], "expected '" + value + "', found '" + _tokens[_pos].value + "'");
	++_pos;
}

void ConfigParser::_throwSyntaxError(const std::string &msg) const
{
	throw std::runtime_error(_path + ": " + msg);
}

void ConfigParser::_throwSyntaxErrorAt(const Token &tok, const std::string &msg) const
{
	std::ostringstream oss;
	oss << _path << ":" << tok.line << ": " << msg;
	throw std::runtime_error(oss.str());
}

// ---- Validasyon yardımcıları (Tuzan'ın ConfigParser'ından uyarlandı) ----

bool ConfigParser::_isNumeric(const std::string &s) const
{
	if (s.empty())
		return false;
	for (size_t i = 0; i < s.size(); ++i)
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
	return true;
}

bool ConfigParser::_isValidDirPath(const std::string &path) const
{
	if (path.empty() || path.find('\0') != std::string::npos)
		return false;

	bool isAbsolute   = (path[0] == '/');
	bool isRelCurrent = (path.compare(0, 2, "./") == 0);
	bool isRelParent  = (path.compare(0, 3, "../") == 0);

	return isAbsolute || isRelCurrent || isRelParent;
}

bool ConfigParser::_isValidFilename(const std::string &name) const
{
	if (name.empty() || name.find('\0') != std::string::npos)
		return false;
	if (name.find('/') != std::string::npos || name.find("..") != std::string::npos)
		return false;
	return true;
}

bool ConfigParser::_isValidLocationPath(const std::string &uri) const
{
	if (uri.empty() || uri[0] != '/')
		return false;
	if (uri.find('\0') != std::string::npos)
		return false;
	return true;
}

bool ConfigParser::_isValidServerName(const std::string &name) const
{
	for (size_t i = 0; i < name.size(); ++i)
	{
		char ch = name[i];
		if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '-' && ch != '_')
			return false;
	}
	return true;
}

// Sadece gerçek/standart HTTP hata kodları kabul edilir (subject: "default
// error pages"; RFC'de olmayan bir kod için sayfa üretmenin anlamı yok).
bool ConfigParser::_isValidErrorPageCode(int code) const
{
	return (code == 400 || code == 401 || code == 403 || code == 404
		|| code == 405 || code == 406 || code == 408 || code == 411
		|| code == 413 || code == 414 || code == 415 || code == 429
		|| code == 431 || code == 500 || code == 501 || code == 502
		|| code == 503 || code == 504 || code == 505 || code == 507
		|| code == 508);
}

size_t ConfigParser::_parseSizeValue(const std::string &raw) const
{
	if (raw.empty())
		throw std::runtime_error(_path + ": empty size value");

	char   unit = raw[raw.size() - 1];
	size_t mult = 1;

	if (unit == 'K' || unit == 'k')
		mult = 1024UL;
	else if (unit == 'M' || unit == 'm')
		mult = 1024UL * 1024UL;
	else if (unit == 'G' || unit == 'g')
		mult = 1024UL * 1024UL * 1024UL;

	std::string numPart = (mult > 1) ? raw.substr(0, raw.size() - 1) : raw;
	if (!_isNumeric(numPart))
		throw std::runtime_error(_path + ": invalid size value '" + raw + "'");

	std::stringstream ss(numPart);
	size_t            value;
	if (!(ss >> value) || !ss.eof())
		throw std::runtime_error(_path + ": invalid size value '" + raw + "'");
	if (value == 0 || value > MAX_CLIENT_BODY_SIZE / mult)
		throw std::runtime_error(_path + ": size value out of range (max 1G) '" + raw + "'");
	return value * mult;
}

// getaddrinfo ile host'un çözümlenebilir olduğunu doğrular (Tuzan'ın
// _resolveIPv4'ünün basitleştirilmiş hali) -- Server.cpp zaten kendi
// createListenSocket'inde de getaddrinfo çağırıyor, burada sadece parse
// zamanında ERKEN hata vermek için var, sonucu atıyoruz.
void ConfigParser::_validateListenHost(const std::string &host) const
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_NUMERICHOST; // Server.cpp'nin createListenSocket'iyle tutarlı: sadece IP literal

	int status = getaddrinfo(host.c_str(), NULL, &hints, &res);
	if (res)
		freeaddrinfo(res);
	if (status != 0)
		throw std::runtime_error(_path + ": 'listen' unresolvable host '" + host + "'");
}

// ---- Parse ----

void ConfigParser::_parseServerDirective(ServerConfig &server)
{
	const Token &keyTok = _tokens[_pos];
	std::string  key    = _next();

	if (key == "listen")
	{
		if (server.hasListen)
			_throwSyntaxErrorAt(keyTok, "'listen' already defined for this server block");
		std::string value = _next();
		size_t colon = value.find(':');
		std::string hostPart;
		std::string portPart;

		if (colon == std::string::npos)
		{
			if (_isNumeric(value))
			{
				hostPart = "0.0.0.0";
				portPart = value;
			}
			else
			{
				hostPart = value;
				portPart = "8080";
			}
		}
		else
		{
			hostPart = value.substr(0, colon);
			portPart = value.substr(colon + 1);
		}

		if (!_isNumeric(portPart))
			_throwSyntaxErrorAt(keyTok, "'listen' invalid port (1-65535)");
		std::stringstream ss(portPart);
		long portVal;
		if (!(ss >> portVal) || !ss.eof() || portVal <= 0 || portVal > 65535)
			_throwSyntaxErrorAt(keyTok, "'listen' invalid port (1-65535)");

		_validateListenHost(hostPart);

		server.host = hostPart;
		server.port = static_cast<int>(portVal);
		server.hasListen = true;
		_expect(";");
	}
	else if (key == "server_name")
	{
		if (server.hasServerName)
			_throwSyntaxErrorAt(keyTok, "'server_name' already defined for this server block");
		while (!_atEnd() && _peek() != ";")
		{
			std::string name = _next();
			if (!_isValidServerName(name))
				_throwSyntaxErrorAt(keyTok, "'server_name' invalid characters in '" + name + "'");
			server.serverNames.push_back(name);
		}
		server.hasServerName = true;
		_expect(";");
	}
	else if (key == "client_max_body_size")
	{
		if (server.hasClientMaxBodySize)
			_throwSyntaxErrorAt(keyTok, "'client_max_body_size' already defined for this server block");
		server.clientMaxBodySize = _parseSizeValue(_next());
		server.hasClientMaxBodySize = true;
		_expect(";");
	}
	else if (key == "error_page")
	{
		std::vector<int> codes;
		while (!_atEnd() && _peek() != ";")
		{
			std::string tok = _next();
			if (!_isNumeric(tok))
			{
				if (!_isValidDirPath(tok) && !_isValidLocationPath(tok))
					_throwSyntaxErrorAt(keyTok, "'error_page' invalid path '" + tok + "'");
				if (codes.empty())
					_throwSyntaxErrorAt(keyTok, "'error_page' missing code before path");
				for (size_t i = 0; i < codes.size(); ++i)
					server.errorPages[codes[i]] = tok;
				_expect(";");
				return;
			}
			std::stringstream ss(tok);
			int code;
			if (!(ss >> code) || !ss.eof())
				_throwSyntaxErrorAt(keyTok, "'error_page' invalid code format '" + tok + "'");
			if (!_isValidErrorPageCode(code))
				_throwSyntaxErrorAt(keyTok, "'error_page' unsupported code " + tok);
			codes.push_back(code);
		}
		_throwSyntaxErrorAt(keyTok, "'error_page' missing path or ';'");
	}
	else if (key == "location")
	{
		_parseLocationBlock(server);
	}
	else
	{
		_throwSyntaxErrorAt(keyTok, "unknown server directive '" + key + "'");
	}
}

void ConfigParser::_parseLocationDirective(LocationConfig &location)
{
	const Token &keyTok = _tokens[_pos];
	std::string  key    = _next();

	if (key == "root")
	{
		if (location.hasRoot)
			_throwSyntaxErrorAt(keyTok, "'root' already defined for this location");
		std::string path = _next();
		if (!_isValidDirPath(path))
			_throwSyntaxErrorAt(keyTok, "'root' invalid path '" + path + "'");
		location.root = path;
		location.hasRoot = true;
		_expect(";");
	}
	else if (key == "index")
	{
		if (location.hasIndex)
			_throwSyntaxErrorAt(keyTok, "'index' already defined for this location");
		std::string file = _next();
		if (!_isValidFilename(file))
			_throwSyntaxErrorAt(keyTok, "'index' invalid filename '" + file + "'");
		location.index = file;
		location.hasIndex = true;
		_expect(";");
	}
	else if (key == "autoindex")
	{
		if (location.hasAutoindex)
			_throwSyntaxErrorAt(keyTok, "'autoindex' already defined for this location");
		std::string value = _next();
		if (value == "on")
			location.autoindex = true;
		else if (value == "off")
			location.autoindex = false;
		else
			_throwSyntaxErrorAt(keyTok, "'autoindex' expected on/off, got '" + value + "'");
		location.hasAutoindex = true;
		_expect(";");
	}
	else if (key == "client_max_body_size")
	{
		if (location.hasClientMaxBodySize)
			_throwSyntaxErrorAt(keyTok, "'client_max_body_size' already defined for this location");
		// Location seviyesinde override -- Server.cpp bu alanı henüz
		// okumuyor (sadece server seviyesindekini kullanıyor), ama config
		// tarafında geçerli/parse edilebilir olması gerekiyor.
		_parseSizeValue(_next());
		location.hasClientMaxBodySize = true;
		_expect(";");
	}
	else if (key == "allow_methods" || key == "methods")
	{
		if (location.hasAllowMethods)
			_throwSyntaxErrorAt(keyTok, "'allow_methods' already defined for this location");
		location.methods.clear();
		while (!_atEnd() && _peek() != ";")
		{
			std::string method = _next();
			if (method != "GET" && method != "POST" && method != "DELETE")
				_throwSyntaxErrorAt(keyTok, "'allow_methods' unsupported method '" + method + "'");
			if (std::find(location.methods.begin(), location.methods.end(), method) != location.methods.end())
				_throwSyntaxErrorAt(keyTok, "'allow_methods' duplicate method '" + method + "'");
			location.methods.push_back(method);
		}
		if (location.methods.empty())
			_throwSyntaxErrorAt(keyTok, "'allow_methods' empty");
		location.hasAllowMethods = true;
		_expect(";");
	}
	else if (key == "upload_dir" || key == "upload_path")
	{
		if (location.hasUploadPath)
			_throwSyntaxErrorAt(keyTok, "'upload_dir' already defined for this location");
		std::string path = _next();
		if (!_isValidDirPath(path))
			_throwSyntaxErrorAt(keyTok, "'upload_dir' invalid path '" + path + "'");
		location.uploadPath = path;
		location.hasUploadPath = true;
		_expect(";");
	}
	else if (key == "cgi_extension")
	{
		if (location.hasCgiExtension)
			_throwSyntaxErrorAt(keyTok, "'cgi_extension' already defined for this location");
		std::string ext = _next();
		if (ext.empty() || ext[0] != '.')
			_throwSyntaxErrorAt(keyTok, "'cgi_extension' must start with '.' ('" + ext + "')");

		// Tuzan'ın formatı: cgi_extension .py /usr/bin/python3;
		// İkinci token noktalı virgül değilse interpreter path'i sayılır.
		location.cgiExtension = ext;
		if (!_atEnd() && _peek() != ";")
		{
			std::string interpreterPath = _next();
			if (!_isValidDirPath(interpreterPath))
				_throwSyntaxErrorAt(keyTok, "'cgi_extension' invalid interpreter path '" + interpreterPath + "'");
			location.cgiInterpreter = interpreterPath;
		}
		location.hasCgiExtension = true;
		_expect(";");
	}
	else if (key == "return" || key == "redirect")
	{
		if (location.hasRedirect)
			_throwSyntaxErrorAt(keyTok, "'return' already defined for this location");
		std::string first = _next();
		if (_isNumeric(first))
		{
			std::stringstream ss(first);
			int code;
			if (!(ss >> code) || !ss.eof())
				_throwSyntaxErrorAt(keyTok, "'return' invalid code format '" + first + "'");
			if (!(code == 301 || code == 302 || code == 303 || code == 307 || code == 308))
				_throwSyntaxErrorAt(keyTok, "'return' unsupported code " + first);

			std::string target = _next();
			location.redirectCode   = code;
			location.redirectTarget = target;
		}
		else
		{
			// Eski tek-argümanlı biçim de desteklenir: return /hedef;
			location.redirectCode   = 301;
			location.redirectTarget = first;
		}
		location.hasRedirect = true;
		_expect(";");
	}
	else
	{
		_throwSyntaxErrorAt(keyTok, "unknown location directive '" + key + "'");
	}
}

void ConfigParser::_parseLocationBlock(ServerConfig &server)
{
	const Token &locTok = _tokens[_pos - 1]; // "location" token'ı zaten _next() ile tüketildi
	(void)locTok;

	if (_atEnd())
		throw std::runtime_error(_path + ": unexpected end of file after 'location'");
	const Token &pathTok = _tokens[_pos];

	LocationConfig location;
	location.path = _next();
	if (!_isValidLocationPath(location.path))
		_throwSyntaxErrorAt(pathTok, "location invalid path '" + location.path + "'");

	for (size_t i = 0; i < server.locations.size(); ++i)
		if (server.locations[i].path == location.path)
			_throwSyntaxErrorAt(pathTok, "duplicate location path '" + location.path + "'");

	_expect("{");
	while (!_atEnd() && _peek() != "}")
		_parseLocationDirective(location);
	if (_atEnd())
		throw std::runtime_error(_path + ": unexpected end of file inside 'location' block");
	_expect("}");

	server.locations.push_back(location);
}

ServerConfig ConfigParser::_parseServerBlock()
{
	ServerConfig server;

	_expect("server");
	_expect("{");
	while (!_atEnd() && _peek() != "}")
		_parseServerDirective(server);
	if (_atEnd())
		throw std::runtime_error(_path + ": unexpected end of file inside 'server' block");
	_expect("}");

	if (!server.hasListen)
		throw std::runtime_error(_path + ": server block missing required 'listen' directive");

	return server;
}

// Tuzan'ın ListenTable::_checkServerNameConflicts mantığının uyarlanmış
// hali: aynı host:port üzerinde aynı server_name (boş isim -- "default
// server" -- dahil) varsa Host header ile ayırt edilemez, reddet.
void ConfigParser::_checkServerNameConflicts(const std::vector<ServerConfig> &servers) const
{
	for (size_t a = 0; a < servers.size(); ++a)
	{
		for (size_t b = a + 1; b < servers.size(); ++b)
		{
			if (servers[a].host != servers[b].host || servers[a].port != servers[b].port)
				continue;

			const std::vector<std::string> &namesA = servers[a].serverNames;
			const std::vector<std::string> &namesB = servers[b].serverNames;

			if (namesA.empty() && namesB.empty())
			{
				std::ostringstream oss;
				oss << _path << ": duplicate server on " << servers[a].host << ":" << servers[a].port
					<< " (ikisi de isimsiz -- Host header ile ayırt edilemez)";
				throw std::runtime_error(oss.str());
			}
			for (size_t i = 0; i < namesA.size(); ++i)
			{
				if (std::find(namesB.begin(), namesB.end(), namesA[i]) != namesB.end())
				{
					std::ostringstream oss;
					oss << _path << ": duplicate server_name '" << namesA[i] << "' on "
						<< servers[a].host << ":" << servers[a].port;
					throw std::runtime_error(oss.str());
				}
			}
		}
	}
}

std::vector<ServerConfig> ConfigParser::parse(const std::string &path)
{
	_path = path;
	_pos  = 0;
	_tokens.clear();

	std::string raw = _readFile(path);
	_tokenize(raw);

	std::vector<ServerConfig> result;
	while (!_atEnd())
		result.push_back(_parseServerBlock());

	if (result.empty())
		throw std::runtime_error(path + ": no 'server' block found");

	_checkServerNameConflicts(result);

	return result;
}
