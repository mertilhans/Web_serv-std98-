#ifndef CGI_PROCESS_HPP
#define CGI_PROCESS_HPP

#include <string>
#include <vector>
#include <sys/types.h>	// pid_t
#include <ctime>

// Tek bir CGI cagrisinin (fork edilmis child + iki pipe) yasam dongusunu
// yonetir: fork+pipe+dup2+chdir+execve (start), stdin'e parca parca yazma
// (writeStdin), stdout'tan parca parca okuma (readStdout), erken sonlandirma
// (terminate). Server/ServerCgi.cpp bu sinifin ustune sadece "glue" ekler:
// poll() kaydi (mPollFds + fd->client ters-arama haritalari), timeout/reap
// zamanlamasi -- fd yasam dongusunun kendisi (ac/yaz/oku/kapat) burada.
class CgiProcess
{
public:
	CgiProcess();
	~CgiProcess();
	CgiProcess(const CgiProcess &other);
	CgiProcess &operator=(const CgiProcess &other);

	// fork+pipe+dup2+chdir+execve. Basarisizlikta (pipe/fork hatasi) false
	// doner, hicbir kaynak sizdirmaz (acilan pipe'lar kapatilir). body bos
	// ise (orn. GET) stdin ucu hemen kapatilir (stdinFd() == -1 doner) --
	// caller bu durumda stdin'i poll()'a hic eklememeli.
	bool start(const std::string &interpreterPath, const std::string &scriptPath,
			const std::vector<std::string> &envStrings, const std::string &body, bool keepAlive);

	// stdin'e bir sonraki parcayi yazar. Pipe kiriksa (CGI erken cikmis
	// olabilir, SIGPIPE SIG_IGN sayesinde write() sadece -1 doner) veya
	// yazma tamamlandiysa stdin fd'sini kapatip true doner (caller poll()
	// kaydini kaldirmali); yazilacak veri kaldiysa false doner.
	bool writeStdin();

	// stdout'tan bir parca okuyup ic tampona ekler. EOF/hata durumunda
	// fd'yi kapatip true doner (caller poll() kaydini kaldirmali ve
	// finalize etmeli), okumaya devam edilebilirse false doner.
	bool readStdout();

	// Stdin hala aciksa (CGI govdeyi tam okumadan stdout'u kapatmis
	// olabilir) yazmanin anlami kalmadigi icin acikca kapatir.
	void closeStdin();

	// SIGKILL + kalan (kapatilmamis) stdin/stdout fd'lerini kapatir --
	// hem timeout hem abort (client erken koptu) tarafindan kullanilir.
	void terminate();

	pid_t pid() const;
	int stdinFd() const;
	int stdoutFd() const;
	bool keepAlive() const;
	time_t startTime() const;
	const std::string &outputBuffer() const;

private:
	pid_t mPid;
	int mStdinFd;	// -1: body yok / zaten kapatildi
	int mStdoutFd;
	std::string mInputBuffer;
	size_t mInputOffset;
	std::string mOutputBuffer;
	bool mKeepAlive;
	time_t mStartTime;
};

#endif
