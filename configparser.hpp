#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include <string>
#include <vector>
#include <map>

// NOT: Bu struct'ların alan adları/tipleri Server.hpp/Server.cpp'nin
// beklediği şekille BİREBİR aynı (host/port tek listen, index tek dosya,
// cgiExtension tek uzantı, serverNames çoklu isim). Tuzan'ın tuzans/
// klasöründeki parser'ı daha zengin bir model kullanıyor (çoklu listen,
// çoklu index, cgi_extension->interpreter map'i) ama Server.cpp'ye HİÇ
// dokunmamak için parser'ın validasyon/parse mantığı buraya, bu daha
// basit struct şekline hedeflenerek taşındı. cgiInterpreter/redirectCode
// alanları Tuzan'ın directive'lerinden geliyor ama Server.cpp henüz
// bunları okumuyor -- ileride CGI/redirect tarafı genişleyince kullanılabilir.
// Tuzan'ın ConfigStructs.hpp'sindeki gibi her alan için ayrı bir hasX bool'u
// var -- amacı: parser aynı directive'i aynı blok içinde İKİNCİ kez görürse
// (örn. iki tane "root ...;") sessizce üzerine yazmak yerine hata versin.
// Server.cpp bu bool'ları hiç okumuyor, sadece configparser.cpp içindeki
// duplicate-directive kontrolü için var.
struct LocationConfig
{
	std::string              path;
	bool                     hasAllowMethods;
	std::vector<std::string> methods;
	bool                     hasRoot;
	std::string              root;
	bool                     hasIndex;
	std::string              index;
	bool                     hasAutoindex;
	bool                     autoindex;
	bool                     hasClientMaxBodySize;
	bool                     hasUploadPath;
	std::string              uploadPath;
	bool                     hasCgiExtension;
	std::string              cgiExtension;
	std::string              cgiInterpreter; // "cgi_extension .py /usr/bin/python3" -> ikinci token
	bool                     hasRedirect;
	std::string              redirectTarget;
	int                      redirectCode;   // "return 301 /path;" -> 301

	LocationConfig();
};

struct ServerConfig
{
	bool                        hasListen;
	std::string                 host;
	int                         port;
	bool                        hasServerName;
	std::vector<std::string>    serverNames;
	std::map<int, std::string>  errorPages;
	bool                        hasClientMaxBodySize;
	size_t                      clientMaxBodySize;
	std::vector<LocationConfig> locations;

	ServerConfig();
};

class ConfigParser
{
public:
	ConfigParser();
	~ConfigParser();

	std::vector<ServerConfig> parse(const std::string &path);

private:
	ConfigParser(const ConfigParser &other);
	ConfigParser &operator=(const ConfigParser &other);

	struct Token
	{
		std::string value;
		int         line;
	};

	std::string         _path;
	std::vector<Token>  _tokens;
	size_t              _pos;

	// Okuma + tokenize
	std::string _readFile(const std::string &path) const;
	void        _tokenize(const std::string &raw);

	// Token akışı yardımcıları
	bool               _atEnd() const;
	const std::string &_peek() const;
	std::string        _next();
	void               _expect(const std::string &value);
	void               _throwSyntaxError(const std::string &msg) const;
	void               _throwSyntaxErrorAt(const Token &tok, const std::string &msg) const;

	// Parse
	ServerConfig _parseServerBlock();
	void         _parseServerDirective(ServerConfig &server);
	void         _parseLocationBlock(ServerConfig &server);
	void         _parseLocationDirective(LocationConfig &location);

	// Tuzan'ın ConfigParser'ından uyarlanan validasyon yardımcıları
	// (tuzans/sources/ConfigParser.cpp _isValidDirPath/_isValidFilename/
	// _isValidLocationPath/_isValidServerName/_isNumeric/_parseSizeValue/
	// _isValidErrorPageCode ile aynı kurallar).
	bool   _isNumeric(const std::string &s) const;
	bool   _isValidDirPath(const std::string &path) const;
	bool   _isValidFilename(const std::string &name) const;
	bool   _isValidLocationPath(const std::string &uri) const;
	bool   _isValidServerName(const std::string &name) const;
	bool   _isValidErrorPageCode(int code) const;
	size_t _parseSizeValue(const std::string &raw) const;
	void   _validateListenHost(const std::string &host) const;

	// Tuzan'ın ListenTable::_checkServerNameConflicts'inden uyarlandı:
	// aynı host:port üzerinde aynı server_name (boş isim dahil) varsa
	// Host header ile ayırt edilemez -> reddet.
	void _checkServerNameConflicts(const std::vector<ServerConfig> &servers) const;
};

#endif
