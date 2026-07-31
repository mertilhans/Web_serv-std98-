#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <string>
#include <vector>
#include "ConfigStructs.hpp"

class ConfigParser
{
private:
	std::vector<std::string>		_tokens;
	size_t							_pos;
	std::vector<raw::ServerConfig>	_servers;

	ConfigParser(const ConfigParser& ref);
	ConfigParser& operator=(const ConfigParser& ref);
	void				_tokenize(const std::string& content);
	bool				_atEnd() const;
	std::string			_peek() const;
	std::string			_next();
	void				_expect(const std::string& token);
	void				_parseServerBlock();
	void				_parseServerDirective(raw::ServerConfig& server);
	void				_parseLocationBlock(raw::ServerConfig& server);
	void				_parseLocationDirective(raw::LocationConfig& location);
	raw::ListenConfig	_parseListenValue(const std::string& raw);
	size_t				_parseSizeValue(const std::string& raw);
	void				_applyInheritance(raw::ServerConfig& server);
	bool				_isValidDirPath(const std::string& path);
	bool				_isValidFilename(const std::string& filename);
	bool				_isValidLocationPath(const std::string& uri);
	bool				_isValidServerName(const std::string& name);
	bool				_resolveIPv4(const std::string& host, uint32_t& outIp);
	bool				_isNumeric(const std::string& str);
	bool				_isValidErrorPageCode(int code) const;
public:
	ConfigParser();
	~ConfigParser();

	void	parseFile(const std::string& path);
	const std::vector<raw::ServerConfig>&	getServers() const;
	std::vector<raw::ServerConfig>&		getServers();
};

#endif