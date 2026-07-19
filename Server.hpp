#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <string>
#include <map>
#include <poll.h>
#include "configpars.hpp"


struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
};

struct ClientRequestState
{
    bool                    headersParsed;
    HttpRequest             request;
    size_t                  contentLength;
    ServerConfig            *matchedConfig;

    ClientRequestState() : headersParsed(false), contentLength(0), matchedConfig(NULL) {}
};

struct ListenSocket
{
    int                         fd;
    std::string                 host;
    int                         port;
    std::vector<ServerConfig*>  configs;
};

class Server
{
public:
    Server(std::vector<ServerConfig> &configs);
    ~Server();
    void setupSockets();
    void run();

private:
    std::vector<struct pollfd> mPollFds;
    std::vector<ServerConfig> mConfigs;
    std::vector<ListenSocket> mListenSockets;
    std::map<int, std::string> mClientReadBuffers;
    std::map<int, std::string> mClientWriteBuffers;
    std::map<int, ClientRequestState> mClientStates;
    std::map<int, ListenSocket*> mClientListenSockets;

    Server(const Server &other);
    Server &operator=(const Server &other);

    int createListenSocket(const std::string &host, int port);
    void listen_data(ServerConfig *cfg);

    bool isListeningSocket(int fd) const;
    ListenSocket *findListenSocket(int fd);
    void acceptNewClient(int listenFd);
    bool readClientData(int fd);
    bool writeClientData(int fd);
    bool parseRequestLine(const std::string &line, HttpRequest &req);
    bool parseHeaderLines(const std::string &headerBlock, HttpRequest &req);
    void addListeningSockets();
    void listeningSockets();
	void processClient(int fd);
	bool tryParseHeaders(int fd);
	bool isBodyComplete(int fd);
	void finalizeRequest(int fd);
	ServerConfig *selectServerConfig(int fd);
	LocationConfig *selectLocation(ServerConfig *cfg, const std::string &path);
	bool isMethodAllowed(LocationConfig *loc, const std::string &method);
	std::string resolveFilePath(LocationConfig *loc, const std::string &path);
	bool serveStaticFile(const std::string &fullPath, std::string &content);
    std::string buildResponse(int statusCode, const std::string &statusText, const std::string &body);
    std::string buildErrorResponse(int statusCode);
    void sendErrorAndCleanup(int fd, int statusCode);
    void closeClient(size_t i);
	bool isPollin(size_t i);
	bool isPollout(size_t i);
	void controlMethod(ClientRequestState &state, LocationConfig *loc, int fd);
	void getHandle(ClientRequestState &state, LocationConfig *loc, int fd);



};

#endif
