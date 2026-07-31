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
// Slowloris tarzi saldiri: client istegi TAMAMLAMADAN, surekli kucuk
// parcalar (orn. 1 bayt) gonderip her seferinde idle-timeout'u (yukarida)
// sifirlayarak baglantiyi sonsuza kadar acik tutmaya calisir. Buna karsi
// AYRI bir "mutlak" sinir: bir istegin BASLANGICINDAN (Client::requestStartTime,
// sadece resetForNextRequest'te yenilenir, HER read()'de degil) itibaren
// header'lar bu sure icinde tamamlanmazsa baglanti kapatilir -- kac kucuk
// parca geldigi onemsiz.
#define HEADER_RECEIVE_TIMEOUT_SECONDS 30
// RFC 7230 3.1.1 / 3.2.5: sinirsiz buyuklukte istek satiri/header blogu
// kabul etmek bellek tuketim saldirisina acik kapi birakir -- makul (nginx
// varsayilanlarina yakin) ustsinirlar asilirsa 414/431 ile reddediyoruz.
#define MAX_REQUEST_LINE_LENGTH 8192
#define MAX_HEADER_BLOCK_SIZE 65536
// Graceful shutdown drain suresi -- ServerCgi.cpp'deki CGI_TIMEOUT_SECONDS
// (10s) degerinden KASITLI OLARAK BUYUK: draining sirasinda hala calisan
// mesru bir CGI'nin kendi 10 saniyelik hakkini tam kullanabilmesi icin.
// Bu sure sadece "takilmis/asiri yavas bir client sonsuza kadar kapanmayi
// engellemesin" diye konan bir ust sinir -- CGI'nin kendi timeout'unu
// erkenden ezmemeli.
#define SHUTDOWN_DRAIN_TIMEOUT_SECONDS 12

// Sinyal isleyicileri async-signal-safe olmayan hicbir seyi (cout, new/delete,
// vs.) guvenle cagiramaz -- signal, kodun HERHANGI bir noktasinda araya
// girebilir. Bu yuzden isleyici SADECE bu bayragi set eder; gercek temizlik
// (mesaj yazma, poll() dongusunden cikma, Server::~Server()'in soketleri
// kapatmasi) ana dongude, guvenli bir baglamda yapilir.
static volatile sig_atomic_t g_shutdownRequested = 0;

static void handleShutdownSignal(int signum)
{
	(void)signum;
	g_shutdownRequested = 1;
}

Server::Server(const ListenTable &listenTable)
	: mListenTable(listenTable), mRouter(listenTable)
{
	// CGI child'i erken cikip stdin pipe'imizi kapatirsa, bir sonraki write()
	// varsayilan SIGPIPE davranisiyla TUM sureci oldururdu (subject: program
	// hicbir sekilde crash olmamali). SIG_IGN ile write() bunun yerine
	// sadece -1 doner, zaten mevcut writeClientData/CGI yazma mantigi
	// negatif donusu "kapat" olarak ele aliyor.
	signal(SIGPIPE, SIG_IGN);
	// Ctrl+C (SIGINT) ya da `kill` (SIGTERM) ile gelen kapanma istegini
	// graceful sekilde ele al -- varsayilan davranis (process'i aninda
	// sonlandirmak) Server::~Server()'i hic calistirmaz, acik soketler OS
	// tarafindan zorla kapatilir. Burada onun yerine ana donguden kontrollu
	// cikip normal C++ yikici zincirinin (destructor chain) calismasini
	// sagliyoruz.
	signal(SIGINT, handleShutdownSignal);
	signal(SIGTERM, handleShutdownSignal);
}

Server::~Server()
{
    // Graceful kapanis sirasinda hala calisan bir CGI varsa, ebeveyni (biz)
    // oldukten sonra yetim kalip -- script sonsuz donguyse -- sonsuza kadar
    // calismaya devam ederdi (normalde 10 saniyelik checkCgiTimeouts koruması
    // artik calismiyor, cunku poll() dongusunden zaten ciktik). SIGKILL ile
    // kesin olarak sonlandiriyoruz; reap etmeye gerek yok -- biz de birazdan
    // cikacagimiz icin kalan zombie kaydi init'e (PID 1) miras kalip otomatik
    // reap edilir.
    for (std::map<int, CgiProcess>::iterator it = mCgiProcesses.begin(); it != mCgiProcesses.end(); ++it)
        it->second.terminate();

    for (size_t i = 0; i < mListenSockets.size(); ++i)
        close(mListenSockets[i].fd);

    for (size_t i = 0; i < mPollFds.size(); ++i)
    {
        if (!isListeningSocket(mPollFds[i].fd))
            close(mPollFds[i].fd);
    }
}

