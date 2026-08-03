#include "Server.hpp"
#include "FsUtils.hpp"
#include "HttpUtils.hpp"
#include "HttpResponseBuilder.hpp"
#include "NetUtils.hpp"
#include "StringUtils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <stdexcept>
#include <iostream>
#include <map>
#include <string>

#define POLL_TIMEOUT_MS 1000
#define CLIENT_TIMEOUT_SECONDS 30
#define HEADER_RECEIVE_TIMEOUT_SECONDS 30
#define MAX_REQUEST_LINE_LENGTH 8192
#define MAX_HEADER_BLOCK_SIZE 65536
#define SHUTDOWN_DRAIN_TIMEOUT_SECONDS 12


static volatile sig_atomic_t g_shutdownRequested = 0;

static void handleShutdownSignal(int signum)
{
	(void)signum;
	g_shutdownRequested = 1;
}

Server::Server(const ListenTable &listenTable)
	: mListenTable(listenTable), mRouter(listenTable)
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, handleShutdownSignal);
	signal(SIGTERM, handleShutdownSignal);
}

Server::~Server()
{
    for (std::map<int, CgiSession>::iterator it = mCgiSessions.begin(); it != mCgiSessions.end(); ++it)
    {
        if (it->second.pid > 0)
            kill(it->second.pid, SIGKILL);
    }

    for (size_t i = 0; i < mListenSockets.size(); ++i)
        close(mListenSockets[i].fd);

    for (size_t i = 0; i < mPollFds.size(); ++i)
    {
        if (!isListeningSocket(mPollFds[i].fd))
            close(mPollFds[i].fd);
    }
}


int Server::createListenSocket(const RealListen &rl)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		throw std::runtime_error(std::string("socket: ") + strerror(errno));

	int enable = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

	if (!NetUtils::setNonBlocking(fd))
	{
		int savedErrno = errno;
		close(fd);
		throw std::runtime_error(std::string("fcntl: ") + strerror(savedErrno));
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = rl.ip;
	addr.sin_port        = rl.port;

	if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1)
	{
		int savedErrno = errno;
		close(fd);
		throw std::runtime_error(std::string("bind: ") + strerror(savedErrno));
	}
	if (listen(fd, SOMAXCONN) == -1)
	{
		int savedErrno = errno;
		close(fd);
		throw std::runtime_error(std::string("listen: ") + strerror(savedErrno));
	}
	return (fd);
}

void Server::setupSockets()
{
	const std::vector<RealListen> &realSockets = mListenTable.realSockets();

	for (size_t i = 0; i < realSockets.size(); ++i)
	{
		ListenSocket ls;
		ls.fd   = createListenSocket(realSockets[i]);
		ls.ip   = realSockets[i].ip;
		ls.port = realSockets[i].port;
		mListenSockets.push_back(ls);
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
    struct sockaddr_in peerAddr;
    socklen_t          peerAddrLen = sizeof(peerAddr);
    int clientFd = accept(listenFd, reinterpret_cast<struct sockaddr *>(&peerAddr), &peerAddrLen);
    if (clientFd == -1)
        return;

    if (!NetUtils::setNonBlocking(clientFd))
	{
        close(clientFd);
        return;
    }

    Client &client = mClients[clientFd];
    client.remoteIp = peerAddr.sin_addr.s_addr;

    struct sockaddr_in localAddr;
    socklen_t          localAddrLen = sizeof(localAddr);
    if (getsockname(clientFd, reinterpret_cast<struct sockaddr *>(&localAddr), &localAddrLen) == 0)
        client.localIp = localAddr.sin_addr.s_addr;

    client.listenSocket = findListenSocket(listenFd);
    client.lastActivity = time(NULL);
    client.requestStartTime = time(NULL);

    struct pollfd pfd;
    pfd.fd      = clientFd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    mPollFds.push_back(pfd);
}

void Server::sendResponseAndCleanup(int fd, const std::string &response)
{
	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it == mClients.end())
		return;

	Client &client = it->second;

	client.writeBuffer += response;
	client.closeAfterWrite = !client.keepAlive;

	if (client.keepAlive)
		client.resetForNextRequest();
}

void Server::sendErrorAndCleanup(int fd, int statusCode)
{
	ServerConfig *cfg = NULL;
	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it != mClients.end())
	{
		cfg = it->second.matchedConfig;
		it->second.keepAlive = false;
	}
	sendResponseAndCleanup(fd, HttpResponseBuilder::buildError(statusCode, cfg, false));
}


