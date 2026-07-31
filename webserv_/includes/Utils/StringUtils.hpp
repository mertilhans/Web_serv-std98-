#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <string>

// Stateless string yardimcilari -- ConfigParser, ListenTable, Server ve
// CGI arasinda paylasilir. Hicbir modul-ozel bilgiye ihtiyac duymaz,
// hep ayni girdi icin ayni ciktiyi verir. Sinif hic orneklenmez.
class StringUtils
{
public:
	static std::string	toLower(const std::string& str);
	static std::string	toUpper(const std::string& str);
	// Sadece BASTAKI bosluklari (' ') kirpar -- HTTP header degeri
	// ayirmada kullanilan tam davranis budur (sondan/tab'tan kirpma yok).
	static std::string	trimLeadingSpaces(const std::string& str);
	static bool			isNumeric(const std::string& str);

private:
	// Sinif hic orneklenmez (sadece static metodlar) -- OCF yine de burada,
	// private'da, hicbir zaman kullanilmayacak sekilde tanimli tutuluyor.
	StringUtils();
	~StringUtils();
	StringUtils(const StringUtils& ref);
	StringUtils&	operator=(const StringUtils& ref);
};

#endif
