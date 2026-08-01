#include "Router.hpp"

Router::Router(const ListenTable &listenTable) : mListenTable(listenTable)
{
}

Router::~Router()
{
}

ServerConfig *Router::selectServerConfig(uint16_t port, uint32_t localIp, const std::string &hostHeader) const
{
	// getsockname() ile ogrenilen gercek local IP + dinlenen port ->
	// ListenTable, wildcard-merge/spesifik-IP kuralina gore dogru bucket'i
	// bulur (bkz. ListenTable::resolve).
	const std::vector<ServerConfig*> *bucket = mListenTable.resolve(port, localIp);
	if (!bucket)
		return (NULL);

	std::string host = hostHeader;
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);

	// chooseByHost bucket icinde Host header'a gore (case-insensitive) esler,
	// eslesme yoksa bucket'taki ilk tanimlanan server'i "default server"
	// olarak doner. ListenTable'in API'si bilerek const -- salt-okunur bir
	// routing tablosu; Server'in geri kalani ise ServerConfig/LocationConfig
	// pointer'larini hep mutable kullaniyor (hicbir yerde mutasyona
	// ugratilmiyor), o yuzden bu tek sinirda const_cast ediyoruz.
	return const_cast<ServerConfig *>(mListenTable.chooseByHost(*bucket, host));
}

bool Router::isPathPrefixMatch(const std::string &path, const std::string &locPath) const
{
	if (path.compare(0, locPath.size(), locPath) != 0)
		return (false);
	if (locPath.size() == path.size())
		return (true);
	if (!locPath.empty() && locPath[locPath.size() - 1] == '/')
		return (true);
	return (path[locPath.size()] == '/');
}

LocationConfig *Router::selectLocation(ServerConfig *cfg, const std::string &path) const
{
	LocationConfig *bestLocation = NULL;

	for (size_t i = 0; i < cfg->locations.size(); ++i)
	{
		LocationConfig &loc = cfg->locations[i];

		if (isPathPrefixMatch(path, loc.path))
		{
			if (!bestLocation || loc.path.size() > bestLocation->path.size())
			{
				bestLocation = &loc;
			}
		}
	}

	return (bestLocation);
}

bool Router::isMethodAllowed(LocationConfig *loc, const std::string &method) const
{
	size_t i = 0;
	while (i < loc->methods.size())
	{
		if (loc->methods[i] == method)
			return (true);
		i++;
	}
	return (false);
}
