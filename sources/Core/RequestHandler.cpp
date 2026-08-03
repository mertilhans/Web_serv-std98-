#include "RequestHandler.hpp"
#include "FsUtils.hpp"
#include "HttpUtils.hpp"
#include "HttpResponseBuilder.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <sstream>

std::string RequestHandler::joinPath(const std::string &base, LocationConfig *loc, const std::string &path)
{
	std::string relative = path.substr(loc->path.size());
	if (!relative.empty() && relative[0] == '/')
		relative = relative.substr(1);

	if (FsUtils::hasDotDotSegment(relative))
		return "";

	std::string fullPath = base;
	if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
		fullPath += "/";
	fullPath += relative;

	return (fullPath);
}

std::string RequestHandler::resolveFilePath(LocationConfig *loc, const std::string &path)
{
	return (joinPath(loc->root, loc, path));
}

std::string RequestHandler::resolveUploadPath(LocationConfig *loc, const std::string &path)
{
	return (joinPath(loc->uploadPath, loc, path));
}

bool RequestHandler::writeUploadFile(const std::string &fullPath, const std::string &body)
{
	int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		return (false);

	ssize_t written = write(fd, body.c_str(), body.size());
	close(fd);

	return (written == static_cast<ssize_t>(body.size()));
}

bool RequestHandler::buildAutoindex(const std::string &dirPath, const std::string &requestPath, std::string &out)
{
	DIR *dir = opendir(dirPath.c_str());
	if (!dir)
		return (false);

	std::ostringstream html;
	html << "<html><body><h1>Index of " << HttpUtils::htmlEscape(requestPath) << "</h1><ul>";

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name == ".")
			continue;
		// name diskten geliyor ama BURAYA client'in upload ettigi bir
		// dosya adi olarak da gelmis olabilir -- HTML'e gomulmeden once
		// escape ediyoruz (stored XSS savunmasi).
		std::string escapedName = HttpUtils::htmlEscape(name);
		html << "<li><a href=\"" << escapedName << "\">" << escapedName << "</a></li>";
	}
	closedir(dir);

	html << "</ul></body></html>";
	out = html.str();
	return (true);
}

bool RequestHandler::handleGet(Client &client, LocationConfig *loc, std::string &response, int &errorCode)
{
	const std::string &requestPath = client.request.getPath();
	std::string fullPath = resolveFilePath(loc, requestPath);

	struct stat st;
	bool exists = (stat(fullPath.c_str(), &st) == 0);
	bool isDir = (exists && S_ISDIR(st.st_mode));

	if (isDir && (requestPath.empty() || requestPath[requestPath.size() - 1] != '/'))
	{
		response = HttpResponseBuilder::buildRedirect(301, requestPath + "/", client.keepAlive);
		return (true);
	}
	std::string dirPath;

	if (isDir)
	{
		if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
			fullPath += "/";
		dirPath = fullPath;

		exists = false;
		for (size_t i = 0; i < loc->index.size(); ++i)
		{
			std::string candidate = dirPath + loc->index[i];
			if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode))
			{
				fullPath = candidate;
				exists = true;
				break;
			}
		}
	}

	if (exists && S_ISREG(st.st_mode) && access(fullPath.c_str(), R_OK) != 0)
	{
		errorCode = 403;
		return (false);
	}

	std::string content;
	if (exists && S_ISREG(st.st_mode) && FsUtils::readWholeFile(fullPath, content))
	{
		response = HttpResponseBuilder::build(200, HttpUtils::statusText(200), content, client.keepAlive, HttpUtils::contentTypeFor(fullPath));
		return (true);
	}

	if (isDir && loc->autoindex && buildAutoindex(dirPath, requestPath, content))
	{
		response = HttpResponseBuilder::build(200, HttpUtils::statusText(200), content, client.keepAlive);
		return (true);
	}

	if (isDir)
	{
		errorCode = 404;
		return (false);
	}

	errorCode = 404;
	return (false);
}

bool RequestHandler::handlePost(Client &client, LocationConfig *loc, std::string &response, int &errorCode)
{
	if (loc->uploadPath.empty())
	{
		response = HttpResponseBuilder::build(200, HttpUtils::statusText(200),
			"<html><body><h1>200 OK</h1></body></html>", client.keepAlive);
		return (true);
	}

	std::string fullPath = resolveUploadPath(loc, client.request.getPath());

	if (fullPath.empty() || fullPath[fullPath.size() - 1] == '/')
	{
		errorCode = 400;
		return (false);
	}

	struct stat existSt;
	bool alreadyExisted = (stat(fullPath.c_str(), &existSt) == 0);

	std::string body = client.readBuffer.substr(0, client.contentLength);

	if (writeUploadFile(fullPath, body))
	{
		if (alreadyExisted)
			response = HttpResponseBuilder::build(200, HttpUtils::statusText(200),
				"<html><body><h1>200 OK</h1></body></html>", client.keepAlive);
		else
			response = HttpResponseBuilder::build(201, HttpUtils::statusText(201),
				"<html><body><h1>201 Created</h1></body></html>", client.keepAlive);
		return (true);
	}

	errorCode = 500;
	return (false);
}

bool RequestHandler::handleDelete(Client &client, LocationConfig *loc, std::string &response, int &errorCode)
{
	if (loc->uploadPath.empty())
	{
		errorCode = 403;
		return (false);
	}

	std::string fullPath = resolveUploadPath(loc, client.request.getPath());

	if (fullPath.empty() || fullPath[fullPath.size() - 1] == '/')
	{
		errorCode = 400;
		return (false);
	}

	struct stat st;
	if (stat(fullPath.c_str(), &st) == -1 || !S_ISREG(st.st_mode))
	{
		errorCode = 404;
		return (false);
	}

	if (remove(fullPath.c_str()) != 0)
	{
		errorCode = 500;
		return (false);
	}

	response = HttpResponseBuilder::build(200, HttpUtils::statusText(200),
		"<html><body><h1>200 OK</h1><p>Deleted</p></body></html>", client.keepAlive);
	return (true);
}
