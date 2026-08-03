#include "ConfigParser.hpp"
#include "ListenTable.hpp"
#include "Server.hpp"
#include <iostream>
#include <exception>

int	main(int argc, char **argv)
{
	std::string configPath = (argc == 2) ? argv[1] : DEFAULT_CONFIG_PATH;
	if (argc > 2) {
		std::cerr << "usage: ./webserv [config_file]" << std::endl;
		return (1);
	}
	try
	{
		ConfigParser	parser;
		parser.parseFile(configPath);

		std::vector<ServerConfig>&	servers = parser.getServers();
		std::cout << "Config OK: " << servers.size() << " server block(s) parsed." << std::endl;

		// ListenTable, server'lar arasi (ip,port,server_name) topology
		// kontrollerini yapar ve wildcard/spesifik IP merge planini kurar.
		// Server bu tabloyu referans olarak alir -- ServerConfig'lerin
		// KENDI bir kopyasini tutmaz, boylece ListenTable'in icindeki
		// ServerConfig* pointer'lari ile Server'in gordugu nesneler her
		// zaman ayni kalir.
		ListenTable	listenTable;
		listenTable.build(servers);
		listenTable.printListenSummary(std::cout);

		Server server(listenTable);
		server.setupSockets();
		server.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
