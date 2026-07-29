#include "Server.hpp"
#include "common.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <map>
#include <string>

#define POLL_TIMEOUT_MS 1000
#define CLIENT_TIMEOUT_SECONDS 30

Server::Server(std::vector<ServerConfig> &configs) : mConfigs(configs)
{
}

Server::~Server()
{
    for (size_t i = 0; i < mListenSockets.size(); ++i)
        close(mListenSockets[i].fd);

    for (size_t i = 0; i < mPollFds.size(); ++i)
    {
        if (!isListeningSocket(mPollFds[i].fd))
            close(mPollFds[i].fd);
    }
}

void addrInfoData(struct addrinfo *info)
{

	std::memset(info, 0, sizeof(*info));
	info->ai_family   = AF_INET;
	info->ai_socktype = SOCK_STREAM;
	info->ai_flags    = AI_NUMERICHOST;
}
void Server::listen_data(ServerConfig *cfg)
{
	ListenSocket ls;
	ls.fd   = createListenSocket(cfg->host, cfg->port);
	ls.host = cfg->host;
	ls.port = cfg->port;
	ls.configs.push_back(cfg);
	mListenSockets.push_back(ls);
}

void socket_and_bind(struct addrinfo *res, int *fd)
{
	for (struct addrinfo *it = res; it != NULL; it = it->ai_next)
	{
		*fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (*fd == -1)
			continue;
	
		int enable = 1;
		setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	
		if (fcntl(*fd, F_SETFL, O_NONBLOCK) == -1) 
		{
			close(*fd);
			*fd = -1;
			continue;
		}
		if (bind(*fd, it->ai_addr, it->ai_addrlen) == 0)
			break;
		close(*fd);
		*fd = -1;
	}
}
int Server::createListenSocket(const std::string &host, int port)
{
	struct addrinfo info;
	struct addrinfo *res = NULL;

	addrInfoData(&info);
    std::ostringstream portStream;
    portStream << port;

    int gaiStatus = getaddrinfo(host.c_str(), portStream.str().c_str(), &info, &res);
    if (gaiStatus != 0) 
	{
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(gaiStatus));
    }

	int fd = -1;
	socket_and_bind(res, &fd);
	freeaddrinfo(res);
    if (fd == -1)
        throw std::runtime_error(host + ":" + portStream.str() + ": " + strerror(errno));

    if (listen(fd, SOMAXCONN) == -1) 
	{
        int savedErrno = errno;
        close(fd);
        throw std::runtime_error(host + ":" + portStream.str() + ": listen: " + strerror(savedErrno));
    }

    return (fd);
}


void Server::setupSockets()
{
    for (size_t i = 0; i < mConfigs.size(); ++i) 
	{
        ServerConfig &cfg = mConfigs[i];

        ListenSocket *existing = NULL;
		size_t j = 0;
        while (j < mListenSockets.size()) 
		{
            if (mListenSockets[j].host == cfg.host && mListenSockets[j].port == cfg.port) 
			{
                existing = &mListenSockets[j];
                break;
            }
			j++;
        }
        if (existing)
		{
            existing->configs.push_back(&cfg);
            continue;
        }
		listen_data(&cfg);
    }
}

bool Server::isListeningSocket(int fd) const
{
	size_t i = 0;
    while (i < mListenSockets.size()) 
	{
        if (mListenSockets[i].fd == fd)
            return (true);
		i++;
    }
    return (false);
}

ListenSocket *Server::findListenSocket(int fd)
{
	size_t i = 0;
	while (i < mListenSockets.size())
	{
		if (mListenSockets[i].fd == fd)
			return (&mListenSockets[i]);
		i++;
	}
	return (NULL);
}

void Server::acceptNewClient(int listenFd)
{
    int clientFd = accept(listenFd, NULL, NULL);
    if (clientFd == -1)
        return;

    if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1)
	{
        close(clientFd);
        return;
    }
    ListenSocket *ls = findListenSocket(listenFd);
    if (ls)
        mClientListenSockets[clientFd] = ls;
    mClientLastActivity[clientFd] = time(NULL);
    struct pollfd pfd;
    pfd.fd      = clientFd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    mPollFds.push_back(pfd);
}


