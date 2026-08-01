#include "ListenTable.hpp"
#include "NetUtils.hpp"
#include "StringUtils.hpp"
#include <stdexcept>
#include <sstream>
#include <netinet/in.h>	// INADDR_ANY
#include <arpa/inet.h>	// ntohs

ListenTable::ListenTable() {}
ListenTable::~ListenTable() {}
ListenTable::ListenTable(const ListenTable& ref) { (void)ref; }
ListenTable&	ListenTable::operator=(const ListenTable& ref) { (void)ref; return (*this); }

void	ListenTable::build(std::vector<ServerConfig>& servers)
{
	_buckets.clear();
	_realSockets.clear();

	_collectBuckets(servers);
	_checkServerNameConflicts();
	_buildRealSockets();
}

// Adim 1: TUM server bloklarindaki TUM listen'lari port -> exact_ip ->
// bucket seklinde topla. Bu asamada henuz hicbir OS-seviyesi karar
// vermiyoruz, sadece "kim kimle ayni (ip,port) uzerinde" sorusuna cevap
// veriyoruz.
void	ListenTable::_collectBuckets(std::vector<ServerConfig>& servers)
{
	for (size_t s = 0; s < servers.size(); s++)
	{
		ServerConfig&	srv = servers[s];
		for (size_t i = 0; i < srv.listens.size(); i++)
		{
			const ListenConfig&	lc = srv.listens[i];
			_buckets[lc.port][lc.ip].push_back(&srv);
		}
	}
}

// Adim 2: her exact (ip,port) bucket'i icinde server_name capismasi var mi
// kontrol et. Ayni bucket'ta iki server ayni server_name'e (isimsiz ""
// dahil) sahipse, Host header geldiginde hangisinin cevap verecegi
// belirsizdir -> reddet. Farkli server_name'e sahip olmalari gecerli bir
// virtual-host senaryosudur.
void	ListenTable::_checkServerNameConflicts() const
{
	std::map<uint16_t, std::map<uint32_t, std::vector<ServerConfig*> > >::const_iterator	pit;

	for (pit = _buckets.begin(); pit != _buckets.end(); ++pit)
	{
		std::map<uint32_t, std::vector<ServerConfig*> >::const_iterator	iit;

		for (iit = pit->second.begin(); iit != pit->second.end(); ++iit)
		{
			const std::vector<ServerConfig*>&	bucket = iit->second;

			for (size_t a = 0; a < bucket.size(); a++)
			{
				for (size_t b = a + 1; b < bucket.size(); b++)
				{
					if (bucket[a]->serverName == bucket[b]->serverName)
					{
						std::ostringstream	oss;
						oss << "Config: " << NetUtils::ipToString(iit->first)
							<< ":" << ntohs(pit->first)
							<< " duplicate server_name '"
							<< bucket[a]->serverName << "' (server id="
							<< bucket[a]->id << " and id=" << bucket[b]->id << ")";
						throw std::runtime_error(oss.str());
					}
				}
			}
		}
	}
}

// Adim 3: NGINX MANTIGI -- gercek socket kararini burada veriyoruz. Bir
// portta wildcard (0.0.0.0) varsa: o port icin SADECE BIR gercek socket
// acilir (wildcard'in kendisi). Ayni portta duran spesifik IP'ler gercek
// bind() almaz -- onlar sadece "routing metadata" olarak _buckets icinde
// kalir ve runtime'da getsockname() ile secilir. Bir portta wildcard
// YOKSA: o portta gorunen her farkli IP kendi gercek socket'ini alir
// (aralarinda gercek bir OS cakismasi olamaz, cunku farkli IP'lerdir).
void	ListenTable::_buildRealSockets()
{
	std::map<uint16_t, std::map<uint32_t, std::vector<ServerConfig*> > >::iterator	pit;

	for (pit = _buckets.begin(); pit != _buckets.end(); ++pit)
	{
		uint16_t	port = pit->first;
		bool		hasWildcard = (pit->second.find(INADDR_ANY) != pit->second.end());

		if (hasWildcard)
		{
			RealListen	rl;
			rl.ip = INADDR_ANY;
			rl.port = port;
			rl.isWildcard = true;
			_realSockets.push_back(rl);
		}
		else
		{
			std::map<uint32_t, std::vector<ServerConfig*> >::iterator	iit;
			for (iit = pit->second.begin(); iit != pit->second.end(); ++iit)
			{
				RealListen	rl;
				rl.ip = iit->first;
				rl.port = port;
				rl.isWildcard = false;
				_realSockets.push_back(rl);
			}
		}
	}
}