// ListenTable::build() zaten isim cozumlemesini ve wildcard-merge kararini
// parse asamasinda verdi (bkz. RealListen); burada sadece rl.ip/rl.port
// (ikisi de Network Byte Order) ile dogrudan sockaddr_in kuruyoruz --
// getaddrinfo'ya tekrar ihtiyac yok.
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
    // accept() zaten karsi tarafin adresini bedavaya verebiliyor (getpeername
    // subject'in izin verdigi fonksiyon listesinde YOK, bu yuzden NULL yerine
    // dogrudan accept()'in kendi out-parametresini kullaniyoruz) -- CGI'ye
    // REMOTE_ADDR olarak vermek icin saklıyoruz.
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

    // Nginx-tarzi wildcard-merge routing icin: baglantinin GERCEKTE hangi
    // local IP'ye geldigini ogreniyoruz (0.0.0.0 degil, gercek hedef IP).
    // ListenTable::resolve() bu adresi kullanacak.
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
	// += (= degil): normalde writeBuffer buraya her cagrildiginda zaten
	// bos olur (onceki yanit writeClientData tarafindan tamamen
	// yazilip erase edilmis olur) -- TEK istisna, "Expect: 100-continue"
	// ara yanitinin (contentLengthCheck) henuz yazilmamis olabilecegi
	// durum. = kullansaydik, govde tek bir read()'de header'la birlikte
	// gelip her sey ayni processClient turunda bitince ara yaniti hic
	// gondermeden nihai yanitla ustune yazardik.
	client.writeBuffer += response;
	client.closeAfterWrite = !client.keepAlive;

	// Keep-alive'da per-request alanlari (headersParsed/request/... ve
	// requestStartTime) hemen sifirliyoruz -- ama readBuffer/writeBuffer'a
	// dokunmuyoruz: pipelining'de readBuffer zaten bir sonraki istegin
	// baytlarini icerebilir, writeBuffer'i da writeClientData yonetiyor.
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
		// Hata sonrasi baglanti HER ZAMAN kapanir: parser/gövde durumu
		// bozulmus olabilir, keep-alive ile devam etmek bir sonraki
		// istegi yanlis yerden okumaya yol acabilir.
		it->second.keepAlive = false;
	}

	sendResponseAndCleanup(fd, HttpResponseBuilder::buildError(statusCode, cfg, false));
}


// client_max_body_size location-seviyesinde override edilebiliyor
// (ConfigParser parse aninda location.hasClientMaxBodySize degilse
// server'dan miras alip location.clientMaxBodySize'a yaziyor -- yani
// LocationConfig::clientMaxBodySize HER ZAMAN dogru EFEKTIF deger).
// Ama Host/path henuz sadece SERVER'i belirliyor (matchedConfig) -- gercek
// govde sinirini dogru uygulamak icin LOCATION'i da simdiden (finalizeRequest
// beklemeden) coz"up onun degerini kullanmaliyiz, yoksa her zaman server'in
// (location override'ini yok sayan) degeri kullanilirdi.
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
		// atol() gecersiz/rakam-disi bir deger icin sessizce 0 dondurur --
		// bu, gercekte "govde yok" ile "Content-Length bozuk" durumlarini
		// ayirt edilemez hale getirirdi. Once StringUtils::isNumeric ile
		// dogruluyoruz, degilse 400 ile reddediyoruz.
		if (!StringUtils::isNumeric(clHeader))
		{
			sendErrorAndCleanup(fd, 400);
			return (false);
		}
		client.contentLength = static_cast<size_t>(std::atol(clHeader.c_str()));
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

	// NOT: "upload_dir yoksa govdeyi hic okumadan erken cevap ver" seklinde
	// bir kisayol denendi ama geri alindi -- govde tamamen gelmeden baglantiyi
	// kapatmak (close()), istemci hala yaziyorsa TCP'nin RST gondermesine ve
	// bizim cevabimizin kaybolmasina yol acabiliyor (bkz. shutdown() ile
	// yari-kapatma tartismasi). Bunu guvenli yapmak icin shutdown(SHUT_WR)
	// + kalan govdeyi okuyup atma gerekir -- bu ayrica, dikkatlice
	// tasarlanmasi gereken bir iyilestirme, buraya aceleyle yamanmadi.
	// Simdilik govde her zaman tamamen okunuyor, RequestHandler::handlePost
	// upload_dir bossa 200 donuyor (asagida govde okunduktan sonra).

	// RFC 7231 6.5.10 / RFC 7230 5.4: sadece HTTP/1.1'de zorunlu -- gercek
	// istemciler (tarayici/curl) her zaman Host gonderir, bu kontrol sadece
	// eksik/bozuk istekleri yakalar, normal trafigi etkilemez.
	if (client.request.getVersion() == "HTTP/1.1" && client.request.getHeader("Host").empty())
	{
		sendErrorAndCleanup(fd, 400);
		return (false);
	}

	// RFC 7231 6.2.1: govde bekleniyorsa ve istemci "Expect: 100-continue"
	// istediyse, govdeyi okumaya baslamadan once ara "100 Continue" yaniti
	// gonderiyoruz. closeAfterWrite'i BILEREK false'a sabitliyoruz --
	// sendResponseAndCleanup nihai yanitta bunu keepAlive'a gore zaten
	// tekrar set edecek, ama bu ARA yanit hicbir sekilde baglantiyi
	// kapatmamali (writeClientData writeBuffer bosalinca closeAfterWrite'a
	// bakiyor, varsayilan deger true oldugu icin buraya dokunmazsak
	// govdeyi beklemeden baglanti kapanirdi).
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

