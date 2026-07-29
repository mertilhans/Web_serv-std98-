// CGI implementasyonu -- Server.hpp'de bildirilen Server::isCgiRequest,
// Server::cgiHandle ve poll-driven CGI pipe yönetimi (isCgiPipe,
// handleCgiPipeEvent, cleanupCgiFds, removePipeFromPoll,
// setClientPollEvents) burada tanımlı. Önceden Server.cpp'nin içindeydi,
// buraya taşındı; Server.hpp'deki bildirimler ve Server.cpp'deki
// controlMethod'daki çağrı noktası değişmedi.
#include "Server.hpp"
#include "common.hpp"

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
#include <cerrno>

#define CGI_TIMEOUT_SECONDS 5 // CGI script'i bu saniyeyi geçerse SIGKILL + 500

// --- cgiHandle'ın kullandığı yardımcılar ---

// Uzantıya göre interpreter seçer; bilinmeyen uzantılarda script'in kendi
// shebang'ıyla doğrudan çalıştırılmasına düşer (false döner).
static bool findInterpreter(const std::string &ext, std::string &interpreter)
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
	addEnv(env, "GATEWAY_INTERFACE", "CGI/1.1");
	addEnv(env, "SERVER_PROTOCOL", req.version.empty() ? "HTTP/1.1" : req.version);
	addEnv(env, "REQUEST_METHOD", req.method);
	addEnv(env, "SCRIPT_NAME", req.path);
	addEnv(env, "SCRIPT_FILENAME", scriptPath);
	addEnv(env, "PATH_INFO", req.path);
	addEnv(env, "PATH_TRANSLATED", scriptPath);
	addEnv(env, "QUERY_STRING", req.query);
	addEnv(env, "REDIRECT_STATUS", "200");
	addEnv(env, "CONTENT_LENGTH", numToString(static_cast<long>(bodySize)));

	std::map<std::string, std::string>::const_iterator ctIt = req.headers.find("Content-Type");
	addEnv(env, "CONTENT_TYPE", ctIt != req.headers.end() ? ctIt->second : "");

	if (cfg)
	{
		addEnv(env, "SERVER_NAME", cfg->serverNames.empty() ? cfg->host : cfg->serverNames[0]);
		addEnv(env, "SERVER_PORT", numToString(cfg->port));
	}

	for (std::map<std::string, std::string>::const_iterator it = req.headers.begin(); it != req.headers.end(); ++it)
	{
		if (it->first == "Content-Type" || it->first == "Content-Length")
			continue;
		addEnv(env, toHttpEnvName(it->first), it->second);
	}
}

// parseCgiOutput'un sonucunu taşımak için; buildResponse/buildRedirect'e
// tek tek parametre geçmek yerine bunu döndürüp cgiHandle'da kullanıyoruz.
struct CgiOutput
{
	int statusCode;
	std::string statusText;
	std::string contentType;
	std::string location;
	std::string body;

	CgiOutput() : statusCode(200), statusText("OK"), contentType("text/html") {}
};

// CGI script'inin stdout'una yazdığı "header'lar\r\n\r\nbody" çıktısını ayrıştırır.
// Status/Content-Type/Location dışındaki header'lar yok sayılır.
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
	}
}

bool Server::isCgiRequest(LocationConfig *loc, const std::string &path)
{
	if (loc->cgiExtension.empty())
		return (false);
	if (path.size() < loc->cgiExtension.size())
		return (false);
	return (path.compare(path.size() - loc->cgiExtension.size(), loc->cgiExtension.size(), loc->cgiExtension) == 0);
}

/* ESKİ HALİ (silmedim, yorum satırına aldım): burada CGI pipe'ları için
 * cgiHandle kendi İÇİNDE ikinci, ayrı bir poll() döngüsü çalıştırıyordu.
 * Subject bunu açıkça yasaklıyor: "You must use only 1 poll() (or
 * equivalent) for all the I/O operations... pipes/FIFOs must be
 * non-blocking and driven by a single poll()." Aşağıdaki asıl (aktif)
 * cgiHandle artık pipe'ları kurup mPollFds'e (ana poll döngüsüne) kayıt
 * ediyor, gerçek okuma/yazma işini handleCgiPipeEvent yapıyor —
 * listeningSockets() (Server.cpp) zaten tek poll() çağırıyor, CGI da
 * ondan geçiyor.
 *
 * void Server::cgiHandle(ClientRequestState &state, LocationConfig *loc, int fd)
 * {
 * 	std::string scriptPath = resolveFilePath(loc, state.request.path);
 * 	...
 * 	bool timedOut = false;
 * 	while (true)
 * 	{
 * 		if (time(NULL) - start > CGI_TIMEOUT_SECONDS) { timedOut = true; break; }
 * 		struct pollfd pfds[2];
 * 		...
 * 		int ready = poll(pfds, n, 200); // <-- ikinci poll(), yasak
 * 		...
 * 	}
 * 	...
 * }
 */
