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

// TEK bir calisan CGI surecinin durumu: fork edilmis child'in pid'i, iki
// pipe ucumuz (child'in stdin'ine yazdigimiz inFd / stdout'undan
// okudugumuz outFd), yazilacak istek govdesi ve o ana kadar okunan cikti.
// mCgiSessions'ta OUT-FD'ye gore saklanir; mCgiPipeOwner her iki pipe
// fd'sini de outFd'ye esler (poll()'un dondurdugu fd'den session bulmak
// icin). Yasam dongusu sources/Cgi/ServerCgi.cpp'de yonetilir. CGI-ozel
// sabit/tip (CGI_TIMEOUT_SECONDS, CgiOutput) includes/Cgi/cgi.hpp'de.
struct CgiSession
{
	pid_t       pid;
	int         clientFd;
	int         inFd;	// child stdin'ine YAZDIGIMIZ uc; kapaninca -1
	int         outFd;	// child stdout'undan OKUDUGUMUZ uc
	std::string body;	// child'in stdin'ine yazilacak istek govdesi
	size_t      written;
	std::string output;
	time_t      start;
};

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
    // CGI: outFd -> session, ve her iki pipe-fd -> outFd ters-arama
    // haritasi (isPollin/isPollout, poll()'un dondurdugu pipe fd'sinden
    // hangi session'a ait oldugunu bulmak icin isCgiPipe/handleCgiPipeEvent
    // uzerinden kullanir).
    std::map<int, CgiSession> mCgiSessions;
    std::map<int, int> mCgiPipeOwner;

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
	// --- CGI (sources/Cgi/ServerCgi.cpp) ---
	void cgiHandle(Client &client, LocationConfig *loc, int fd);
	void registerCgiSession(pid_t pid, int fd, int inPipe[2], int outPipe[2],
			const std::string &body);
	bool isCgiPipe(int pollFd) const;
	bool handleCgiPipeEvent(size_t i);
	void closeCgiFds(CgiSession &session);
	void removePipeFromPoll(int pipeFd);
	void setClientPollEvents(int clientFd, short events);
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