// KADEMELI (incremental) chunked-decode: client.chunkParseOffset, readBuffer
// icinde SON DOGRULANMIS chunk sinirini tutar -- her cagri baştan degil,
// oradan devam eder. client.chunkedBody da ana kadar decode edilmis govdeyi
// biriktirir. Bu sayede N bayt'lik chunked govde toplamda O(N) maliyetle
// islenir (eskiden her yeni parca geldiginde BASTAN taranip O(N^2)'ye
// cikiyordu -- gercek olcum: 20MB chunked govde ~20 saniye surdu, ayni
// boyut Content-Length ile <1.5 saniyede bitiyordu).
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
			// raw'da chunked govde TAM olarak [0, dataStart+2) araligini
			// kapliyor -- bundan SONRAKI baytlar (varsa) pipelined bir
			// sonraki istektir, caller bunlari mClients[fd].readBuffer'da
			// koruyabilsin diye consumedLength'i bildiriyoruz.
			consumedLength = dataStart + 2;
			return (CHUNK_COMPLETE);
		}

		if (dataStart + chunkSize + 2 > raw.size())
			return (CHUNK_INCOMPLETE);
		if (raw.compare(dataStart + chunkSize, 2, "\r\n") != 0)
			return (CHUNK_INVALID);

		client.chunkedBody.append(raw, dataStart, chunkSize);
		pos = dataStart + chunkSize + 2;
		// Bu chunk TAMAMEN dogrulandi/decode edildi -- ilerlemeyi kaydet ki
		// bir sonraki cagri (daha fazla bayt gelince) buradan devam etsin,
		// bu chunk'i tekrar taramasin.
		client.chunkParseOffset = pos;
	}
}


ServerConfig *Server::selectServerConfig(int fd)
{
	std::map<int, Client>::iterator cIt = mClients.find(fd);
	if (cIt == mClients.end() || !cIt->second.listenSocket)
		return (NULL);

	Client &client = cIt->second;

	return mRouter.selectServerConfig(client.listenSocket->port, client.localIp,
			client.request.getHeader("Host"));
}


// GET/POST/DELETE'in dosya sistemi mantigi RequestHandler'da (bkz.
// includes/RequestHandler.hpp) -- burasi sadece fd/mClients'e bagli
// sendResponseAndCleanup/sendErrorAndCleanup secimini yapan glue.
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

	// loc->cgiExtension anahtarlari nokta ile basliyor (ConfigParser bunu
	// parse aninda garanti ediyor), bu yuzden substr(dot) (nokta dahil) ile
	// karsilastiriyoruz.
	std::string ext = path.substr(dot);
	return (loc->cgiExtension.find(ext) != loc->cgiExtension.end());
}

// cgiHandle implementasyonu sources/ServerCgi.cpp icinde.

