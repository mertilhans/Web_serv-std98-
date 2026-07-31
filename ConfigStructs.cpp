#include "ConfigStructs.hpp"

using namespace raw;

ListenConfig::ListenConfig()
	: ip(0), port(0),
	isWildcard(false), raw("")
{
}

LocationConfig::LocationConfig()
	: path(""),
	hasRoot(false), root(""),
	hasIndex(false), index(),
	hasAutoindex(false), autoindex(false),
	hasClientMaxBodySize(false), clientMaxBodySize(0),
	hasAllowMethods(false), methods(),
	hasUploadPath(false), uploadPath(""),
	cgiExtension(),
	hasRedirect(false),redirectCode(0), redirectTarget("")
{
	methods.push_back(DEFAULT_METHOD);
}

ServerConfig::ServerConfig()
	: id(0),
	listens(),
	errorPages(),
	hasServerName(false), serverName(""),
	hasRoot(false), root(DEFAULT_ROOT),
	hasIndex(false), index(),
	hasClientMaxBodySize(false), clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE),
	hasAutoindex(false), autoindex(DEFAULT_AUTOINDEX),
	locations()
{
	index.push_back(DEFAULT_INDEX);
}