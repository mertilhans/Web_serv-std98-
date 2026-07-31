#include "CgiProcess.hpp"
#include "NetUtils.hpp"
#include <unistd.h>
#include <signal.h>

CgiProcess::CgiProcess()
	: mPid(-1), mStdinFd(-1), mStdoutFd(-1), mInputOffset(0), mKeepAlive(false), mStartTime(0)
{
}

CgiProcess::~CgiProcess()
{
}

CgiProcess::CgiProcess(const CgiProcess &other)
	: mPid(other.mPid), mStdinFd(other.mStdinFd), mStdoutFd(other.mStdoutFd),
	mInputBuffer(other.mInputBuffer), mInputOffset(other.mInputOffset),
	mOutputBuffer(other.mOutputBuffer), mKeepAlive(other.mKeepAlive), mStartTime(other.mStartTime)
{
}

CgiProcess &CgiProcess::operator=(const CgiProcess &other)
{
	if (this != &other)
	{
		mPid = other.mPid;
		mStdinFd = other.mStdinFd;
		mStdoutFd = other.mStdoutFd;
		mInputBuffer = other.mInputBuffer;
		mInputOffset = other.mInputOffset;
		mOutputBuffer = other.mOutputBuffer;
		mKeepAlive = other.mKeepAlive;
		mStartTime = other.mStartTime;
	}
	return (*this);
}

bool CgiProcess::start(const std::string &interpreterPath, const std::string &scriptPath,
		const std::vector<std::string> &envStrings, const std::string &body, bool keepAlive)
{
	int stdinPipe[2];
	int stdoutPipe[2];

	if (pipe(stdinPipe) == -1)
		return (false);
	if (pipe(stdoutPipe) == -1)
	{
		close(stdinPipe[0]);
		close(stdinPipe[1]);
		return (false);
	}

	std::vector<char *> envp;
	for (size_t i = 0; i < envStrings.size(); ++i)
		envp.push_back(const_cast<char *>(envStrings[i].c_str()));
	envp.push_back(NULL);

	// scriptPath'i dizin/dosya-adi olarak ikiye ayiriyoruz: subject "CGI
	// dogru dizinde calismali" diyor, o yuzden child chdir(scriptDir)
	// yapacak -- bu durumda execve'ye SADECE dosya adini (scriptBase)
	// vermeliyiz, yoksa (chdir sonrasi) yol iki kez katlanir.
	std::string scriptDir = ".";
	std::string scriptBase = scriptPath;
	size_t slash = scriptPath.find_last_of('/');
	if (slash != std::string::npos)
	{
		scriptDir = scriptPath.substr(0, slash);
		scriptBase = scriptPath.substr(slash + 1);
	}

	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(interpreterPath.c_str()));
	argv.push_back(const_cast<char *>(scriptBase.c_str()));
	argv.push_back(NULL);

	pid_t pid = fork();
	if (pid == -1)
	{
		close(stdinPipe[0]);
		close(stdinPipe[1]);
		close(stdoutPipe[0]);
		close(stdoutPipe[1]);
		return (false);
	}

	if (pid == 0)
	{
		// ---- CHILD ----
		dup2(stdinPipe[0], STDIN_FILENO);
		dup2(stdoutPipe[1], STDOUT_FILENO);
		close(stdinPipe[0]);
		close(stdinPipe[1]);
		close(stdoutPipe[0]);
		close(stdoutPipe[1]);

		if (chdir(scriptDir.c_str()) != 0)
			_exit(1);

		execve(interpreterPath.c_str(), &argv[0], &envp[0]);
		// execve basarisiz oldu: normal exit() akisina (atexit/C++ destructor,
		// parent ile paylasilan iostream buffer'lari) girmeden hemen cik --
		// aksi halde ayni process ikinci bir webserv gibi devam ederdi.
		_exit(1);
	}

	// ---- PARENT ----
	close(stdinPipe[0]);
	close(stdoutPipe[1]);

	NetUtils::setNonBlocking(stdinPipe[1]);
	NetUtils::setNonBlocking(stdoutPipe[0]);

	mPid = pid;
	mStdoutFd = stdoutPipe[0];
	mKeepAlive = keepAlive;
	mStartTime = time(NULL);

	if (body.empty())
	{
		// Yazilacak govde yok (orn. GET) -- yazma ucunu hemen kapatip
		// CGI'nin stdin'ine aninda EOF gonderiyoruz, poll'e hic girmeye
		// gerek yok.
		close(stdinPipe[1]);
		mStdinFd = -1;
	}
	else
	{
		mStdinFd = stdinPipe[1];
		mInputBuffer = body;
	}

	return (true);
}

bool CgiProcess::writeStdin()
{
	if (mStdinFd == -1)
		return (true);

	const char *data = mInputBuffer.c_str() + mInputOffset;
	size_t remaining = mInputBuffer.size() - mInputOffset;

	ssize_t written = write(mStdinFd, data, remaining);
	if (written < 0)
	{
		// Kirik pipe (CGI erken cikmis olabilir) -- SIGPIPE SIG_IGN
		// sayesinde write() burada sadece -1 donuyor, crash yok.
		close(mStdinFd);
		mStdinFd = -1;
		return (true);
	}

	mInputOffset += static_cast<size_t>(written);
	if (mInputOffset < mInputBuffer.size())
		return (false);

	close(mStdinFd);
	mStdinFd = -1;
	return (true);
}

bool CgiProcess::readStdout()
{
	char buffer[4096];
	ssize_t bytesRead = read(mStdoutFd, buffer, sizeof(buffer));

	if (bytesRead > 0)
	{
		mOutputBuffer.append(buffer, bytesRead);
		return (false);
	}

	// bytesRead <= 0: EOF ya da okuma hatasi -- ikisi de "CGI bitti" sayilir.
	close(mStdoutFd);
	mStdoutFd = -1;
	return (true);
}

void CgiProcess::closeStdin()
{
	if (mStdinFd != -1)
	{
		close(mStdinFd);
		mStdinFd = -1;
	}
}

void CgiProcess::terminate()
{
	kill(mPid, SIGKILL);
	if (mStdinFd != -1)
	{
		close(mStdinFd);
		mStdinFd = -1;
	}
	if (mStdoutFd != -1)
	{
		close(mStdoutFd);
		mStdoutFd = -1;
	}
}

pid_t CgiProcess::pid() const
{
	return (mPid);
}

int CgiProcess::stdinFd() const
{
	return (mStdinFd);
}

int CgiProcess::stdoutFd() const
{
	return (mStdoutFd);
}

bool CgiProcess::keepAlive() const
{
	return (mKeepAlive);
}

time_t CgiProcess::startTime() const
{
	return (mStartTime);
}

const std::string &CgiProcess::outputBuffer() const
{
	return (mOutputBuffer);
}
