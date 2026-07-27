#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <string>
#include <map>
#include <ctime>
#include <poll.h>
#include "configpars.hpp"


struct StatusEntry
{
	int         code;
	const char  *text;
};

enum ChunkResult
{
    CHUNK_INCOMPLETE,
    CHUNK_COMPLETE,
    CHUNK_INVALID
};




struct HttpRequest
{
    std::string method;
    std::string path;
    std::string query;
    std::string version;
    std::map<std::string, std::string> headers;
};

struct ClientRequestState
{
    bool                    headersParsed;
    HttpRequest             request;
    size_t                  contentLength;
    bool                    isChunked;
    ServerConfig            *matchedConfig;

    ClientRequestState() : headersParsed(false), contentLength(0), isChunked(false), matchedConfig(NULL) {}
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
    std::map<int, time_t> mClientLastActivity;

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
	bool isPathPrefixMatch(const std::string &path, const std::string &locPath);
	bool isMethodAllowed(LocationConfig *loc, const std::string &method);
	bool hasDotDotSegment(const std::string &path);
	std::string joinPath(const std::string &base, LocationConfig *loc, const std::string &path);
	std::string resolveFilePath(LocationConfig *loc, const std::string &path);
	std::string resolveUploadPath(LocationConfig *loc, const std::string &path);
	bool writeUploadFile(const std::string &fullPath, const std::string &body);
	bool buildAutoindex(const std::string &dirPath, const std::string &requestPath, std::string &out);
    std::string buildResponse(int statusCode, const std::string &statusText, const std::string &body, const std::string &contentType = "text/html");
    std::string getContentType(const std::string &path);
    std::string buildErrorResponse(int statusCode, ServerConfig *cfg);
    std::string buildRedirect(const std::string &location);
    void sendErrorAndCleanup(int fd, int statusCode);
    void sendResponseAndCleanup(int fd, const std::string &response);
    void closeClient(size_t i);
	bool isPollin(size_t i);
	bool isPollout(size_t i);
	bool isClientTimedOut(size_t i);
	void controlMethod(ClientRequestState &state, LocationConfig *loc, int fd);
	bool isCgiRequest(LocationConfig *loc, const std::string &path);
	void cgiHandle(ClientRequestState &state, LocationConfig *loc, int fd);
	void getHandle(ClientRequestState &state, LocationConfig *loc, int fd);
	void postHandle(ClientRequestState &state, LocationConfig *loc, int fd);
	void deleteHandle(ClientRequestState &state, LocationConfig *loc, int fd);
	bool contentLengthCheck(ClientRequestState &state, int fd);
	ChunkResult tryUnchunk(const std::string &raw, std::string &decoded);


};

#endif