static std::string urlDecodePath(const std::string &path)
{
	std::string out;
	out.reserve(path.size());

	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '%' && i + 2 < path.size()
			&& std::isxdigit(static_cast<unsigned char>(path[i + 1]))
			&& std::isxdigit(static_cast<unsigned char>(path[i + 2])))
		{
			int value = static_cast<int>(std::strtol(path.substr(i + 1, 2).c_str(), NULL, 16));
			out += static_cast<char>(value);
			i += 2;
		}
		else
			out += path[i];
	}

	return (out);
}

bool Server::parseRequestLine(const std::string &line, HttpRequest &req)
{
	std::istringstream iss(line);

	if (!(iss >> req.method >> req.path >> req.version))
		return (false);

	std::string extra;
	if (iss >> extra)
		return (false);

	size_t qpos = req.path.find('?');

	//Mr.Mertilhnass, you will process the query string in CGI...
	if (qpos != std::string::npos)
	{
		req.query = req.path.substr(qpos + 1);
		req.path = req.path.substr(0, qpos);
	}

	req.path = urlDecodePath(req.path);

	return (true);
}

std::string Server::buildResponse(int statusCode, const std::string &statusText, const std::string &body, const std::string &contentType)
{
	std::ostringstream response;
	response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
		<< "Content-Length: " << body.size() << "\r\n"
		<< "Content-Type: " << contentType << "\r\n"
		<< "Connection: close\r\n"
		<< "\r\n"
		<< body;
	return (response.str());
}

static std::map<std::string, std::string> createMimeTypes()
{
    std::map<std::string, std::string> mimeType;
    mimeType["html"] = "text/html";
    mimeType["htm"]  = "text/html";
    mimeType["css"]  = "text/css";
    mimeType["js"]   = "application/javascript";
    mimeType["txt"]  = "text/plain";
    mimeType["json"] = "application/json";
    mimeType["png"]  = "image/png";
    mimeType["jpg"]  = "image/jpeg";
    mimeType["jpeg"] = "image/jpeg";
    mimeType["gif"]  = "image/gif";
    mimeType["svg"]  = "image/svg+xml";
    mimeType["ico"]  = "image/x-icon";
    mimeType["pdf"]  = "application/pdf";
    return (mimeType);
}

std::string Server::getContentType(const std::string &path)
{
    static const std::map<std::string, std::string> mimeTypes = createMimeTypes();

    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return ("application/octet-stream");

    std::string ext = path.substr(dot + 1);

    std::map<std::string, std::string>::const_iterator it = mimeTypes.find(ext);
    if (it != mimeTypes.end())
        return (it->second);

    return ("application/octet-stream");
}


std::string Server::buildRedirect(const std::string &location)
{
	std::ostringstream redirect;
	redirect << "HTTP/1.1 301 Moved Permanently\r\n"
		<< "Location: " << location << "\r\n"
		<< "Content-Length: 0\r\n"
		<< "Connection: close\r\n"
		<< "\r\n";
	return (redirect.str());
}


static const StatusEntry StatusTable[] = 
{
	{ 400, "Bad Request" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 413, "Payload Too Large" },
	{ 500, "Internal Server Error" },
	{ 501, "Not Implemented" }
};

static std::string statusTextFor(int statusCode)
{
	size_t count = sizeof(StatusTable) / sizeof(StatusTable[0]);
	for (size_t i = 0; i < count; ++i)
	{
		if (StatusTable[i].code == statusCode)
			return (StatusTable[i].text);
	}
	return ("Error");
}

std::string Server::buildErrorResponse(int statusCode, ServerConfig *cfg)
{
	std::string statusText = statusTextFor(statusCode);

	if (cfg)
	{
		//Tuzan bey sen config içerisinde errorpageleri tutacaksın ben burada yönlendiricem varsa.
		std::map<int, std::string>::const_iterator it = cfg->errorPages.find(statusCode);
		if (it != cfg->errorPages.end())
		{
			std::string content;
			if (readWholeFile(it->second, content))
				return (buildResponse(statusCode, statusText, content));
		}
	}

	std::ostringstream body;
	body << "<html><body><h1>" << statusCode << " " << statusText << "</h1></body></html>";

	return (buildResponse(statusCode, statusText, body.str()));
}


void Server::sendResponseAndCleanup(int fd, const std::string &response)
{
	mClientWriteBuffers[fd] = response;
	mClientReadBuffers.erase(fd);
	mClientStates.erase(fd);
}

