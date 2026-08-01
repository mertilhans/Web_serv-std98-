#ifndef CONFIGSTRUCTS_HPP
#define CONFIGSTRUCTS_HPP

#include <stdint.h>
#include <string>
#include <vector>
#include <map>

#define DEFAULT_CONFIG_PATH "configs/default.conf"
#define DEFAULT_HOST "0.0.0.0"
#define DEFAULT_PORT "8080"
#define DEFAULT_ROOT "./www"
#define DEFAULT_INDEX "index.html"
#define DEFAULT_CLIENT_MAX_BODY_SIZE 1048576
#define MAX_CLIENT_BODY_SIZE 1073741824
#define DEFAULT_AUTOINDEX false
#define DEFAULT_METHOD "GET"

struct ListenConfig
{
public:
	uint32_t	ip;
	uint16_t	port;
	bool		isWildcard;
	std::string	raw;

	ListenConfig();
};

struct LocationConfig
{
public:
	std::string							path;
	bool								hasRoot;
	std::string							root;
	bool								hasIndex;
	std::vector<std::string>			index;
	bool								hasAutoindex;
	bool								autoindex;
	bool								hasClientMaxBodySize;
	size_t								clientMaxBodySize;
	bool								hasAllowMethods;
	std::vector<std::string>			methods;
	bool								hasUploadPath;
	std::string							uploadPath;
	std::map<std::string, std::string>	cgiExtension;
	bool								hasRedirect;
	int									redirectCode;
	std::string							redirectTarget;

	LocationConfig();
};

struct ServerConfig
{
public:
	int							id;
	std::vector<ListenConfig>	listens;
	std::map<int, std::string>	errorPages;
	bool						hasServerName;
	std::string					serverName;
	bool						hasRoot;
	std::string					root;
	bool						hasIndex;
	std::vector<std::string>	index;
	bool						hasClientMaxBodySize;
	size_t						clientMaxBodySize;
	bool						hasAutoindex;
	bool						autoindex;

	std::vector<LocationConfig>	locations;

	ServerConfig();
};

#endif
