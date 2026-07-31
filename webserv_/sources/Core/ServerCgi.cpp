#include "Server.hpp"
#include "NetUtils.hpp"
#include "StringUtils.hpp"
#include "HttpUtils.hpp"
#include "HttpResponseBuilder.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sstream>

#define CGI_TIMEOUT_SECONDS 10

// "Content-Type" -> "HTTP_CONTENT_TYPE" (CGI/1.1 header->env donusumu).
// Bu dosyaya ozel bir kural (sadece CGI env insasinda kullanilir), ama
// asil buyuk-harfe cevirme islemi StringUtils::toUpper'a devrediliyor.
static std::string headerToEnvName(const std::string &headerName)
{
	std::string result = headerName;

	for (size_t i = 0; i < result.size(); ++i)
	{
		if (result[i] == '-')
			result[i] = '_';
	}
	return ("HTTP_" + StringUtils::toUpper(result));
}

// ------------------------------------------------------------------------
// giris noktasi: controlMethod buraya yonlendiriyor
// ------------------------------------------------------------------------

void Server::cgiHandle(Client &client, LocationConfig *loc, int fd)
{
	const std::string &requestPath = client.request.getPath();
	size_t dot = requestPath.find_last_of('.');
	std::string ext = (dot == std::string::npos) ? "" : requestPath.substr(dot);

	std::map<std::string, std::string>::const_iterator it = loc->cgiExtension.find(ext);
	if (it == loc->cgiExtension.end())
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	std::string interpreterPath = it->second;
	std::string scriptPath = RequestHandler::resolveFilePath(loc, requestPath);

	// scriptPath'in DISKTE gercekten var olmasini KASITLI OLARAK sart
	// kosmuyoruz: ".py" gibi extension'larda interpreter+script ikilisi
	// (python3 hello.py) script'in var olmasini gerektirir, ama bagimsiz
	// calisan bir CGI programi (orn. subject testerindeki cgi_tester) hic
	// var olmayan bir PATH_INFO ile de gecerli sekilde calisabilir --
	// config'te bu ikisini ayirt eden bir bayrak yok, ve nginx/Apache gibi
	// sunucular da CGI'yi calistirmayi ONCEDEN dosya kontrolu yapmadan
	// dener. Script/interpreter gercekten calismazsa zaten finalizeCgi'nin
	// splitHeaderBody kontrolu bos/bozuk ciktiyi yakalayip 502 doner --
	// ayri bir on-kontrole gerek yok. joinPath ("..") traversal'i zaten
	// resolveFilePath icinde reddedip scriptPath'i "" yapiyor, o kontrolu
	// koruyoruz.
	if (scriptPath.empty())
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	startCgi(client, loc, fd, interpreterPath, scriptPath);
}

// ------------------------------------------------------------------------
// environment (subject: "full request and arguments must be available to CGI")
// ------------------------------------------------------------------------

std::vector<std::string> Server::buildCgiEnv(Client &client, LocationConfig *loc,
		int fd, const std::string &scriptPath)
{
	(void)fd;
	std::vector<std::string> env;
	std::ostringstream oss;

	(void)loc;
	env.push_back("REQUEST_METHOD=" + client.request.getMethod());
	// RFC 3875 4.1.13: script AYRI bir isimle tanimlanamiyorsa (bizim
	// eslesmemiz her zaman TAM istek path'ine kadar oldugu icin ayri bir
	// "script-name / path-info" bolunmesi yapmiyoruz) SCRIPT_NAME'in bos
	// olmasina spec izin veriyor -- bu durumda TUM path PATH_INFO'ya gecer
	// (implementation-defined politika, spec'e uygun). Bu politikayi,
	// subject'in resmi test aracinin (tests/tester + tests/cgi_tester)
	// beklentisiyle ayni tutuyoruz.
	env.push_back("SCRIPT_NAME=");
	env.push_back("SCRIPT_FILENAME=" + scriptPath);
	env.push_back("PATH_INFO=" + client.request.getPath());
	env.push_back("PATH_TRANSLATED=" + scriptPath);
	env.push_back("REQUEST_URI=" + client.request.getPath()
		+ (client.request.getQuery().empty() ? "" : "?" + client.request.getQuery()));
	env.push_back("QUERY_STRING=" + client.request.getQuery());
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	// php-cgi bu degisken olmadan (guvenlik amacli) calismayi reddeder;
	// python script'leri icin zararsiz.
	env.push_back("REDIRECT_STATUS=200");
	env.push_back("SERVER_SOFTWARE=webserv/1.0");
	env.push_back("PATH=/usr/bin:/bin");

	oss << client.contentLength;
	env.push_back("CONTENT_LENGTH=" + oss.str());

	std::string contentType = client.request.getHeader("Content-Type");
	if (!contentType.empty())
		env.push_back("CONTENT_TYPE=" + contentType);

	if (client.matchedConfig)
	{
		std::string name = client.matchedConfig->serverName.empty() ? "localhost" : client.matchedConfig->serverName;
		env.push_back("SERVER_NAME=" + name);
	}

	if (client.listenSocket)
	{
		std::ostringstream portOss;
		portOss << ntohs(client.listenSocket->port);
		env.push_back("SERVER_PORT=" + portOss.str());
	}

	env.push_back("REMOTE_ADDR=" + NetUtils::ipToString(client.remoteIp));

	const std::map<std::string, std::string> &headers = client.request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
			it != headers.end(); ++it)
	{
		// Content-Length/Content-Type zaten CGI/1.1'in kendi ozel
		// degiskenleri olarak yukarida verildi, HTTP_* olarak tekrarlamiyoruz.
		if (it->first == "Content-Length" || it->first == "Content-Type")
			continue;
		env.push_back(headerToEnvName(it->first) + "=" + it->second);
	}

	return (env);
}

