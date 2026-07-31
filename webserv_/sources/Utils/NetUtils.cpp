#include "NetUtils.hpp"
#include <sstream>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

// OCF -- private, hic cagrilmaz (sinif sadece static metodlarla kullanilir).
NetUtils::NetUtils()
{
}

NetUtils::~NetUtils()
{
}

NetUtils::NetUtils(const NetUtils& ref)
{
	(void)ref;
}

NetUtils&	NetUtils::operator=(const NetUtils& ref)
{
	(void)ref;
	return (*this);
}

std::string	NetUtils::ipToString(uint32_t ip)
{
	uint32_t			hostOrder = ntohl(ip);
	std::ostringstream	oss;

	oss << ((hostOrder >> 24) & 0xFF) << "."
		<< ((hostOrder >> 16) & 0xFF) << "."
		<< ((hostOrder >> 8) & 0xFF) << "."
		<< (hostOrder & 0xFF);
	return (oss.str());
}

bool	NetUtils::resolveIPv4(const std::string& host, uint32_t& outIp)
{
	struct addrinfo		hints;
	struct addrinfo*	res = NULL;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || res == NULL)
		return (false);

	struct sockaddr_in*	addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
	outIp = addr->sin_addr.s_addr;
	freeaddrinfo(res);
	return (true);
}

bool	NetUtils::setNonBlocking(int fd)
{
	int	flags = fcntl(fd, F_GETFL, 0);

	if (flags == -1)
		return (false);
	return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}
