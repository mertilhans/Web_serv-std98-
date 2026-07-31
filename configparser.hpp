#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include <string>
#include <vector>
#include <map>

// Bu dosya artık SADECE Server.hpp/Server.cpp'nin beklediği basit struct
// şeklini sağlıyor (host/port tek listen, index tek dosya, cgiExtension
// tek uzantı, serverNames çoklu isim). Gerçek parse işini artık
// ConfigParser.hpp/cpp + ConfigStructs.hpp (namespace raw) yapıyor --
// main.cpp, raw::ServerConfig listesini parse ettikten sonra
// convertConfigs() ile buradaki basit ServerConfig'e çeviriyor. Böylece
// Server.cpp/Server.hpp hiç değişmeden kalabiliyor.
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

	LocationConfig()
		: hasAllowMethods(false), hasRoot(false), hasIndex(false),
		  hasAutoindex(false), autoindex(false), hasClientMaxBodySize(false),
		  hasUploadPath(false), hasCgiExtension(false), hasRedirect(false),
		  redirectCode(0)
	{
		methods.push_back("GET");
	}
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

	ServerConfig()
		: hasListen(false), port(8080), hasServerName(false),
		  hasClientMaxBodySize(false), clientMaxBodySize(1024UL * 1024UL)
	{
	}
};

#endif