void Server::sendErrorAndCleanup(int fd, int statusCode)
{
	ServerConfig *cfg = NULL;
	std::map<int, ClientRequestState>::iterator it = mClientStates.find(fd);
	if (it != mClientStates.end())
		cfg = it->second.matchedConfig;

	sendResponseAndCleanup(fd, buildErrorResponse(statusCode, cfg));
}


bool Server::contentLengthCheck(ClientRequestState &state, int fd)
{
	std::map<std::string, std::string>::iterator clIt = state.request.headers.find("content-length");
	if (clIt != state.request.headers.end())
		state.contentLength = static_cast<size_t>(std::atol(clIt->second.c_str()));

	std::map<std::string, std::string>::iterator teIt = state.request.headers.find("transfer-encoding");
	if (teIt != state.request.headers.end() && teIt->second.find("chunked") != std::string::npos)
		state.isChunked = true;

	state.matchedConfig = selectServerConfig(fd);
	if (!state.isChunked && state.matchedConfig && state.contentLength > state.matchedConfig->clientMaxBodySize)
	{
		sendErrorAndCleanup(fd,413);
		return (false);
	}
	return(true);

}
bool Server::tryParseHeaders(int fd)
{
	size_t headerEnd = mClientReadBuffers[fd].find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return (false);

	ClientRequestState &state = mClientStates[fd];

	std::string headerBlock = mClientReadBuffers[fd].substr(0, headerEnd);
	size_t firstLineEnd = headerBlock.find("\r\n");

	std::string requestLine = headerBlock.substr(0, firstLineEnd);

	bool requestLineOk = parseRequestLine(requestLine, state.request);
	bool headersOk = parseHeaderLines(headerBlock, state.request);

	mClientReadBuffers[fd].erase(0, headerEnd + 4);

	if (!requestLineOk || !headersOk)
	{
		sendErrorAndCleanup(fd, 400);
		return (false);
	}
	if (!(contentLengthCheck(state, fd)))
		return(false);
	state.headersParsed = true;
	return (true);
}

bool Server::isBodyComplete(int fd)
{
	return mClientReadBuffers[fd].size() >= mClientStates[fd].contentLength;
}

ChunkResult Server::tryUnchunk(const std::string &raw, std::string &decoded)
{
	size_t pos = 0;
	decoded.clear();

	while (true)
	{
		size_t lineEnd = raw.find("\r\n", pos);
		if (lineEnd == std::string::npos)
			return (CHUNK_INCOMPLETE);

		std::string sizeLine = raw.substr(pos, lineEnd - pos);
		size_t semicolon = sizeLine.find(';');
		if (semicolon != std::string::npos)
			sizeLine = sizeLine.substr(0, semicolon);

		if (sizeLine.empty() || sizeLine.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
			return (CHUNK_INVALID);

		size_t chunkSize = std::strtoul(sizeLine.c_str(), NULL, 16);
		pos = lineEnd + 2;

		if (chunkSize == 0)
		{
			if (pos + 2 > raw.size())
				return (CHUNK_INCOMPLETE);
			return (raw.compare(pos, 2, "\r\n") == 0 ? CHUNK_COMPLETE : CHUNK_INVALID);
		}

		if (pos + chunkSize + 2 > raw.size())
			return (CHUNK_INCOMPLETE);
		if (raw.compare(pos + chunkSize, 2, "\r\n") != 0)
			return (CHUNK_INVALID);

		decoded.append(raw, pos, chunkSize);
		pos += chunkSize + 2;
	}
}


ServerConfig *Server::selectServerConfig(int fd)
{
	std::map<int, ListenSocket*>::iterator it = mClientListenSockets.find(fd);
	if (it == mClientListenSockets.end() || it->second->configs.empty())
		return (NULL);

	ListenSocket *ls = it->second;

	std::string host = mClientStates[fd].request.headers["host"];
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);

	for (size_t i = 0; i < ls->configs.size(); ++i)
	{
		ServerConfig *cfg = ls->configs[i];
		for (size_t j = 0; j < cfg->serverNames.size(); ++j)
		{
			if (cfg->serverNames[j] == host)
				return (cfg);
		}
	}

	return ls->configs[0];
}
bool Server::isPathPrefixMatch(const std::string &path, const std::string &locPath)
{
	if (path.compare(0, locPath.size(), locPath) != 0)
		return (false);
	if (locPath.size() == path.size())
		return (true);
	if (!locPath.empty() && locPath[locPath.size() - 1] == '/')
		return (true);
	return (path[locPath.size()] == '/');
}

