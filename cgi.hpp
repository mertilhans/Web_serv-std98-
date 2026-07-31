#ifndef CGI_HPP
#define CGI_HPP

#include <string>


#define CGI_TIMEOUT_SECONDS 5 // CGI script'i bu saniyeyi geçerse SIGKILL + 500

// parseCgiOutput'un sonucunu taşımak için; buildResponse/buildRedirect'e
// tek tek parametre geçmek yerine bunu döndürüp cgiHandle'da kullanıyoruz.
struct CgiOutput
{
	int         statusCode;
	std::string statusText;
	std::string contentType;
	std::string location;
	std::string body;

	CgiOutput();
};

#endif