#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <string>
#include <map>
#include <ctime>
#include <poll.h>
#include <sys/types.h>	// pid_t
#include "ConfigStructs.hpp"
#include "ListenTable.hpp"
#include "Client.hpp"
#include "Router.hpp"
#include "RequestHandler.hpp"
#include "CgiProcess.hpp"

enum ChunkResult
{
    CHUNK_INCOMPLETE,
    CHUNK_COMPLETE,
    CHUNK_INVALID
};

// Gercekten bind()+listen() edilmis TEK bir soket. ListenTable::realSockets()'taki
// her RealListen icin bir tane acilir -- ip/port ayni sekilde Network Byte Order.
struct ListenSocket
{
    int         fd;
    uint32_t    ip;
    uint16_t    port;
};

// CgiProcess artik gercek bir sinif (bkz. includes/CgiProcess.hpp): fork+pipe
// yasam dongusunu kendi yonetir. mCgiProcesses'te CLIENT fd'sine gore
// saklanir (bir client'in ayni anda en fazla bir CGI'si olabilir, cunku bir
// sonraki istek CGI bitmeden islenmiyor -- bkz. processClient re-entrancy
// korumasi).

class Server
{
public:
    Server(const ListenTable &listenTable);
    ~Server();
    void setupSockets();
    void run();

private:
    std::vector<struct pollfd> mPollFds;
    const ListenTable &mListenTable;
    std::vector<ListenSocket> mListenSockets;
    // Eskiden fd'ye gore paralel giden 8 ayri map vardi (read/write buffer,
    // request state, listen socket, local/remote IP, last-activity,
    // close-after-write) -- artik hepsi TEK bir Client nesnesinde (bkz.
    // includes/Client.hpp), map'i fd -> Client olarak tek yerde tutuyoruz.
    std::map<int, Client> mClients;
    // Routing (port+localIp+Host header -> ServerConfig -> LocationConfig)
    // artik burada degil, Router icinde (bkz. includes/Router.hpp).
    Router mRouter;
    // CGI: client fd -> surec durumu, ve iki pipe-fd -> client-fd ters-arama
    // haritasi (isPollin/isPollout, poll()'un dondurdugu pipe fd'sinden
    // hangi client'in CGI'si oldugunu bulmak icin kullanir).
    std::map<int, CgiProcess> mCgiProcesses;
    std::map<int, int> mCgiStdinToClient;
    std::map<int, int> mCgiStdoutToClient;
    // finalizeCgi/checkCgiTimeouts/abortCgi hicbir zaman blocking waitpid
    // cagirmaz -- hemen reap olmayan pid'ler buraya eklenir, checkCgiTimeouts
    // her ana dongu turunda WNOHANG ile tekrar dener.
    std::vector<pid_t> mPendingReap;

    Server(const Server &other);
    Server &operator=(const Server &other);

    int createListenSocket(const RealListen &rl);

    bool isListeningSocket(int fd) const;
    ListenSocket *findListenSocket(int fd);
    void acceptNewClient(int listenFd);
    bool readClientData(int fd);
    bool writeClientData(int fd);
    void addListeningSockets();
    void listeningSockets();
	void processClient(int fd);
	bool tryParseHeaders(int fd);
	bool isBodyComplete(int fd);
	void finalizeRequest(int fd);
	ServerConfig *selectServerConfig(int fd);
    void sendErrorAndCleanup(int fd, int statusCode);
    void sendResponseAndCleanup(int fd, const std::string &response);
    void closeClient(size_t i);
	// mPollFds'ten fd DEGERINE gore arayip siler (index'e gore degil) --
	// CGI temizligi ayni anda birden fazla pollfd girisini (stdin+stdout
	// pipe'lari, hatta ayni anda client fd'sini) silebiliyor; index'e
	// guvenmek bir onceki silme sonrasi yanlis girisi silme riski tasir.
	void removePollFd(int fd);
	// finalizeCgi gibi, o an POLLIN/POLLOUT islenen index'in DISINDAKI bir
	// fd'ye (asil client'a) yeni bir yanit kuyruklandiginda, o fd'nin
	// pollfd girisine POLLOUT eklemek icin.
	void ensurePollout(int fd);
	bool isPollin(size_t i);
	bool isPollout(size_t i);
	bool isClientTimedOut(size_t i);
	void controlMethod(Client &client, LocationConfig *loc, int fd);
	bool isCgiRequest(LocationConfig *loc, const std::string &path);
	void cgiHandle(Client &client, LocationConfig *loc, int fd);
	// --- CGI (sources/ServerCgi.cpp) ---
	void startCgi(Client &client, LocationConfig *loc, int fd,
			const std::string &interpreterPath, const std::string &scriptPath);
	std::vector<std::string> buildCgiEnv(Client &client, LocationConfig *loc,
			int fd, const std::string &scriptPath);
	bool handleCgiStdinWritable(size_t i);
	bool handleCgiStdoutReadable(size_t i);
	void finalizeCgi(int clientFd);
	void checkCgiTimeouts();
	void abortCgi(int clientFd);
	void getHandle(Client &client, LocationConfig *loc, int fd);
	void postHandle(Client &client, LocationConfig *loc, int fd);
	void deleteHandle(Client &client, LocationConfig *loc, int fd);
	bool contentLengthCheck(Client &client, int fd);
	size_t effectiveMaxBodySize(Client &client);
	ChunkResult tryUnchunk(Client &client, size_t &consumedLength);

};

#endif
