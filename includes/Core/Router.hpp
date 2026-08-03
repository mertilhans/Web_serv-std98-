#ifndef ROUTER_HPP
#define ROUTER_HPP

#include <string>
#include <stdint.h>
#include "ConfigStructs.hpp"
#include "ListenTable.hpp"

// ListenTable'i sarar: accept()+getsockname() sonucundan (port, local ip)
// ve HTTP istegindeki Host header'indan gercek ServerConfig'i, sonra
// istek path'inden en uzun prefix-esleyen LocationConfig'i secer, en
// sonunda o location'in method izinlerini kontrol eder. Server bu
// sinifin ustune sadece "glue" ekler (Client/fd yonetimi Server'da kalir).
class Router
{
public:
	Router(const ListenTable &listenTable);
	~Router();

	// port + accept() sonrasi getsockname() ile ogrenilen gercek local ip
	// (ikisi de Network Byte Order) ve Host header degeri verildiginde,
	// wildcard-merge/spesifik-IP kuralina gore dogru ServerConfig'i doner.
	// Eslesme yoksa NULL.
	ServerConfig *selectServerConfig(uint16_t port, uint32_t localIp, const std::string &hostHeader) const;

	// cfg icindeki location'lar arasindan path'e en uzun prefix ile eslesen
	// LocationConfig'i doner (nginx tarzi "longest prefix match"). Eslesme
	// yoksa NULL.
	LocationConfig *selectLocation(ServerConfig *cfg, const std::string &path) const;

	bool isMethodAllowed(LocationConfig *loc, const std::string &method) const;

private:
	const ListenTable &mListenTable;

	Router(const Router &other);
	Router &operator=(const Router &other);

	bool isPathPrefixMatch(const std::string &path, const std::string &locPath) const;
};

#endif