LocationConfig *Server::selectLocation(ServerConfig *cfg, const std::string &path)
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


bool Server::isMethodAllowed(LocationConfig *loc, const std::string &method)
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

bool Server::hasDotDotSegment(const std::string &path)
{
	size_t pos = 0;

	while (pos < path.size())
	{
		size_t slash = path.find('/', pos);
		std::string segment = (slash == std::string::npos) ? path.substr(pos) : path.substr(pos, slash - pos);

		if (segment == "..")
			return true;

		if (slash == std::string::npos)
			break;
		pos = slash + 1;
	}

	return false;
}

std::string Server::joinPath(const std::string &base, LocationConfig *loc, const std::string &path)
{
	std::string relative = path.substr(loc->path.size());
	if (!relative.empty() && relative[0] == '/')
		relative = relative.substr(1);

	if (hasDotDotSegment(relative))
		return "";

	std::string fullPath = base;
	if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
		fullPath += "/";
	fullPath += relative;

	return (fullPath);
}

std::string Server::resolveFilePath(LocationConfig *loc, const std::string &path)
{
	return (joinPath(loc->root, loc, path));
}

std::string Server::resolveUploadPath(LocationConfig *loc, const std::string &path)
{
	return (joinPath(loc->uploadPath, loc, path));
}

bool readWholeFile(const std::string &path, std::string &content)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return (false);

    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return (true);
}

bool Server::writeUploadFile(const std::string &fullPath, const std::string &body)
{
	int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		return (false);

	ssize_t written = write(fd, body.c_str(), body.size());
	close(fd);

	return (written == static_cast<ssize_t>(body.size()));
}
bool Server::buildAutoindex(const std::string &dirPath, const std::string &requestPath, std::string &out)
{
	DIR *dir = opendir(dirPath.c_str());
	if (!dir)
		return (false);

	std::ostringstream html;
	html << "<html><body><h1>Index of " << requestPath << "</h1><ul>";

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name == ".")
			continue;
		html << "<li><a href=\"" << name << "\">" << name << "</a></li>";
	}
	closedir(dir);

	html << "</ul></body></html>";
	out = html.str();
	return (true);
}

void Server::getHandle(ClientRequestState &state, LocationConfig *loc, int fd)
{
	std::string fullPath = resolveFilePath(loc, state.request.path);

	struct stat st;
	bool exists = (stat(fullPath.c_str(), &st) == 0);
	bool isDir = (exists && S_ISDIR(st.st_mode));

	if (isDir && (state.request.path.empty() || state.request.path[state.request.path.size() - 1] != '/'))
	{
		sendResponseAndCleanup(fd, buildRedirect(state.request.path + "/"));
		return;
	}
	std::string dirPath;

	if (isDir)
	{
		if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
			fullPath += "/";
		dirPath = fullPath;
		fullPath += loc->index;
		exists = (stat(fullPath.c_str(), &st) == 0);
	}

	if (exists && S_ISREG(st.st_mode) && access(fullPath.c_str(), R_OK) != 0)
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	std::string content;
	if (exists && S_ISREG(st.st_mode) && readWholeFile(fullPath, content))
	{
		sendResponseAndCleanup(fd, buildResponse(200, "OK", content, getContentType(fullPath)));
		return;
	}

	if (isDir && loc->autoindex && buildAutoindex(dirPath, state.request.path, content))
	{
		sendResponseAndCleanup(fd, buildResponse(200, "OK", content));
		return;
	}

	if (isDir)
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	sendErrorAndCleanup(fd, 404);
	return;
}

void Server::postHandle(ClientRequestState &state, LocationConfig *loc, int fd)
{
	if (loc->uploadPath.empty())
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	std::string fullPath = resolveUploadPath(loc, state.request.path);

	if (fullPath.empty() || fullPath[fullPath.size() - 1] == '/')
	{
		sendErrorAndCleanup(fd, 400);
		return;
	}

	struct stat existSt;
	bool alreadyExisted = (stat(fullPath.c_str(), &existSt) == 0);

	std::string body = state.isChunked
		? mClientReadBuffers[fd]
		: mClientReadBuffers[fd].substr(0, state.contentLength);

	if (writeUploadFile(fullPath, body))
	{
		if (alreadyExisted)
			sendResponseAndCleanup(fd, buildResponse(200, "OK",
				"<html><body><h1>200 OK</h1></body></html>"));
		else
			sendResponseAndCleanup(fd, buildResponse(201, "Created",
				"<html><body><h1>201 Created</h1></body></html>"));
		return;
	}

	sendErrorAndCleanup(fd, 500);
}

