// CGI implementasyonu -- Server.hpp'de bildirilen Server::isCgiRequest,
// Server::cgiHandle ve poll-driven CGI pipe yönetimi (isCgiPipe,
// handleCgiPipeEvent, closeCgiFds, removePipeFromPoll,
// setClientPollEvents) burada tanımlı. Server sınıfının üyesi
// olmayan CGI-özel sabit/tip (CGI_TIMEOUT_SECONDS, CgiOutput) cgi.hpp'de.
// (isCgiRequest'in kendisi Server.cpp'de -- routing'in geri kalanıyla
// aynı yerde, map tabanlı cgi_extension lookup'ı olarak.)
#include "Server.hpp"
#include "cgi.hpp"
#include "RequestHandler.hpp"
#include "HttpResponseBuilder.hpp"

#include <netinet/in.h>	// ntohs (SERVER_PORT için)
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <csignal>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <ctime>
// cerrno kasıtlı olarak yok: subject read/write sonrası errno'ya bakmayı
// yasaklıyor, o yüzden burada hiç kullanmıyoruz.

// --- cgiHandle'ın kullandığı yardımcılar ---

// Uzantıya göre interpreter seçer; bilinmeyen uzantılarda script'in kendi
// shebang'ıyla doğrudan çalıştırılmasına düşer (false döner). Config'te
// (cgi_extension .py /usr/bin/python3 gibi) interpreter verilmişse o
// öncelikli -- bu fonksiyon sadece config değeri boşsa devreye girer.
static bool resolveInterpreter(const std::string &ext, std::string &interpreter)
{
	if (ext == ".py") { interpreter = "python3"; return true; }
	if (ext == ".pl") { interpreter = "perl"; return true; }
	return false;
}

// SERVER_PORT/CONTENT_LENGTH gibi sayısal env değerlerini string'e çevirmek
// için küçük bir yardımcı (atoi'nin tersi, sprintf yerine ostringstream).
static std::string numToString(long value)
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

