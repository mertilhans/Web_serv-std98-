#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <ctime>
#include <stdint.h>
#include "HttpRequest.hpp"
#include "ConfigStructs.hpp"

struct ListenSocket;

// TEK bir client baglantisinin butun durumunu tasir. Eskiden Server'da
// fd'ye gore paralel giden 8 ayri std::map vardi (readBuffer, writeBuffer,
// requestState, listenSocket, localIp, remoteIp, lastActivity,
// closeAfterWrite) -- her closeClient/accept HEPSINI ayni fd ile tek tek
// guncellemek zorundaydi. Artik Server tek bir `std::map<int, Client>`
// tutuyor; bu sinif "bir client'in ne oldugu"nu tek yerde topluyor.
class Client
{
public:
	Client();
	~Client();
	Client(const Client& ref);
	Client&	operator=(const Client& ref);

	// Keep-alive'da bir yanit gonderildikten sonra BIR SONRAKI istegi
	// karsilamak icin per-request alanlari sifirlar. Baglanti-seviyesi
	// alanlar (listenSocket/localIp/remoteIp) DOKUNULMADAN kalir --
	// onlar TCP baglantisi acikken hep aynidir, istekten istege degismez.
	void	resetForNextRequest();

	// --- baglanti seviyesi (accept'te bir kez kurulur, closeClient'e kadar sabit) ---
	ListenSocket	*listenSocket;
	uint32_t		localIp;	// getsockname() ile, wildcard-merge routing icin
	uint32_t		remoteIp;	// accept() ile, CGI REMOTE_ADDR icin
	time_t			lastActivity;		// idle timeout: HER basarili read()'de yenilenir

	// readBuffer/writeBuffer da baglanti-seviyesindedir (resetForNextRequest
	// TARAFINDAN TEMIZLENMEZ) -- pipelining'de readBuffer bir sonraki
	// istegin baytlarini zaten icerebilir, writeBuffer ise writeClientData
	// tarafindan bagimsiz olarak yonetilir.
	std::string		readBuffer;
	std::string		writeBuffer;

	// --- istek seviyesi (her istekte / keep-alive resetForNextRequest()'te sifirlanir) ---
	bool			headersParsed;
	HttpRequest		request;
	size_t			contentLength;
	bool			isChunked;
	bool			keepAlive;
	ServerConfig	*matchedConfig;
	bool			closeAfterWrite;
	// Chunked govde KADEMELI (incremental) parse edilir -- her yeni
	// read()'de readBuffer'in BASINDAN itibaren tekrar taramak (chunked
	// govdenin gorulmus kismini her seferinde yeniden decode etmek) buyuk
	// govdelerde O(n^2) maliyete yol acar (gercek olcum: 20MB chunked ~20s,
	// Content-Length ile ayni boyut <1.5s). chunkParseOffset readBuffer'da
	// SON DOGRULANMIS chunk sinirini, chunkedBody o ana kadar decode
	// edilmis govdeyi tutar -- Server::tryUnchunk buradan devam eder.
	size_t			chunkParseOffset;
	std::string		chunkedBody;
	// Slowloris tarzi "surekli kucuk parcalar gonderip idle timeout'u hep
	// sifirlama" saldirisina karsi: bu istegin BASLANGICINDAN beri gecen
	// sure, HER read()'de degil SADECE yeni bir istek beklenmeye
	// baslandiginda sifirlanir -- bkz. Server::isClientTimedOut.
	time_t			requestStartTime;
};

#endif
