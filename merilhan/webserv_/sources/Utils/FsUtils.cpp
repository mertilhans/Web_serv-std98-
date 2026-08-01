#include "FsUtils.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// OCF -- private, hic cagrilmaz (sinif sadece static metodlarla kullanilir).
FsUtils::FsUtils()
{
}

FsUtils::~FsUtils()
{
}

FsUtils::FsUtils(const FsUtils& ref)
{
	(void)ref;
}

FsUtils&	FsUtils::operator=(const FsUtils& ref)
{
	(void)ref;
	return (*this);
}

// std::ifstream degil, subject'in whitelist'indeki open/read/close ile
// (poll() disinda -- normal disk dosyalari icin subject'in izin verdigi
// istisna, bkz. IV.1: "You are not required to use poll() for regular
// disk files").
bool	FsUtils::readWholeFile(const std::string& path, std::string& content)
{
	int	fd = open(path.c_str(), O_RDONLY);

	if (fd == -1)
		return (false);

	content.clear();
	char	buffer[4096];
	ssize_t	bytesRead;

	while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
		content.append(buffer, static_cast<size_t>(bytesRead));

	bool	ok = (bytesRead == 0);
	close(fd);
	return (ok);
}

bool	FsUtils::hasDotDotSegment(const std::string& path)
{
	size_t	pos = 0;

	while (pos < path.size())
	{
		size_t		slash = path.find('/', pos);
		std::string	segment = (slash == std::string::npos) ? path.substr(pos) : path.substr(pos, slash - pos);

		if (segment == "..")
			return (true);
		if (slash == std::string::npos)
			break;
		pos = slash + 1;
	}
	return (false);
}

bool	FsUtils::exists(const std::string& path)
{
	struct stat	st;

	return (stat(path.c_str(), &st) == 0);
}

bool	FsUtils::isDirectory(const std::string& path)
{
	struct stat	st;

	if (stat(path.c_str(), &st) != 0)
		return (false);
	return (S_ISDIR(st.st_mode));
}

bool	FsUtils::isRegularFile(const std::string& path)
{
	struct stat	st;

	if (stat(path.c_str(), &st) != 0)
		return (false);
	return (S_ISREG(st.st_mode));
}

bool	FsUtils::isReadable(const std::string& path)
{
	return (access(path.c_str(), R_OK) == 0);
}