// HTTP header adını CGI'nin beklediği HTTP_* env-variable formatına çevirir
// (örn. "User-Agent" -> "HTTP_USER_AGENT").
static std::string toHttpEnvName(const std::string &headerName)
{
	std::string out = "HTTP_";
	for (size_t i = 0; i < headerName.size(); ++i)
	{
		char c = headerName[i];
		out += (c == '-') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return out;
}

// envp dizisine tek bir "KEY=value" satırı eklemek için tekrar eden kodu
// tekilleştirir (buildCgiEnv aşağıda defalarca çağırıyor).
static void addEnv(std::vector<std::string> &env, const std::string &key, const std::string &value)
{
	env.push_back(key + "=" + value);
}

// CGI/1.1 meta-variable'ları kurar: script'e giden tüm bağlam (method, query,
// header'lar) environment üzerinden taşınır, body stdin pipe'ından gider.
static void buildCgiEnv(const HttpRequest &req, ServerConfig *cfg, const std::string &scriptPath,
						size_t bodySize, std::vector<std::string> &env)
{
	// PATH olmadan hem "/usr/bin/env <interpreter>" hem script'in kendi
	// "#!/usr/bin/env ..." shebang'ı sadece env'in built-in fallback yoluna
	// (genelde /usr/bin:/bin) düşer -- nvm ile kurulu node, homebrew'daki
	// bazı araçlar orada olmaz. Webserv'i başlatan shell'in kendi PATH'ini
	// aynen CGI'ye de veriyoruz.
	const char *pathEnv = std::getenv("PATH");
	addEnv(env, "PATH", pathEnv ? pathEnv : "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");

	addEnv(env, "GATEWAY_INTERFACE", "CGI/1.1");
	addEnv(env, "SERVER_PROTOCOL", req.getVersion().empty() ? "HTTP/1.1" : req.getVersion());
	addEnv(env, "REQUEST_METHOD", req.getMethod());
	addEnv(env, "SCRIPT_NAME", req.getPath());
	addEnv(env, "SCRIPT_FILENAME", scriptPath);
	addEnv(env, "PATH_INFO", req.getPath());
	addEnv(env, "PATH_TRANSLATED", scriptPath);
	addEnv(env, "QUERY_STRING", req.getQuery());
	addEnv(env, "REDIRECT_STATUS", "200");
	addEnv(env, "CONTENT_LENGTH", numToString(static_cast<long>(bodySize)));
	addEnv(env, "CONTENT_TYPE", req.getHeader("Content-Type"));

	if (cfg)
	{
		addEnv(env, "SERVER_NAME", cfg->hasServerName ? cfg->serverName : "localhost");
		if (!cfg->listens.empty())
			addEnv(env, "SERVER_PORT", numToString(ntohs(cfg->listens[0].port)));
	}

	const std::map<std::string, std::string> &headers = req.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	{
		if (it->first == "Content-Type" || it->first == "Content-Length")
			continue;
		addEnv(env, toHttpEnvName(it->first), it->second);
	}
}

// CgiOutput struct'ı cgi.hpp'de (Server.hpp/ConfigParser.hpp ile aynı
// .hpp/.cpp eşleşme mantığı) -- ctor'u burada tanımlıyoruz.
CgiOutput::CgiOutput() : statusCode(200), statusText("OK"), contentType("text/html")
{
}

// CGI script'inin stdout'una yazdığı "header'lar\r\n\r\nbody" çıktısını ayrıştırır.
// Status/Content-Type/Location/Set-Cookie dışındaki header'lar yok sayılır.
static void parseCgiOutput(const std::string &raw, CgiOutput &out)
{
	size_t sep = raw.find("\r\n\r\n");
	size_t sepLen = 4;
	if (sep == std::string::npos)
	{
		sep = raw.find("\n\n");
		sepLen = 2;
	}

	std::string headerBlock = (sep == std::string::npos) ? "" : raw.substr(0, sep);
	out.body = (sep == std::string::npos) ? raw : raw.substr(sep + sepLen);

	std::istringstream headerStream(headerBlock);
	std::string line;
	while (std::getline(headerStream, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string name = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		size_t valueStart = value.find_first_not_of(' ');
		value = (valueStart == std::string::npos) ? "" : value.substr(valueStart);

		if (name == "Status")
		{
			out.statusCode = std::atoi(value.c_str());
			size_t sp = value.find(' ');
			if (sp != std::string::npos)
				out.statusText = value.substr(sp + 1);
		}
		else if (name == "Content-Type")
			out.contentType = value;
		else if (name == "Location")
			out.location = value;
		else if (name == "Set-Cookie")
			out.setCookie = value;
	}
}

// HttpResponseBuilder::build Set-Cookie diye bir parametre bilmiyor; onu
// değiştirmek yerine, zaten kurulmuş header'lı response string'inin son
// boş satırından (header/body ayracı) hemen önce Set-Cookie satırını
// araya sokuyoruz. Cookie yoksa response aynen döner.
static std::string insertSetCookie(const std::string &response, const std::string &cookie)
{
	if (cookie.empty())
		return response;
	size_t headerEnd = response.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return response;
	return response.substr(0, headerEnd) + "\r\nSet-Cookie: " + cookie + response.substr(headerEnd);
}

// Server.cpp'deki createListenSocket ile aynı kalıp: düşük seviye OS işini
// (burada: iki pipe açma) ayrı, sade bir fonksiyona koyup çağıran tarafın
// sadece başarı/başarısızlığa bakmasını sağlıyor.
static bool openCgiPipes(int inPipe[2], int outPipe[2])
{
	if (pipe(inPipe) == -1)
		return false;
	if (pipe(outPipe) == -1)
	{
		close(inPipe[0]);
		close(inPipe[1]);
		return false;
	}
	return true;
}

// fork() sonrası child'ın YAPTIĞI HER ŞEY burada: pipe'ları stdin/stdout'a
// bağlama, doğru dizine geçme, script'i çalıştırma. Bu fonksiyon normal
// şartlarda hiç geri dönmez (execve başarılıysa process image değişir,
// başarısızsa _exit(1) ile biter).
static void execCgiChild(int inPipe[2], int outPipe[2], bool useInterpreter,
						const std::string &interpreter, const std::string &scriptDir,
						const std::string &scriptFile, char **envp)
{
	dup2(inPipe[0], STDIN_FILENO);
	dup2(outPipe[1], STDOUT_FILENO);
	close(inPipe[0]); close(inPipe[1]);
	close(outPipe[0]); close(outPipe[1]);

	// Subject: "The CGI should be run in the correct directory for
	// relative path file access." chdir() izinli fonksiyon -- script'in
	// kendi dizinine geçip exec'i o dizine göre relatif dosya adıyla
	// yapıyoruz (scriptDir/scriptFile cgiHandle'da, buildCgiEnv'den önce
	// hesaplandı -- SCRIPT_FILENAME de aynı scriptFile'ı kullanıyor,
	// ikisi tutarlı).
	if (chdir(scriptDir.c_str()) != 0)
		_exit(1);

	if (useInterpreter)
	{
		char *argv[] = { const_cast<char *>("env"), const_cast<char *>(interpreter.c_str()),
						  const_cast<char *>(scriptFile.c_str()), NULL };
		execve("/usr/bin/env", argv, envp);
	}
	else
	{
		char *argv[] = { const_cast<char *>(scriptFile.c_str()), NULL };
		execve(scriptFile.c_str(), argv, envp);
	}
	_exit(1);
}

// fork() sonrası PARENT'ın yaptığı her şey: kullanılmayan pipe uçlarını
// kapatma, kalanları non-blocking yapma, CgiSession'ı kaydedip ana poll()
// döngüsüne (mPollFds) ekleme. Buradan sonrası artık blocking değil --
// gerçek okuma/yazma handleCgiPipeEvent'te olacak.
void Server::registerCgiSession(pid_t pid, int fd, int inPipe[2], int outPipe[2], const std::string &body)
{
	close(inPipe[0]);
	close(outPipe[1]);
	fcntl(inPipe[1], F_SETFL, O_NONBLOCK);
	fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

	CgiSession session;
	session.pid      = pid;
	session.clientFd = fd;
	session.inFd     = inPipe[1];
	session.outFd    = outPipe[0];
	session.body     = body;
	session.written  = 0;
	session.start    = time(NULL);

	mCgiSessions[session.outFd] = session;
	mCgiPipeOwner[session.outFd] = session.outFd;

	struct pollfd outPfd;
	outPfd.fd = session.outFd;
	outPfd.events = POLLIN;
	outPfd.revents = 0;
	mPollFds.push_back(outPfd);

	if (!session.body.empty())
	{
		// Yazılacak body varsa inFd'yi de poll'e ekliyoruz (POLLOUT hazır
		// olunca handleCgiPipeEvent yazacak).
		mCgiPipeOwner[session.inFd] = session.outFd;

		struct pollfd inPfd;
		inPfd.fd = session.inFd;
		inPfd.events = POLLOUT;
		inPfd.revents = 0;
		mPollFds.push_back(inPfd);
	}
	else
	{
		// Body yoksa (GET gibi) yazacak bir şey yok, stdin'i hemen kapatıp
		// script'e EOF veriyoruz; inFd'yi poll'e hiç eklemiyoruz.
		close(session.inFd);
		mCgiSessions[session.outFd].inFd = -1;
	}

	// CGI biterken handleCgiPipeEvent bu client'ı tekrar POLLOUT'a çevirecek
	// (cevap writeBuffer'a konunca); o zamana kadar bu fd'den bir
	// daha okumayalım diye events'i 0 yapıyoruz.
	setClientPollEvents(fd, 0);
}

// Artık cgiHandle sadece akışı okunaklı sırayla yürütüyor: script'i
// doğrula -> pipe'ları aç -> env'i kur -> fork et -> child'ı çalıştır /
// parent'ı kaydet. Gerçek iş yukarıdaki dört yardımcıda.
void Server::cgiHandle(Client &client, LocationConfig *loc, int fd)
{
	// CGI pipe'ının okuma ucu erkenden kapanırsa write() SIGPIPE fırlatıp
	// tüm process'i öldürebilir; subject'te "signal" izinli fonksiyon olduğu
	// için burada yok sayıyoruz (errno'ya bakmadan, sadece disposition).
	signal(SIGPIPE, SIG_IGN);

	std::string scriptPath = RequestHandler::resolveFilePath(loc, client.request.getPath());
	if (scriptPath.empty())
	{
		sendErrorAndCleanup(fd, 400);
		return;
	}

	struct stat st;
	if (stat(scriptPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
	{
		sendErrorAndCleanup(fd, 404);
		return;
	}

	// Interpreter önce config'ten (cgi_extension .py /usr/bin/python3);
	// config değer vermemişse resolveInterpreter'ın yerleşik tablosuna,
	// o da bilmiyorsa script'in kendi shebang'ına düşülür.
	size_t dot = client.request.getPath().find_last_of('.');
	std::string ext = (dot == std::string::npos) ? "" : client.request.getPath().substr(dot);
	std::string interpreter;
	bool useInterpreter = false;
	std::map<std::string, std::string>::const_iterator extIt = loc->cgiExtension.find(ext);
	if (extIt != loc->cgiExtension.end() && !extIt->second.empty())
	{
		interpreter = extIt->second;
		useInterpreter = true;
	}
	else
		useInterpreter = resolveInterpreter(ext, interpreter);

	if (access(scriptPath.c_str(), useInterpreter ? R_OK : X_OK) != 0)
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	// Subject: "CGI should be run in the correct directory for relative
	// path file access." Child chdir(scriptDir) yapacak, o yüzden
	// SCRIPT_FILENAME/PATH_TRANSLATED gibi env değerleri de child'ın YENİ
	// çalışma dizinine göre tutarlı olmalı -- ikisini de aynı scriptFile
	// (sadece dosya adı) ile dolduruyoruz.
	size_t slash = scriptPath.find_last_of('/');
	std::string scriptDir  = (slash == std::string::npos) ? "." : scriptPath.substr(0, slash);
	std::string scriptFile = (slash == std::string::npos) ? scriptPath : scriptPath.substr(slash + 1);

	int inPipe[2];
	int outPipe[2];
	if (!openCgiPipes(inPipe, outPipe))
	{
		sendErrorAndCleanup(fd, 500);
		return;
	}

	// Gövde (chunked bile olsa processClient tarafından ÖNCEDEN tam decode
	// edilip readBuffer'ın başına konmuş durumda) -- contentLength kadarı
	// bu isteğin gövdesi, gerisi (varsa) pipelined bir sonraki istek.
	std::string body = client.readBuffer.substr(0, client.contentLength);

	std::vector<std::string> envStorage;
	buildCgiEnv(client.request, client.matchedConfig, scriptFile, body.size(), envStorage);

	std::vector<char *> envp;
	for (size_t i = 0; i < envStorage.size(); ++i)
		envp.push_back(const_cast<char *>(envStorage[i].c_str()));
	envp.push_back(NULL);

	pid_t pid = fork();
	if (pid == -1)
	{
		close(inPipe[0]); close(inPipe[1]);
		close(outPipe[0]); close(outPipe[1]);
		sendErrorAndCleanup(fd, 500);
		return;
	}

	if (pid == 0)
	{
		execCgiChild(inPipe, outPipe, useInterpreter, interpreter, scriptDir, scriptFile, &envp[0]);
		return; // execCgiChild normalde hiç dönmez (execve/_exit), savunma amaçlı
	}

	registerCgiSession(pid, fd, inPipe, outPipe, body);
}

// listeningSockets()'teki (Server.cpp) tek poll() döngüsü her fd için
// client soketi mi CGI pipe'ı mı ayırt etmek zorunda; bunun için eklendi.
bool Server::isCgiPipe(int pollFd) const
{
	return mCgiPipeOwner.find(pollFd) != mCgiPipeOwner.end();
}

// Bir CGI pipe'ı işi bitince (yazma tamamlandı / okuma EOF / timeout)
// mPollFds'ten tek satırlık, index-güvenli şekilde çıkarmak için.
void Server::removePipeFromPoll(int pipeFd)
{
	for (size_t i = 0; i < mPollFds.size(); ++i)
	{
		if (mPollFds[i].fd == pipeFd)
		{
			mPollFds.erase(mPollFds.begin() + i);
			return;
		}
	}
}

// CGI çalışırken client fd'sinin events'ini 0 yapıp (isPollin tetiklenmesin),
// CGI bitince POLLIN|POLLOUT'a çevirmek için (cevap writeBuffer'a konunca
// isPollout onu flush edecek; keep-alive'da bağlantı açık kalıp yeni istek
// okunabilsin diye POLLIN de geri açılıyor).
void Server::setClientPollEvents(int clientFd, short events)
{
	for (size_t i = 0; i < mPollFds.size(); ++i)
	{
		if (mPollFds[i].fd == clientFd)
		{
			mPollFds[i].events = events;
			return;
		}
	}
}

// Bir CGI session'ı bitince (başarı/hata/timeout fark etmez) hem inFd hem
// outFd için mPollFds/mCgiPipeOwner/close çağrılarını tekrar etmemek için.
void Server::closeCgiFds(CgiSession &session)
{
	if (session.inFd != -1)
	{
		removePipeFromPoll(session.inFd);
		close(session.inFd);
		mCgiPipeOwner.erase(session.inFd);
	}
	removePipeFromPoll(session.outFd);
	close(session.outFd);
	mCgiPipeOwner.erase(session.outFd);
}

// Ana poll() dönüşünde (Server.cpp'deki listeningSockets()) bir CGI pipe
// fd'si hazır (ya da timeout) olduğunda buradan geçiyor; TEK poll() çağrısı
// dışında başka hiçbir poll()/read()/write() yok.
bool Server::handleCgiPipeEvent(size_t i)
{
	int curFd = mPollFds[i].fd;

	std::map<int, int>::iterator ownerIt = mCgiPipeOwner.find(curFd);
	if (ownerIt == mCgiPipeOwner.end())
		return false;

	std::map<int, CgiSession>::iterator sessIt = mCgiSessions.find(ownerIt->second);
	if (sessIt == mCgiSessions.end())
		return false;
	CgiSession &session = sessIt->second;

	// Subject: "a request should never hang indefinitely" — script çok
	// uzun sürerse burada öldürüyoruz. (Hiç event üretmeyen -- örn. uyuyan --
	// script'ler için aynı kontrol checkCgiTimeouts'ta her poll() turunda
	// da yapılıyor.)
	if (time(NULL) - session.start > CGI_TIMEOUT_SECONDS)
	{
		int clientFd = session.clientFd;
		kill(session.pid, SIGKILL);
		waitpid(session.pid, NULL, 0);
		closeCgiFds(session);
		mCgiSessions.erase(sessIt);
		sendErrorAndCleanup(clientFd, 500);
		setClientPollEvents(clientFd, POLLIN | POLLOUT);
		// closeCgiFds mPollFds'ten satır(lar) sildiği için index i artık
		// başka bir fd'yi gösteriyor olabilir; listeningSockets()'in ++i
		// yapmaması gerekiyorsa true dönmemiz lazım, bunu index'in hâlâ
		// aynı fd'yi gösterip göstermediğine bakarak (kaydırmadan bağımsız,
		// güvenli şekilde) anlıyoruz.
		return (i >= mPollFds.size() || mPollFds[i].fd != curFd);
	}

	if (curFd == session.inFd && (mPollFds[i].revents & (POLLOUT | POLLHUP | POLLERR)))
	{
		if (mPollFds[i].revents & POLLOUT)
		{
			size_t chunk = std::min(session.body.size() - session.written, static_cast<size_t>(4096));
			ssize_t w = write(session.inFd, session.body.c_str() + session.written, chunk);
			if (w > 0)
				session.written += static_cast<size_t>(w);
		}
		if ((mPollFds[i].revents & (POLLHUP | POLLERR)) || session.written >= session.body.size())
		{
			removePipeFromPoll(session.inFd);
			close(session.inFd);
			mCgiPipeOwner.erase(session.inFd);
			session.inFd = -1;
		}
	}
	else if (curFd == session.outFd && (mPollFds[i].revents & (POLLIN | POLLHUP | POLLERR)))
	{
		char buf[4096];
		ssize_t r = read(session.outFd, buf, sizeof(buf));
		if (r > 0)
		{
			session.output.append(buf, static_cast<size_t>(r));
		}
		else
		{
			int clientFd = session.clientFd;
			int status = 0;
			waitpid(session.pid, &status, 0);

			// keepAlive'ı sendResponseAndCleanup'tan ÖNCE okuyoruz --
			// keep-alive durumunda o çağrı client'ın per-request
			// alanlarını (resetForNextRequest ile) sıfırlıyor.
			bool keepAlive = false;
			std::map<int, Client>::iterator cIt = mClients.find(clientFd);
			if (cIt != mClients.end())
				keepAlive = cIt->second.keepAlive;

			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			{
				closeCgiFds(session);
				mCgiSessions.erase(sessIt);
				sendErrorAndCleanup(clientFd, 500);
			}
			else
			{
				CgiOutput cgiOut;
				parseCgiOutput(session.output, cgiOut);
				closeCgiFds(session);
				mCgiSessions.erase(sessIt);

				if (!cgiOut.location.empty() && cgiOut.statusCode == 200)
					sendResponseAndCleanup(clientFd, insertSetCookie(
						HttpResponseBuilder::buildRedirect(302, cgiOut.location, keepAlive), cgiOut.setCookie));
				else
					sendResponseAndCleanup(clientFd, insertSetCookie(
						HttpResponseBuilder::build(cgiOut.statusCode, cgiOut.statusText, cgiOut.body,
							keepAlive, cgiOut.contentType), cgiOut.setCookie));
			}
			setClientPollEvents(clientFd, POLLIN | POLLOUT);
		}
	}
	// Yukarıdaki inFd/outFd dallarından biri pipe kapattıysa index
	// i'deki satır artık başka bir fd olabilir; closeClient(i)'nin döndürdüğü
	// "erased" mantığının aynısı (listeningSockets() ++i yapmasın diye).
	return (i >= mPollFds.size() || mPollFds[i].fd != curFd);
}

// handleCgiPipeEvent'teki timeout kontrolü sadece pipe'ta bir EVENT olunca
// çalışır -- hiç çıktı üretmeden uyuyan bir script hiçbir event üretmez ve
// sonsuza kadar asılı kalırdı. Server.cpp'deki ana döngü her poll()
// turunda burayı çağırıp aynı timeout'u event beklemeden uyguluyor.
void Server::checkCgiTimeouts()
{
	time_t now = time(NULL);
	std::vector<int> timedOut;

	for (std::map<int, CgiSession>::iterator it = mCgiSessions.begin(); it != mCgiSessions.end(); ++it)
	{
		if (now - it->second.start > CGI_TIMEOUT_SECONDS)
			timedOut.push_back(it->first);
	}

	for (size_t i = 0; i < timedOut.size(); ++i)
	{
		std::map<int, CgiSession>::iterator sessIt = mCgiSessions.find(timedOut[i]);
		if (sessIt == mCgiSessions.end())
			continue;
		CgiSession &session = sessIt->second;

		int clientFd = session.clientFd;
		kill(session.pid, SIGKILL);
		waitpid(session.pid, NULL, 0);
		closeCgiFds(session);
		mCgiSessions.erase(sessIt);
		sendErrorAndCleanup(clientFd, 500);
		setClientPollEvents(clientFd, POLLIN | POLLOUT);
	}
}

// closeClient(), fd disconnect edildiğinde ve o fd'nin hâlâ çalışan bir
// CGI'si varsa bunu çağırır: süreci öldür, pipe'ları kapat, hiçbir yanıt
// göndermeye ÇALIŞMA (client zaten gidiyor). Session yoksa sessizce döner.
void Server::abortCgi(int clientFd)
{
	std::map<int, CgiSession>::iterator sessIt = mCgiSessions.begin();
	while (sessIt != mCgiSessions.end() && sessIt->second.clientFd != clientFd)
		++sessIt;
	if (sessIt == mCgiSessions.end())
		return;

	CgiSession &session = sessIt->second;
	kill(session.pid, SIGKILL);
	waitpid(session.pid, NULL, 0);
	closeCgiFds(session);
	mCgiSessions.erase(sessIt);
}
