#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <string>
#include "ConfigStructs.hpp"
#include "Client.hpp"

// GET/POST/DELETE icin statik dosya sistemi islemlerini (statik dosya
// servisi, index/autoindex, upload yazma, dosya silme) yapan, stateless
// (hicbir zaman ornek yaratilmayan) yardimci sinif. Server::controlMethod
// bu sinifin ciktisina gore sendResponseAndCleanup/sendErrorAndCleanup
// cagirir -- fd/mClients/pollfd yonetimi kasitli olarak burada degil,
// Server'da kaliyor (bu sinif fd bile bilmiyor).
class RequestHandler
{
public:
	// Basarili durumda response'u doldurup true doner. Hata durumunda
	// errorCode'u (orn. 403/404/500) doldurup false doner -- cagiran taraf
	// (Server::controlMethod) bu durumda sendErrorAndCleanup(fd, errorCode)
	// cagirmali.
	static bool handleGet(Client &client, LocationConfig *loc, std::string &response, int &errorCode);
	static bool handlePost(Client &client, LocationConfig *loc, std::string &response, int &errorCode);
	static bool handleDelete(Client &client, LocationConfig *loc, std::string &response, int &errorCode);

	// loc->root + istek path'inden (../ traversal engellenmis) gercek dosya
	// yolunu kurar -- ServerCgi.cpp da CGI script yolunu cozerken bunu
	// kullanir (statik dosya ile CGI script'i AYNI "root altinda, ../
	// yasak" kuraliyla cozuluyor).
	static std::string resolveFilePath(LocationConfig *loc, const std::string &path);

private:
	RequestHandler();
	~RequestHandler();
	RequestHandler(const RequestHandler &other);
	RequestHandler &operator=(const RequestHandler &other);

	static std::string joinPath(const std::string &base, LocationConfig *loc, const std::string &path);
	static std::string resolveUploadPath(LocationConfig *loc, const std::string &path);
	static bool writeUploadFile(const std::string &fullPath, const std::string &body);
	static bool buildAutoindex(const std::string &dirPath, const std::string &requestPath, std::string &out);
};

#endif