void Server::cgiHandle(ClientRequestState &state, LocationConfig *loc, int fd)
{
	// CGI pipe'ının okuma ucu erkenden kapanırsa write() SIGPIPE fırlatıp
	// tüm process'i öldürebilir; subject'te "signal" izinli fonksiyon olduğu
	// için burada yok sayıyoruz (errno'ya bakmadan, sadece disposition).
	signal(SIGPIPE, SIG_IGN);

	std::string scriptPath = resolveFilePath(loc, state.request.path);
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

	std::string interpreter;
	bool useInterpreter = findInterpreter(loc->cgiExtension, interpreter);
	if (access(scriptPath.c_str(), useInterpreter ? R_OK : X_OK) != 0)
	{
		sendErrorAndCleanup(fd, 403);
		return;
	}

	int inPipe[2];
	if (pipe(inPipe) == -1)
	{
		sendErrorAndCleanup(fd, 500);
		return;
	}
	int outPipe[2];
	if (pipe(outPipe) == -1)
	{
		close(inPipe[0]);
		close(inPipe[1]);
		sendErrorAndCleanup(fd, 500);
		return;
	}

	std::vector<std::string> envStorage;
	buildCgiEnv(state.request, state.matchedConfig, scriptPath, mClientReadBuffers[fd].size(), envStorage);

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
		dup2(inPipe[0], STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		close(inPipe[0]); close(inPipe[1]);
		close(outPipe[0]); close(outPipe[1]);

		if (useInterpreter)
		{
			char *argv[] = { const_cast<char *>("env"), const_cast<char *>(interpreter.c_str()),
							  const_cast<char *>(scriptPath.c_str()), NULL };
			execve("/usr/bin/env", argv, &envp[0]);
		}
		else
		{
			char *argv[] = { const_cast<char *>(scriptPath.c_str()), NULL };
			execve(scriptPath.c_str(), argv, &envp[0]);
		}
		_exit(1);
	}

	close(inPipe[0]);
	close(outPipe[1]);
	fcntl(inPipe[1], F_SETFL, O_NONBLOCK);
	fcntl(outPipe[0], F_SETFL, O_NONBLOCK);

	// Buradan sonrası artık blocking değil: pipe'ları kaydedip dönüyoruz,
	// gerçek okuma/yazma ana poll() sonucuna göre handleCgiPipeEvent'te olacak.
	CgiSession session;
	session.pid      = pid;
	session.clientFd = fd;
	session.inFd     = inPipe[1];
	session.outFd    = outPipe[0];
	session.body     = mClientReadBuffers[fd];
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
	// (cevap mClientWriteBuffers'a konunca); o zamana kadar bu fd'den bir
	// daha okumayalım diye events'i 0 yapıyoruz.
	setClientPollEvents(fd, 0);
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
// CGI bitince POLLOUT'a çevirmek için (cevap mClientWriteBuffers'a konunca
// isPollout onu flush edip bağlantıyı kapatacak).
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
void Server::cleanupCgiFds(CgiSession &session)
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
	// uzun sürerse burada öldürüyoruz, poll() ikinci kez açılmadan.
	if (time(NULL) - session.start > CGI_TIMEOUT_SECONDS)
	{
		int clientFd = session.clientFd;
		kill(session.pid, SIGKILL);
		waitpid(session.pid, NULL, 0);
		cleanupCgiFds(session);
		mCgiSessions.erase(sessIt);
		sendErrorAndCleanup(clientFd, 500);
		setClientPollEvents(clientFd, POLLOUT);
		// cleanupCgiFds mPollFds'ten satır(lar) sildiği için index i artık
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

			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			{
				cleanupCgiFds(session);
				mCgiSessions.erase(sessIt);
				sendErrorAndCleanup(clientFd, 500);
			}
			else
			{
				CgiOutput cgiOut;
				parseCgiOutput(session.output, cgiOut);
				cleanupCgiFds(session);
				mCgiSessions.erase(sessIt);

				if (!cgiOut.location.empty() && cgiOut.statusCode == 200)
					sendResponseAndCleanup(clientFd, buildRedirect(cgiOut.location));
				else
					sendResponseAndCleanup(clientFd, buildResponse(cgiOut.statusCode, cgiOut.statusText, cgiOut.body, cgiOut.contentType));
			}
			setClientPollEvents(clientFd, POLLOUT);
		}
	}

	// Yukarıdaki inFd/outFd dallarından biri cleanupCgiFds çağırdıysa index
	// i'deki satır artık başka bir fd olabilir; closeClient(i)'nin döndürdüğü
	// "erased" mantığının aynısı (listeningSockets() ++i yapmasın diye).
	return (i >= mPollFds.size() || mPollFds[i].fd != curFd);
}