size_t Server::effectiveMaxBodySize(Client &client)
{
	if (!client.matchedConfig)
		return (DEFAULT_CLIENT_MAX_BODY_SIZE);

	LocationConfig *loc = mRouter.selectLocation(client.matchedConfig, client.request.getPath());
	if (loc)
		return (loc->clientMaxBodySize);
	return (client.matchedConfig->clientMaxBodySize);
}

bool Server::contentLengthCheck(Client &client, int fd)
{
	std::string clHeader = client.request.getHeader("Content-Length");
	if (!clHeader.empty())
	{
		if (!StringUtils::toSizeT(clHeader, client.contentLength))
		{
			sendErrorAndCleanup(fd, 400);
			return (false);
		}
	}

	std::string teHeader = client.request.getHeader("Transfer-Encoding");
	if (teHeader.find("chunked") != std::string::npos)
		client.isChunked = true;

	client.matchedConfig = selectServerConfig(fd);
	size_t maxBodySize = effectiveMaxBodySize(client);
	if (!client.isChunked && maxBodySize > 0 && client.contentLength > maxBodySize)
	{
		sendErrorAndCleanup(fd, 413);
		return (false);
	}

	if (client.request.getVersion() == "HTTP/1.1" && client.request.getHeader("Host").empty())
	{
		sendErrorAndCleanup(fd, 400);
		return (false);
	}

	std::string expectHeader = client.request.getHeader("Expect");
	if ((client.contentLength > 0 || client.isChunked)
		&& (expectHeader == "100-continue" || expectHeader == "100-Continue"))
	{
		client.closeAfterWrite = false;
		client.writeBuffer += "HTTP/1.1 100 Continue\r\n\r\n";
		ensurePollout(fd);
	}

	return(true);
}

bool Server::tryParseHeaders(int fd)
{
	Client &client = mClients[fd];

	size_t headerEnd = client.readBuffer.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
	{
		if (client.readBuffer.size() > MAX_HEADER_BLOCK_SIZE)
		{
			sendErrorAndCleanup(fd, 431);
			return (false);
		}
		return (false);
	}

	std::string headerBlock = client.readBuffer.substr(0, headerEnd);
	if (headerBlock.size() > MAX_HEADER_BLOCK_SIZE)
	{
		client.readBuffer.erase(0, headerEnd + 4);
		sendErrorAndCleanup(fd, 431);
		return (false);
	}

	size_t firstLineEnd = headerBlock.find("\r\n");
	std::string requestLine = headerBlock.substr(0, firstLineEnd);

	if (requestLine.size() > MAX_REQUEST_LINE_LENGTH)
	{
		client.readBuffer.erase(0, headerEnd + 4);
		sendErrorAndCleanup(fd, 414);
		return (false);
	}

	bool requestLineOk = client.request.parseRequestLine(requestLine);
	bool headersOk = client.request.parseHeaderLines(headerBlock);

	client.readBuffer.erase(0, headerEnd + 4);

	if (!requestLineOk || !headersOk)
	{
		sendErrorAndCleanup(fd, 400);
		return (false);
	}

	const std::string &version = client.request.getVersion();
	if (version != "HTTP/1.1" && version != "HTTP/1.0")
	{
		sendErrorAndCleanup(fd, 505);
		return (false);
	}

	client.keepAlive = HttpUtils::determineKeepAlive(client.request);

	if (!(contentLengthCheck(client, fd)))
		return(false);
	client.headersParsed = true;
	return (true);
}

bool Server::isBodyComplete(int fd)
{
	return mClients[fd].readBuffer.size() >= mClients[fd].contentLength;
}

ChunkResult Server::tryUnchunk(Client &client, size_t &consumedLength)
{
	const std::string &raw = client.readBuffer;
	size_t pos = client.chunkParseOffset;

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
		size_t dataStart = lineEnd + 2;

		if (chunkSize == 0)
		{
			if (dataStart + 2 > raw.size())
				return (CHUNK_INCOMPLETE);
			if (raw.compare(dataStart, 2, "\r\n") != 0)
				return (CHUNK_INVALID);
		
			consumedLength = dataStart + 2;
			return (CHUNK_COMPLETE);
		}

		if (dataStart + chunkSize + 2 > raw.size())
			return (CHUNK_INCOMPLETE);
		if (raw.compare(dataStart + chunkSize, 2, "\r\n") != 0)
			return (CHUNK_INVALID);

		client.chunkedBody.append(raw, dataStart, chunkSize);
		pos = dataStart + chunkSize + 2;
		client.chunkParseOffset = pos;
	}
}