// ------------------------------------------------------------------------
// fork + pipe + execve
// ------------------------------------------------------------------------

void Server::startCgi(Client &client, LocationConfig *loc, int fd,
		const std::string &interpreterPath, const std::string &scriptPath)
{
	std::vector<std::string> envStrings = buildCgiEnv(client, loc, fd, scriptPath);

	// Govde (chunked bile olsa processClient tarafindan ONCEDEN tam
	// decode edilip bellekte hazir) -- oldugu gibi CGI'nin stdin'ine
	// yaziyoruz, subject'in "CGI EOF'u govde sonu sayar" kuralina uygun
	// olarak yazma bitince pipe'i kapatip EOF gonderecegiz.
	std::string body = client.readBuffer.substr(0, client.contentLength);

	CgiProcess proc;
	if (!proc.start(interpreterPath, scriptPath, envStrings, body, client.keepAlive))
	{
		sendErrorAndCleanup(fd, 500);
		return;
	}

	struct pollfd stdoutPfd;
	stdoutPfd.fd = proc.stdoutFd();
	stdoutPfd.events = POLLIN;
	stdoutPfd.revents = 0;
	mPollFds.push_back(stdoutPfd);
	mCgiStdoutToClient[proc.stdoutFd()] = fd;

	if (proc.stdinFd() != -1)
	{
		struct pollfd stdinPfd;
		stdinPfd.fd = proc.stdinFd();
		stdinPfd.events = POLLOUT;
		stdinPfd.revents = 0;
		mPollFds.push_back(stdinPfd);
		mCgiStdinToClient[proc.stdinFd()] = fd;
	}

	mCgiProcesses[fd] = proc;
}

// ------------------------------------------------------------------------
// poll() dispatch: stdin yazilabilir / stdout okunabilir
// ------------------------------------------------------------------------

bool Server::handleCgiStdinWritable(size_t i)
{
	int fd = mPollFds[i].fd;
	int clientFd = mCgiStdinToClient[fd];

	std::map<int, CgiProcess>::iterator procIt = mCgiProcesses.find(clientFd);
	if (procIt == mCgiProcesses.end())
	{
		close(fd);
		mCgiStdinToClient.erase(fd);
		removePollFd(fd);
		return (true);
	}

	// writeStdin() basarili/basarisiz her iki durumda da pipe'i kendi
	// kapatir -- burada sadece Server'a ozel poll()/lookup temizligi kaldi.
	if (!procIt->second.writeStdin())
		return (false);

	mCgiStdinToClient.erase(fd);
	removePollFd(fd);
	return (true);
}

bool Server::handleCgiStdoutReadable(size_t i)
{
	int fd = mPollFds[i].fd;
	int clientFd = mCgiStdoutToClient[fd];

	std::map<int, CgiProcess>::iterator procIt = mCgiProcesses.find(clientFd);
	if (procIt == mCgiProcesses.end())
	{
		close(fd);
		mCgiStdoutToClient.erase(fd);
		removePollFd(fd);
		return (true);
	}

	// readStdout() basari/EOF/hata farketmeksizin gerektiginde pipe'i kendi
	// kapatir; sadece "bitti mi" bilgisini doner.
	if (!procIt->second.readStdout())
		return (false);

	mCgiStdoutToClient.erase(fd);
	removePollFd(fd);

	finalizeCgi(clientFd);
	return (true);
}

// ------------------------------------------------------------------------
// tamamlanma: reap + CGI ciktisini HTTP yanitina cevir
// ------------------------------------------------------------------------

