#ifndef NET_UTILS_HPP
#define NET_UTILS_HPP

#include <string>
#include <stdint.h>

// Stateless network yardimcilari -- IP<->string donusumu, hostname
// cozumlemesi ve fd'yi non-blocking moda alma. ConfigParser (listen degeri
// parse), ListenTable (debug/hata mesajlari), Server (listen/client
// socket'leri) ve CgiProcess (pipe'lar) arasinda paylasilir.
class NetUtils
{
public:
	// ip Network Byte Order'da beklenir, "a.b.c.d" formatinda doner.
	// inet_ntoa KULLANILMIYOR (subject'in izin verdigi fonksiyon
	// listesinde yok) -- ntohl (izinli) + elle bicimlendirme.
	static std::string	ipToString(uint32_t ip);

	// host bir hostname/IPv4 literal olabilir; basarili olursa outIp'yi
	// Network Byte Order'da doldurup true doner.
	static bool			resolveIPv4(const std::string& host, uint32_t& outIp);

	// fcntl(fd, F_SETFL, O_NONBLOCK) sarmalayicisi -- listen socket, kabul
	// edilen client socket'i ve CGI pipe uclari icin tekrar tekrar yazilan
	// ayni uc satiri tek yerde topluyor.
	static bool			setNonBlocking(int fd);

private:
	// Sinif hic orneklenmez (sadece static metodlar) -- OCF yine de burada,
	// private'da, hicbir zaman kullanilmayacak sekilde tanimli tutuluyor.
	NetUtils();
	~NetUtils();
	NetUtils(const NetUtils& ref);
	NetUtils&	operator=(const NetUtils& ref);
};

#endif