ServerConfig *Server::selectServerConfig(int fd)
{
	std::map<int, Client>::iterator cIt = mClients.find(fd);
	if (cIt == mClients.end() || !cIt->second.listenSocket)
		return (NULL);

	Client &client = cIt->second;

	return (mRouter.selectServerConfig(client.listenSocket->port, client.localIp,
			client.request.getHeader("Host")));
}



void Server::getHandle(Client &client, LocationConfig *loc, int fd)
{
	std::string response;
	int errorCode = 500;

	if (RequestHandler::handleGet(client, loc, response, errorCode))
		sendResponseAndCleanup(fd, response);
	else
		sendErrorAndCleanup(fd, errorCode);
}

void Server::postHandle(Client &client, LocationConfig *loc, int fd)
{
	std::string response;
	int errorCode = 500;

	if (RequestHandler::handlePost(client, loc, response, errorCode))
		sendResponseAndCleanup(fd, response);
	else
		sendErrorAndCleanup(fd, errorCode);
}

void Server::deleteHandle(Client &client, LocationConfig *loc, int fd)
{
	std::string response;
	int errorCode = 500;

	if (RequestHandler::handleDelete(client, loc, response, errorCode))
		sendResponseAndCleanup(fd, response);
	else
		sendErrorAndCleanup(fd, errorCode);
}

bool Server::isCgiRequest(LocationConfig *loc, const std::string &path)
{
	if (loc->cgiExtension.empty())
		return (false);

	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos)
		return (false);

	std::string ext = path.substr(dot);
	return (loc->cgiExtension.find(ext) != loc->cgiExtension.end());
}


void Server::controlMethod(Client &client, LocationConfig *loc, int fd)
{
	if (isCgiRequest(loc, client.request.getPath()))
	{
		cgiHandle(client, loc, fd);
		return;
	}

	const std::string &method = client.request.getMethod();

	if (method == "GET")
	{
		getHandle(client, loc, fd);
		return;
	}

	if (method == "POST")
	{
		postHandle(client, loc, fd);
		return;
	}

	if (method == "DELETE")
	{
		deleteHandle(client, loc, fd);
		return;
	}
	sendErrorAndCleanup(fd, 501);
}

void Server::finalizeRequest(int fd)
{
	Client &client = mClients[fd];

	LocationConfig *loc = client.matchedConfig ? mRouter.selectLocation(client.matchedConfig, client.request.getPath()) : NULL;
	if (!loc)
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	if (loc->hasRedirect)
	{
		sendResponseAndCleanup(fd, HttpResponseBuilder::buildRedirect(loc->redirectCode, loc->redirectTarget, client.keepAlive));
		return;
	}

	if (!mRouter.isMethodAllowed(loc, client.request.getMethod()))
	{
		sendErrorAndCleanup(fd, 405);
		return;
	}

	controlMethod(client, loc, fd);
}

void Server::processClient(int fd)
{
	for (std::map<int, CgiSession>::iterator it = mCgiSessions.begin(); it != mCgiSessions.end(); ++it)
	{
		if (it->second.clientFd == fd)
			return;
	}
	Client &client = mClients[fd];
	if (!client.headersParsed && !tryParseHeaders(fd))
		return;

	if (client.isChunked)
	{
		size_t consumedLength = 0;
		ChunkResult result = tryUnchunk(client, consumedLength);

		if (result == CHUNK_INVALID)
		{
			sendErrorAndCleanup(fd, 400);
			return;
		}
		if (result == CHUNK_INCOMPLETE)
		{
			if (client.chunkedBody.size() > effectiveMaxBodySize(client))
			{
				sendErrorAndCleanup(fd, 413);
				return;
			}
			return;
		}

		if (client.chunkedBody.size() > effectiveMaxBodySize(client))
		{
			sendErrorAndCleanup(fd, 413);
			return;
		}

		client.readBuffer.replace(0, consumedLength, client.chunkedBody);
		client.contentLength = client.chunkedBody.size();
	}
	else if (!isBodyComplete(fd))
	{
		return;
	}

	size_t bodyLength = client.contentLength;
	finalizeRequest(fd);

	client.readBuffer.erase(0, bodyLength);
}

bool Server::readClientData(int fd)
{
	char buffer[4096];
	ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

	if (bytesRead <= 0)
		return true;

	Client &client = mClients[fd];
	client.lastActivity = time(NULL);
	client.readBuffer.append(buffer, bytesRead);
	processClient(fd);

	return false;
}

