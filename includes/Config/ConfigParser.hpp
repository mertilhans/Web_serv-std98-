#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <string>
#include <vector>
#include "ConfigStructs.hpp"

class ConfigParser
{
private:
	std::vector<std::string>	_tokens;
	size_t						_pos;
	std::vector<ServerConfig>	_servers;

	ConfigParser(const ConfigParser& ref);
	ConfigParser& operator=(const ConfigParser& ref);
	void			_tokenize(const std::string& content);
	bool			_atEnd() const;
	std::string		_peek() const;
	std::string		_next();
	void			_expect(const std::string& token);
	void			_parseServerBlock();
	void			_parseServerDirective(ServerConfig& server);
	void			_parseLocationBlock(ServerConfig& server);
	void			_parseLocationDirective(LocationConfig& location);
	ListenConfig	_parseListenValue(const std::string& raw);
	size_t			_parseSizeValue(const std::string& raw);
	void			_applyInheritance(ServerConfig& server);
	// Bu dosya formatina OZEL kabul kriterleri (PATH_MAX siniri, belirli
	// on-ekler, karakter kumesi vb.) -- HTTP kurali ya da genel bir
	// dosya-sistemi/ag yardimcisi degil, sadece bu parser'in politikasi,
	// bu yuzden Utils katmanina degil burada private kaliyor.
	bool			_isValidDirPath(const std::string& path);
	bool			_isValidFilename(const std::string& filename);
	bool			_isValidLocationPath(const std::string& uri);
	bool			_isValidServerName(const std::string& name);
	bool			_isValidErrorPageCode(int code) const;
public:
	ConfigParser();
	~ConfigParser();

	void	parseFile(const std::string& path);
	const std::vector<ServerConfig>&	getServers() const;
	std::vector<ServerConfig>&			getServers();
};

#endif