void Server::controlMethod(Client &client, LocationConfig *loc, int fd)
{
	if (isCgiRequest(loc, client.request.getPath()))
	{
		cgiHandle(client, loc, fd);
		return;
	}

	if (client.request.getMethod() == "GET")
	{
		getHandle(client, loc, fd);
		return;
	}

	if (client.request.getMethod() == "POST")
	{
		postHandle(client, loc, fd);
		return;
	}

	if (client.request.getMethod() == "DELETE")
	{
		deleteHandle(client, loc, fd);
		return;
	}

	std::string body = "<html><body><h1>It works!</h1><p>matched location: " + loc->path + "</p></body></html>";
	sendResponseAndCleanup(fd, HttpResponseBuilder::build(200, HttpUtils::statusText(200), body, client.keepAlive));
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
	// Bu client icin zaten calisan bir CGI varsa (fork edilip pipe'lari
	// poll()'a eklenmis, henuz bitmemis), gelen yeni baytlari sadece
	// readBuffer'da bekletiyoruz -- ayni istegi tekrar finalizeRequest'e
	// sokmuyoruz. CGI bitince finalizeCgi cevabi gonderecek; pipelining ile
	// gelmis bir sonraki istek varsa o da (keep-alive'da oldugu gibi)
	// response yazildiktan sonra islenir.
	if (mCgiProcesses.find(fd) != mCgiProcesses.end())
		return;

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
			// Su ana kadar dogrulanmis chunk'lar icin client_max_body_size'i
			// ERKEN kontrol ediyoruz -- yoksa devasa bir chunked govde,
			// terminating 0-chunk'a kadar hicbir sinira takilmadan sonsuza
			// kadar biriktirilebilirdi.
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

		// Ham (chunked-encoded) baytlari decode edilmis govde ile
		// degistiriyoruz; tuketilen kisimdan SONRAKI (pipelined) baytlar
		// buffer'da oldugu gibi kaliyor. contentLength'i de govde
		// uzunluguna esitliyoruz ki asagidaki erase + handler'lardaki
		// (postHandle) substr(0, contentLength) chunked/non-chunked
		// ayrimi yapmadan ayni sekilde calissin.
		client.readBuffer.replace(0, consumedLength, client.chunkedBody);
		client.contentLength = client.chunkedBody.size();
	}
	else if (!isBodyComplete(fd))
	{
		return;
	}

	size_t bodyLength = client.contentLength;
	finalizeRequest(fd);
	// finalizeRequest -> ... -> sendResponseAndCleanup keep-alive ise
	// client'in per-request alanlarini (resetForNextRequest ile YERINDE,
	// silmeden) zaten sifirlamis olabilir -- bu yuzden govde uzunlugunu
	// yukarida LOKAL degiskene aldik. `client` referansi hala gecerli:
	// closeClient bu senkron cagri zincirinin HICBIR yerinden cagrilmiyor
	// (sadece ana poll() dongusunden), std::map da mevcut bir eleman icin
	// baska eleman eklenmesinden etkilenmez.
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

	// Response tamamen yazildi.
	bool shouldClose = client.closeAfterWrite;

	if (shouldClose)
		return true;

	// Keep-alive: baglanti acik kaliyor. readBuffer'da zaten (pipelining
	// ile) tam bir sonraki istek bekliyor olabilir -- poll() zaten okunmus
	// baytlar icin tekrar sinyal vermeyecegi icin hemen islemeyi deniyoruz.
	// Tam istek yoksa processClient hicbir sey yapmadan doner, bir sonraki
	// POLLIN'i bekleriz.
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

	// Client baglantisi CGI hala calisirken kapaniyorsa (erken disconnect,
	// timeout vb.) yetim kalacak CGI surecini/pipe'larini da temizliyoruz --
	// yoksa zombie process + sizan fd. abortCgi kendi pipe pollfd
	// girislerini fd DEGERINE gore siler (removePollFd), bu yuzden asagida
	// client'in kendi girisini de index yerine fd degeriyle siliyoruz --
	// abortCgi arada baska bir indeksi silmis olsa bile guvenli.
	if (mCgiProcesses.find(fd) != mCgiProcesses.end())
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

	// CGI'nin stdout'undan (bizim okuma ucumuz) veri okunabilir hale geldi.
	if (mCgiStdoutToClient.find(fd) != mCgiStdoutToClient.end())
		return (handleCgiStdoutReadable(i));

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

	// CGI'nin stdin'ine (bizim yazma ucumuz) yazilabilir hale geldi.
	if (mCgiStdinToClient.find(fd) != mCgiStdinToClient.end())
		return (handleCgiStdinWritable(i));

	bool shouldClose = writeClientData(fd);
	if (shouldClose)
	{
		closeClient(i);
		return(true);
	}

	// writeClientData icinde pipelining ile hemen yeni bir yanit
	// kuyruklanmis olabilir (POLLOUT acik kalmali) ya da yazilacak bir sey
	// kalmamis olabilir (POLLOUT kapatilmali, yoksa poll() bos yere
	// tekrar tekrar POLLOUT dondurup CPU'yu mesgul eder).
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

	// Slowloris savunmasi: istek daha tamamlanmadiysa (headers henuz
	// parse edilmediyse) VE istegin baslangicindan beri HEADER_RECEIVE_
	// TIMEOUT_SECONDS'dan fazla gectiyse, ne kadar kucuk parcalar halinde
	// veri gelmis olursa olsun (lastActivity'yi surekli tazeleseler bile)
	// baglantiyi kapatiyoruz.
	if (!it->second.headersParsed
		&& (now - it->second.requestStartTime > HEADER_RECEIVE_TIMEOUT_SECONDS))
		return true;

	return false;
}
void Server::listeningSockets()
{
	// Drain fazi henuz baslamadi (draining=false): normal calisma. Sinyal
	// gelince (draining=true) YENI baglanti kabul etmeyi hemen durduruyoruz
	// (dinleme soketlerini kapatip mPollFds'ten cikariyoruz -- yeni istemciler
	// "connection refused" alir, backlog'da askida kalmazlar), ama MEVCUT
	// client'larin/CGI'lerin islerini normal akisla (ayni isPollin/isPollout/
	// checkCgiTimeouts fonksiyonlariyla) bitirmesine izin veriyoruz.
	// SHUTDOWN_DRAIN_TIMEOUT_SECONDS, CGI_TIMEOUT_SECONDS'tan (ServerCgi.cpp,
	// 10s) BUYUK secilmeli -- yoksa hala kendi 10s hakkini kullanmakta olan
	// mesru bir CGI, drain suresi dolunca erken zorla kesilirdi. Bu sadece
	// "takilmis/asiri yavas bir client sonsuza kadar kapanmayi engellemesin"
	// diye konan bir GUVENLIK AGI -- CGI'nin kendi timeout'unu ezmemeli.
	bool draining = false;
	time_t drainStart = 0;

	for (;;)
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

		if (draining && mClients.empty() && mCgiProcesses.empty())
			break;
		if (draining && (time(NULL) - drainStart >= SHUTDOWN_DRAIN_TIMEOUT_SECONDS))
		{
			std::cout << "webserv: drain timeout reached, forcing shutdown "
				"of remaining connection(s)..." << std::endl;
			break;
		}
		if (mPollFds.empty())
			break;

		int ready;

		ready = poll(&mPollFds[0], mPollFds.size(), POLL_TIMEOUT_MS);
		if (ready == -1)
		{
			// EINTR SIGINT/SIGTERM'in kendisi yuzunden de olusmus olabilir --
			// bayragi kontrol etmeden direkt continue edersek bir sonraki
			// dongu basindaki kontroller zaten yakalayacak, o yuzden burada
			// ekstra kontrole gerek yok, sadece poll()'u guvenle tekrarliyoruz.
			if (errno == EINTR)
				continue;
			throw std::runtime_error(std::string("poll: ") + strerror(errno));
		}
		checkCgiTimeouts();

		size_t i = 0;
		while (i < mPollFds.size())
		{
			bool erased = false;
			// POLLHUP/POLLERR de POLLIN ile birlikte kontrol ediliyor: bir
			// pipe/soket tamamen bosalip karsi taraf kapandiginda kernel
			// bazen SADECE POLLHUP doner (POLLIN'siz) -- read() cagirip
			// EOF'u (0 donusu) tespit etmemiz gerekiyor, yoksa bu fd hicbir
			// zaman islenmeden "hazir" gorunmeye devam eder ve poll()
			// surekli aninda doner (CPU'yu bosuna mesgul eden busy-spin).
			if (mPollFds[i].revents & (POLLIN | POLLHUP | POLLERR))
				erased = isPollin(i);
			if (!erased && (mPollFds[i].revents & POLLOUT))
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