bool Server::writeClientData(int fd)
{
	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it == mClients.end())
		return (true);

	Client &client = it->second;
	ssize_t bytesWritten = write(fd, client.writeBuffer.c_str(), client.writeBuffer.size());
	if (bytesWritten < 0)
		return true;

	client.writeBuffer.erase(0, bytesWritten);

	if (!client.writeBuffer.empty())
		return false;

	bool shouldClose = client.closeAfterWrite;

	if (shouldClose)
		return true;
	processClient(fd);
	return false;
}


void Server::removePollFd(int fd)
{
	for (size_t i = 0; i < mPollFds.size(); ++i)
	{
		if (mPollFds[i].fd == fd)
		{
			mPollFds.erase(mPollFds.begin() + i);
			return;
		}
	}
}

void Server::ensurePollout(int fd)
{
	for (size_t i = 0; i < mPollFds.size(); ++i)
	{
		if (mPollFds[i].fd == fd)
		{
			mPollFds[i].events |= POLLOUT;
			return;
		}
	}
}

void Server::closeClient(size_t i)
{
	int fd = mPollFds[i].fd;
	abortCgi(fd);
	mClients.erase(fd);
	close(fd);
	removePollFd(fd);
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
	int fd = mPollFds[i].fd;

	if (isListeningSocket(fd))
	{
		acceptNewClient(fd);
		return false;
	}
	if (isCgiPipe(fd))
		return (handleCgiPipeEvent(i));

	bool shouldClose = readClientData(fd);
	if (shouldClose)
	{
		closeClient(i);
		return true;
	}

	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it != mClients.end() && !it->second.writeBuffer.empty())
		mPollFds[i].events |= POLLOUT;

	return false;

}
bool Server::isPollout(size_t i)
{
	int fd = mPollFds[i].fd;

	if (isCgiPipe(fd))
		return (handleCgiPipeEvent(i));

	bool shouldClose = writeClientData(fd);
	if (shouldClose)
	{
		closeClient(i);
		return(true);
	}

	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it != mClients.end() && !it->second.writeBuffer.empty())
		mPollFds[i].events |= POLLOUT;
	else
		mPollFds[i].events &= ~POLLOUT;

	return(false);

}

bool Server::isClientTimedOut(size_t i)
{
	int fd = mPollFds[i].fd;
	if (isListeningSocket(fd))
		return false;

	std::map<int, Client>::iterator it = mClients.find(fd);
	if (it == mClients.end())
		return false;

	time_t now = time(NULL);

	if (now - it->second.lastActivity > CLIENT_TIMEOUT_SECONDS)
		return true;

	if (!it->second.headersParsed
		&& (now - it->second.requestStartTime > HEADER_RECEIVE_TIMEOUT_SECONDS))
		return true;

	return false;
}
bool Server::updateDrainState(bool &draining, time_t &drainStart)
{
	if (g_shutdownRequested && !draining)
	{
		draining = true;
		drainStart = time(NULL);
		std::cout << "webserv: shutdown signal received, no longer accepting "
			"new connections, finishing " << mClients.size()
			<< " active connection(s)..." << std::endl;
		for (size_t i = 0; i < mListenSockets.size(); ++i)
		{
			removePollFd(mListenSockets[i].fd);
			close(mListenSockets[i].fd);
		}
		mListenSockets.clear();
	}

	if (draining && mClients.empty() && mCgiSessions.empty())
		return true;
	if (draining && (time(NULL) - drainStart >= SHUTDOWN_DRAIN_TIMEOUT_SECONDS))
	{
		std::cout << "webserv: drain timeout reached, forcing shutdown "
			"of remaining connection(s)..." << std::endl;
		return true;
	}
	return false;
}

void Server::listeningSockets()
{
	bool draining = false;
	time_t drainStart = 0;

	for (;;)
	{
		if (updateDrainState(draining, drainStart))
			break;
		if (mPollFds.empty())
			break;

		int ready;

		ready = poll(&mPollFds[0], mPollFds.size(), POLL_TIMEOUT_MS);
		if (ready == -1)
		{
			if (errno == EINTR)
				continue;
			throw std::runtime_error(std::string("poll: ") + strerror(errno));
		}
		checkCgiTimeouts();

		size_t i = 0;
		while (i < mPollFds.size())
		{
			bool erased = false;
			if (mPollFds[i].revents & (POLLIN | POLLHUP | POLLERR))
				erased = isPollin(i);
			else if (mPollFds[i].revents & POLLOUT)
				erased = isPollout(i);
			if (!erased && isClientTimedOut(i))
			{
				closeClient(i);
				erased = true;
			}
			if (!erased)
				++i;
		}
	}
	std::cout << "webserv: shutdown complete, all connections closed." << std::endl;
}

void Server::run()
{
	addListeningSockets();
	listeningSockets();
}