const std::vector<RealListen>&	ListenTable::realSockets() const
{
	return (_realSockets);
}

const std::vector<ServerConfig*>*	ListenTable::resolve(uint16_t port, uint32_t localIp) const
{
	std::map<uint16_t, std::map<uint32_t, std::vector<ServerConfig*> > >::const_iterator	pit;

	pit = _buckets.find(port);
	if (pit == _buckets.end())
		return (NULL);

	// 1. Once TAM eslesen spesifik IP var mi bak (en spesifik kazanir, nginx gibi)
	std::map<uint32_t, std::vector<ServerConfig*> >::const_iterator	iit;
	iit = pit->second.find(localIp);
	if (iit != pit->second.end())
		return (&iit->second);

	// 2. Yoksa wildcard bucket'a dus
	iit = pit->second.find(INADDR_ANY);
	if (iit != pit->second.end())
		return (&iit->second);

	return (NULL);
}

const ServerConfig*	ListenTable::chooseByHost(const std::vector<ServerConfig*>& bucket,
											const std::string& hostHeader) const
{
	if (bucket.empty())
		return (NULL);

	if (!hostHeader.empty())
	{
		for (size_t i = 0; i < bucket.size(); i++)
		{
			// HTTP Host header degerleri case-insensitive karsilastirilir
			// (RFC 7230 3.2.2 / RFC 9110).
			if (StringUtils::toLower(bucket[i]->serverName) == StringUtils::toLower(hostHeader))
				return (bucket[i]);
		}
	}
	// Host header yok, bos, ya da hicbir server_name ile eslesmedi ->
	// nginx gibi bucket'taki ILK tanimlanan server "default_server" sayilir.
	return (bucket[0]);
}

// Bir bucket'taki server_name'leri "main_site" ya da (server_name bos ise,
// "" varsayilan/default_server anlamina geldigi icin) "(default)" olarak,
// virgulle ayirip yazar.
static void	printBucketServerNames(std::ostream &os, const std::vector<ServerConfig*> &bucket)
{
	for (size_t i = 0; i < bucket.size(); i++)
	{
		if (i > 0)
			os << ", ";
		os << (bucket[i]->serverName.empty() ? "(default)" : bucket[i]->serverName);
	}
}

void	ListenTable::printListenSummary(std::ostream &os) const
{
	for (size_t i = 0; i < _realSockets.size(); i++)
	{
		const RealListen	&rl = _realSockets[i];
		std::map<uint16_t, std::map<uint32_t, std::vector<ServerConfig*> > >::const_iterator	pit
			= _buckets.find(rl.port);

		os << "Listening on " << NetUtils::ipToString(rl.ip) << ":" << ntohs(rl.port);

		if (pit == _buckets.end())
		{
			os << std::endl;
			continue;
		}

		if (!rl.isWildcard)
		{
			// Bu portta wildcard yok -- her spesifik IP kendi gercek
			// soketini aldigi icin, bu soketin TEK sahibi olan bucket'i
			// (kendi ip'si) yazdirmak yeterli.
			std::map<uint32_t, std::vector<ServerConfig*> >::const_iterator	iit = pit->second.find(rl.ip);
			os << " -> ";
			if (iit != pit->second.end())
				printBucketServerNames(os, iit->second);
			os << std::endl;
			continue;
		}

		// Wildcard soket: bu portta duran TUM bucket'lar (wildcard'in
		// kendisi + gercek socket ALMAMIS spesifik IP'ler) bu TEK soket
		// uzerinden runtime'da getsockname() ile secilir -- hepsini,
		// hangi local IP'ye hangi server_name(ler) dustugunu belli ederek
		// ayri satirlarda gosteriyoruz.
		if (pit->second.size() == 1)
		{
			os << " -> ";
			printBucketServerNames(os, pit->second.begin()->second);
			os << std::endl;
		}
		else
		{
			os << " (wildcard, bu portta birden fazla server_name/IP merge edildi):" << std::endl;
			std::map<uint32_t, std::vector<ServerConfig*> >::const_iterator	iit;
			for (iit = pit->second.begin(); iit != pit->second.end(); ++iit)
			{
				os << "    " << NetUtils::ipToString(iit->first) << ":" << ntohs(rl.port) << " -> ";
				printBucketServerNames(os, iit->second);
				os << std::endl;
			}
		}
	}
}
