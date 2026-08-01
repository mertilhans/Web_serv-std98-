#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <map>

// Tek bir HTTP istegini (request-line + header'lar) temsil eder. Parse
// mantigi artik Server'da degil burada -- "HTTP protokolu" sorumlulugu
// tek bir yerde topluyor, Server sadece elde edilen degerleri kullaniyor.
class HttpRequest
{
public:
	HttpRequest();
	~HttpRequest();
	HttpRequest(const HttpRequest& ref);
	HttpRequest&	operator=(const HttpRequest& ref);

	// "GET /path?query HTTP/1.1" satirini parse eder. path url-decode
	// edilir (yuzde-encoding coz + null-byte injection reddi); query
	// KASITLI OLARAK ham/encoded birakilir -- CGI/1.1 spec'i QUERY_STRING'in
	// coz"ulmemis halde CGI'ye verilmesini ister, coz"me islemi script'in
	// kendi isi. Format hatasi, bozuk encoding ya da path'te NULL bayt
	// varsa false doner (caller 400 Bad Request'e cevirir).
	bool	parseRequestLine(const std::string& line);
	// Header blogundaki ("\r\n" ile ayrilmis, ilk satiri istek satiri olan)
	// satirlari parse eder; ilk satir atlanir.
	bool	parseHeaderLines(const std::string& headerBlock);

	const std::string&	getMethod() const;
	const std::string&	getPath() const;
	const std::string&	getQuery() const;
	const std::string&	getVersion() const;
	const std::map<std::string, std::string>&	getHeaders() const;
	// Var olan bir header'i (tam-case anahtarla) dondurur, yoksa "".
	std::string	getHeader(const std::string& name) const;

private:
	std::string	_method;
	std::string	_path;
	std::string	_query;
	std::string	_version;
	std::map<std::string, std::string>	_headers;
};

#endif
