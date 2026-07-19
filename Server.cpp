#include "Server.hpp"
#include "common.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cstdlib>

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

void addrinfo_data(struct addrinfo *info)
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
	
		int yes = 1;
		setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	
		if (fcntl(*fd, F_SETFL, O_NONBLOCK) == -1) {
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

	addrinfo_data(&info);
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

    if (listen(fd, SOMAXCONN) == -1) {
        int savedErrno = errno;
        close(fd);
        throw std::runtime_error(host + ":" + portStream.str() + ": listen: " + strerror(savedErrno));
    }

    return fd;
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

    struct pollfd pfd;
    pfd.fd      = clientFd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    mPollFds.push_back(pfd);
}



bool Server::parseRequestLine(const std::string &line, HttpRequest &req)
{
	std::istringstream iss(line);

	if (!(iss >> req.method >> req.path >> req.version))
		return false;

	std::string extra;
	if (iss >> extra)
		return false;

	return true;
}

std::string Server::buildResponse(int statusCode, const std::string &statusText, const std::string &body)
{
	std::ostringstream oss;
	oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
		<< "Content-Length: " << body.size() << "\r\n"
		<< "Content-Type: text/html\r\n"
		<< "Connection: close\r\n"
		<< "\r\n"
		<< body;
	return oss.str();
}


std::string Server::buildErrorResponse(int statusCode)
{
	switch (statusCode)
	{
		case 400:
			return buildResponse(400, "Bad Request",
				"<html><body><h1>400 Bad Request</h1></body></html>");
		case 403:
			return buildResponse(403, "Forbidden",
				"<html><body><h1>403 Forbidden</h1></body></html>");
		case 404:
			return buildResponse(404, "Not Found",
				"<html><body><h1>404 Not Found</h1></body></html>");
		case 405:
			return buildResponse(405, "Method Not Allowed",
				"<html><body><h1>405 Method Not Allowed</h1></body></html>");
		case 413:
			return buildResponse(413, "Payload Too Large",
				"<html><body><h1>413 Payload Too Large</h1></body></html>");
		case 500:
			return buildResponse(500, "Internal Server Error",
				"<html><body><h1>500 Internal Server Error</h1></body></html>");
		default:
			return buildResponse(statusCode, "Error",
				"<html><body><h1>Error</h1></body></html>");
	}
}


void Server::sendErrorAndCleanup(int fd, int statusCode)
{
	mClientWriteBuffers[fd] = buildErrorResponse(statusCode);
	mClientReadBuffers.erase(fd);
	mClientStates.erase(fd);
}

bool Server::tryParseHeaders(int fd)
{
	size_t headerEnd = mClientReadBuffers[fd].find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return false;

	ClientRequestState &state = mClientStates[fd];

	std::string headerBlock = mClientReadBuffers[fd].substr(0, headerEnd);
	size_t firstLineEnd = headerBlock.find("\r\n");

	std::string requestLine;
	if (firstLineEnd == std::string::npos)
		requestLine = headerBlock;
	else
		requestLine = headerBlock.substr(0, firstLineEnd);

	bool requestLineOk = parseRequestLine(requestLine, state.request);
	bool headersOk = parseHeaderLines(headerBlock, state.request);

	mClientReadBuffers[fd].erase(0, headerEnd + 4);

	if (!requestLineOk || !headersOk)
	{
		sendErrorAndCleanup(fd, 400);
		return false;
	}

	std::map<std::string, std::string>::iterator clIt = state.request.headers.find("Content-Length");
	if (clIt != state.request.headers.end())
		state.contentLength = static_cast<size_t>(std::atol(clIt->second.c_str()));

	state.headersParsed = true;
	return true;
}

bool Server::isBodyComplete(int fd)
{
	return mClientReadBuffers[fd].size() >= mClientStates[fd].contentLength;
}


ServerConfig *Server::selectServerConfig(int fd)
{
	std::map<int, ListenSocket*>::iterator it = mClientListenSockets.find(fd);
	if (it == mClientListenSockets.end() || it->second->configs.empty())
		return (NULL);

	ListenSocket *ls = it->second;

	std::string host = mClientStates[fd].request.headers["Host"];
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
LocationConfig *Server::selectLocation(ServerConfig *cfg, const std::string &path)
{
	LocationConfig *bestLocation = NULL;

	for (size_t i = 0; i < cfg->locations.size(); ++i)
	{
		LocationConfig &loc = cfg->locations[i];
		
		if (path.find(loc.path) == 0)
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

std::string Server::resolveFilePath(LocationConfig *loc, const std::string &path)
{
	std::string relative = path.substr(loc->path.size());
	if (!relative.empty() && relative[0] == '/')
		relative = relative.substr(1);

	std::string fullPath = loc->root;
	if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
		fullPath += "/";
	fullPath += relative;

	return fullPath;
}

bool Server::serveStaticFile(const std::string &fullPath, std::string &content)
{
	struct stat st;
	if (stat(fullPath.c_str(), &st) == -1)
		return false;
	if (!S_ISREG(st.st_mode))
		return false;

	return readWholeFile(fullPath, content);
}

void Server::finalizeRequest(int fd)
{
	ClientRequestState &state = mClientStates[fd];
	state.matchedConfig = selectServerConfig(fd);

	LocationConfig *loc = state.matchedConfig ? selectLocation(state.matchedConfig, state.request.path) : NULL;
	if (!loc)
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	if (!isMethodAllowed(loc, state.request.method))
	{
		sendErrorAndCleanup(fd, 405);
		return;
	}

	if (state.request.method == "GET")
	{
		std::string fullPath = resolveFilePath(loc, state.request.path);
		std::string content;
		if (serveStaticFile(fullPath, content))
		{
			mClientWriteBuffers[fd] = buildResponse(200, "OK", content);
			mClientReadBuffers.erase(fd);
			mClientStates.erase(fd);
			return;
		}
		sendErrorAndCleanup(fd, 404);
		return;
	}

	std::string body = "<html><body><h1>It works!</h1><p>matched location: " + loc->path + "</p></body></html>";
	mClientWriteBuffers[fd] = buildResponse(200, "OK", body);

	mClientReadBuffers.erase(fd);
	mClientStates.erase(fd);
}

void Server::processClient(int fd)
{
	if (!mClientStates[fd].headersParsed && !tryParseHeaders(fd))
		return;

	if (!isBodyComplete(fd))
		return;

	finalizeRequest(fd);
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
		std::string value = line.substr(colon + 1);

		size_t valueStart = value.find_first_not_of(' ');
		value = (valueStart == std::string::npos) ? "" : value.substr(valueStart);

		req.headers[name] = value;

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
	mClientReadBuffers.erase(mPollFds[i].fd);
	mClientWriteBuffers.erase(mPollFds[i].fd);
	mClientStates.erase(mPollFds[i].fd);
	mClientListenSockets.erase(mPollFds[i].fd);
	close(mPollFds[i].fd);
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
void Server::listeningSockets()
{
	for(;;)
	{
		int ready;

		ready = poll(&mPollFds[0], mPollFds.size(), -1);
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

			if (mPollFds[i].revents & POLLIN)
				erased = isPollin(i);

			if (!erased && (mPollFds[i].revents & POLLOUT))
				erased = isPollout(i);
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