void Server::deleteHandle(ClientRequestState &state, LocationConfig *loc, int fd)
{
	if (loc->uploadPath.empty())
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	std::string fullPath = resolveUploadPath(loc, state.request.path);

	if (fullPath.empty() || fullPath[fullPath.size() - 1] == '/')
	{
		sendErrorAndCleanup(fd, 400);
		return;
	}

	struct stat st;
	if (stat(fullPath.c_str(), &st) == -1 || !S_ISREG(st.st_mode))
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	if (remove(fullPath.c_str()) != 0)
	{
		sendErrorAndCleanup(fd, 500);
		return;
	}

	sendResponseAndCleanup(fd, buildResponse(200, "OK",
		"<html><body><h1>200 OK</h1><p>Deleted</p></body></html>"));
}

// isCgiRequest / cgiHandle / isCgiPipe / handleCgiPipeEvent / cleanupCgiFds /
// removePipeFromPoll / setClientPollEvents implementasyonları buradan
// cgi.cpp'ye taşındı (Server.hpp'deki bildirimler aynı kaldı, sadece
// gövdeler başka dosyada). Aşağıdaki controlMethod ve daha aşağıdaki
// listeningSockets() bu fonksiyonları çağırıyor, tanım cgi.cpp'de.

void Server::controlMethod(ClientRequestState &state, LocationConfig *loc, int fd)
{
	if (isCgiRequest(loc, state.request.path))
	{
		cgiHandle(state, loc, fd);
		return;
	}

	if (state.request.method == "GET")
	{
		getHandle(state, loc, fd);
		return;
	}

	if (state.request.method == "POST")
	{
		postHandle(state, loc, fd);
		return;
	}

	if (state.request.method == "DELETE")
	{
		deleteHandle(state, loc, fd);
		return;
	}

	sendErrorAndCleanup(fd, 501);
}

void Server::finalizeRequest(int fd)
{
	ClientRequestState &state = mClientStates[fd];

	LocationConfig *loc = state.matchedConfig ? selectLocation(state.matchedConfig, state.request.path) : NULL;
	if (!loc)
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	if (!loc->redirectTarget.empty())
	{
		sendResponseAndCleanup(fd, buildRedirect(loc->redirectTarget));
		return;
	}

	if (!isMethodAllowed(loc, state.request.method))
	{
		sendErrorAndCleanup(fd, 405);
		return;
	}

	controlMethod(state, loc, fd);
}

void Server::processClient(int fd)
{
	if (!mClientStates[fd].headersParsed && !tryParseHeaders(fd))
		return;

	ClientRequestState &state = mClientStates[fd];

	if (state.isChunked)
	{
		std::string decoded;
		ChunkResult result = tryUnchunk(mClientReadBuffers[fd], decoded);

		if (result == CHUNK_INVALID)
		{
			sendErrorAndCleanup(fd, 400);
			return;
		}
		if (result == CHUNK_INCOMPLETE)
			return;

		if (state.matchedConfig && decoded.size() > state.matchedConfig->clientMaxBodySize)
		{
			sendErrorAndCleanup(fd, 413);
			return;
		}
		mClientReadBuffers[fd] = decoded;
	}
	else if (!isBodyComplete(fd))
	{
		return;
	}

	finalizeRequest(fd);
}


// RFC 7230 3.2: header adları case-insensitive'dir. Karşılaştırmaları
// basit tutmak için hepsini saklarken lowercase'e normalize ediyoruz
// (client "Content-Length" da gönderse "content-length" de gönderse
// aynı key altında saklanır); header değerleri olduğu gibi kalır.
static std::string toLowerHeaderName(const std::string &name)
{
	std::string out = name;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return (out);
}