void Server::finalizeCgi(int clientFd)
{
	std::map<int, CgiProcess>::iterator procIt = mCgiProcesses.find(clientFd);
	if (procIt == mCgiProcesses.end())
		return;

	CgiProcess proc = procIt->second;

	// stdout kapandi ama stdin hala aciksa (CGI govdeyi tam okumadan
	// cikmis olabilir) artik yazmanin anlami yok.
	int leftoverStdinFd = proc.stdinFd();
	if (leftoverStdinFd != -1)
	{
		proc.closeStdin();
		mCgiStdinToClient.erase(leftoverStdinFd);
		removePollFd(leftoverStdinFd);
	}

	mCgiProcesses.erase(clientFd);

	// Asla BLOCKING waitpid cagirmiyoruz (tek poll() disinda beklemek yok).
	// Hemen reap olmazsa pid, checkCgiTimeouts()'un her turda tekrar
	// deneyecegi bekleme listesine eklenir.
	int status = 0;
	if (waitpid(proc.pid(), &status, WNOHANG) <= 0)
		mPendingReap.push_back(proc.pid());

	// Client bu arada baglantiyi kapatmis olabilir (normalde closeClient
	// zaten abortCgi ile buraya hic gelmeden temizler, ama savunma amacli).
	if (mClients.find(clientFd) == mClients.end())
		return;

	std::string headerBlock;
	std::string body;
	int statusCode = 200;
	std::string statusText = "OK";
	std::string contentType = "text/html";

	if (!HttpUtils::splitHeaderBody(proc.outputBuffer(), headerBlock, body))
	{
		sendErrorAndCleanup(clientFd, 502);
		ensurePollout(clientFd);
		return;
	}

	std::istringstream headerStream(headerBlock);
	std::string line;
	while (std::getline(headerStream, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			continue;

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string value = StringUtils::trimLeadingSpaces(line.substr(colon + 1));

		if (key == "Status")
		{
			int parsedCode = statusCode;
			std::istringstream ss(value);
			if (ss >> parsedCode)
				statusCode = parsedCode;
			size_t sp = value.find(' ');
			statusText = (sp == std::string::npos) ? "OK" : value.substr(sp + 1);
		}
		else if (key == "Content-Type")
			contentType = value;
	}

	sendResponseAndCleanup(clientFd, HttpResponseBuilder::build(statusCode, statusText, body, proc.keepAlive(), contentType));
	ensurePollout(clientFd);
}

// ------------------------------------------------------------------------
// timeout + erken-kapanan-client temizligi
// ------------------------------------------------------------------------

void Server::checkCgiTimeouts()
{
	// Bekleyen reap'leri (blocking olmadan) tekrar dene.
	for (size_t i = 0; i < mPendingReap.size(); )
	{
		int status = 0;
		if (waitpid(mPendingReap[i], &status, WNOHANG) > 0)
			mPendingReap.erase(mPendingReap.begin() + i);
		else
			++i;
	}

	time_t now = time(NULL);
	std::vector<int> timedOut;

	for (std::map<int, CgiProcess>::iterator it = mCgiProcesses.begin(); it != mCgiProcesses.end(); ++it)
	{
		if (now - it->second.startTime() > CGI_TIMEOUT_SECONDS)
			timedOut.push_back(it->first);
	}

	for (size_t i = 0; i < timedOut.size(); ++i)
	{
		int clientFd = timedOut[i];
		std::map<int, CgiProcess>::iterator it = mCgiProcesses.find(clientFd);
		if (it == mCgiProcesses.end())
			continue;

		CgiProcess &proc = it->second;
		int stdinFd = proc.stdinFd();
		int stdoutFd = proc.stdoutFd();

		// terminate() SIGKILL gonderir ve kalan pipe fd'lerini kendi kapatir;
		// Server burada sadece kendine ozel poll()/lookup temizligini yapar.
		proc.terminate();
		mPendingReap.push_back(proc.pid());

		if (stdinFd != -1)
		{
			mCgiStdinToClient.erase(stdinFd);
			removePollFd(stdinFd);
		}
		mCgiStdoutToClient.erase(stdoutFd);
		removePollFd(stdoutFd);

		mCgiProcesses.erase(it);

		if (mClients.find(clientFd) != mClients.end())
		{
			sendErrorAndCleanup(clientFd, 504);
			ensurePollout(clientFd);
		}
	}
}

// closeClient(), fd disconnect edildiginde ve o fd'nin hala calisan bir
// CGI'si varsa bunu cagirir: sureci oldur, pipe'lari kapat, hicbir yanit
// gondermeye CALISMA (client zaten gidiyor).
void Server::abortCgi(int clientFd)
{
	std::map<int, CgiProcess>::iterator it = mCgiProcesses.find(clientFd);
	if (it == mCgiProcesses.end())
		return;

	CgiProcess &proc = it->second;
	int stdinFd = proc.stdinFd();
	int stdoutFd = proc.stdoutFd();

	proc.terminate();
	mPendingReap.push_back(proc.pid());

	if (stdinFd != -1)
	{
		mCgiStdinToClient.erase(stdinFd);
		removePollFd(stdinFd);
	}
	mCgiStdoutToClient.erase(stdoutFd);
	removePollFd(stdoutFd);

	mCgiProcesses.erase(it);
}