bool Server::parseHeaderLines(const std::string &headerBlock, HttpRequest &req)
{

	size_t lineStart = headerBlock.find("\r\n");
	if (lineStart == std::string::npos)
		return true;
	lineStart += 2;

	while (lineStart < headerBlock.size())
	{
		size_t lineEnd = headerBlock.find("\r\n", lineStart);
		if (lineEnd == std::string::npos)
			lineEnd = headerBlock.size();

		std::string line = headerBlock.substr(lineStart, lineEnd - lineStart);

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			return false;

		std::string name = line.substr(0, colon);
		if (name.empty() || name.find_first_of(" \t") != std::string::npos)
			return false;

		std::string value = line.substr(colon + 1);

		size_t valueStart = value.find_first_not_of(' ');
		value = (valueStart == std::string::npos) ? "" : value.substr(valueStart);

		req.headers[toLowerHeaderName(name)] = value;

		lineStart = lineEnd + 2;
	}

	return true;
}

bool Server::readClientData(int fd)
{
	char buffer[4096];
	ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

	if (bytesRead <= 0)
		return true;

	mClientLastActivity[fd] = time(NULL);
	mClientReadBuffers[fd].append(buffer, bytesRead);
	processClient(fd);

	return false;
}

bool Server::writeClientData(int fd)
{
	std::string &out = mClientWriteBuffers[fd];

	ssize_t bytesWritten = write(fd, out.c_str(), out.size());
	if (bytesWritten < 0)
		return true;

	out.erase(0, bytesWritten);

	if (out.empty())
		return true;

	return false;
}


void Server::closeClient(size_t i)
{
	int fd = mPollFds[i].fd;

	mClientReadBuffers.erase(fd);
	mClientWriteBuffers.erase(fd);
	mClientStates.erase(fd);
	mClientListenSockets.erase(fd);
	mClientLastActivity.erase(fd);
	close(fd);
	mPollFds.erase(mPollFds.begin() + i);
}


void Server::addListeningSockets()
{
	for (size_t i = 0; i < mListenSockets.size(); ++i)
	{
		struct pollfd pfd;
		pfd.fd      = mListenSockets[i].fd;
		pfd.events  = POLLIN;
		pfd.revents = 0;
		mPollFds.push_back(pfd);
	}
	if (mPollFds.empty())
		throw std::runtime_error("run: no listening sockets, nothing to do");
}


bool Server::isPollin(size_t i)
{
	if (isListeningSocket(mPollFds[i].fd))
	{
		acceptNewClient(mPollFds[i].fd);
		return false;
	}

	bool shouldClose = readClientData(mPollFds[i].fd);
	if (shouldClose)
	{
		closeClient(i);
		return true;
	}

	if (mClientWriteBuffers.count(mPollFds[i].fd) > 0)
		mPollFds[i].events |= POLLOUT;

	return false;

}
bool Server::isPollout(size_t i)
{
	bool shouldClose = writeClientData(mPollFds[i].fd);
	if (shouldClose)
	{
		closeClient(i);
		return(true);
	}
	return(false);

}

bool Server::isClientTimedOut(size_t i)
{
	int fd = mPollFds[i].fd;
	if (isListeningSocket(fd))
		return false;

	std::map<int, time_t>::iterator it = mClientLastActivity.find(fd);
	if (it == mClientLastActivity.end())
		return false;

	return (time(NULL) - it->second) > CLIENT_TIMEOUT_SECONDS;
}
void Server::listeningSockets()
{
	for(;;)
	{
		int ready;

		ready = poll(&mPollFds[0], mPollFds.size(), POLL_TIMEOUT_MS);
		if (ready == -1)
		{
			if (errno == EINTR)
				continue;
			throw std::runtime_error(std::string("poll: ") + strerror(errno));
		}
		size_t i = 0;
		while (i < mPollFds.size())
		{
			bool erased = false;

			// CGI pipe fd'leri de bu TEK poll()'den geçiyor (subject: "only 1
			// poll() for all I/O"), o yüzden client soketi mantığından önce
			// ayırıyoruz. Aşağıdaki orijinal client dallanmasına dokunmadım.
			if (isCgiPipe(mPollFds[i].fd))
			{
				erased = handleCgiPipeEvent(i);
			}
			else
			{
				if (mPollFds[i].revents & POLLIN)
					erased = isPollin(i);
				if (!erased && (mPollFds[i].revents & POLLOUT))
					erased = isPollout(i);
				if (!erased && isClientTimedOut(i))
				{
					closeClient(i);
					erased = true;
				}
			}
			if (!erased)
				++i;
		}
	}
}

void Server::run()
{
	addListeningSockets();
	listeningSockets();
}