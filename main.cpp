#include <string>
#include <vector>
#include <iostream>
#include <exception>
#include <csignal>

#include "configparser.hpp"
#include "Server.hpp"

#define DEFAULT_CONFIG_PATH "default.conf"

int main(int argc, char **argv)
{
    // [Adım 1] argv kontrolü
    std::string configPath = (argc == 2) ? argv[1] : DEFAULT_CONFIG_PATH;
    if (argc > 2) {
        std::cerr << "usage: ./webserv [config_file]" << std::endl;
        return 1;
    }

    // Client bağlantıyı aniden keserse (RST) ve tam o sırada write()
    // çağrılırsa kernel SIGPIPE gönderir; varsayılan davranış process'i
    // sonlandırır. Subject "hiçbir koşulda çökmemeli" diyor, o yüzden
    // ilk I/O'dan önce, process başlarken bir kere ignore ediyoruz.
    signal(SIGPIPE, SIG_IGN);

    try {
        ConfigParser parser;
        std::vector<ServerConfig> configs = parser.parse(configPath);

        // [Adım 9-11] socket'leri kur ve event loop'u başlat
        Server server(configs);
        server.setupSockets(); // [Adım 9-10]
        server.run(); // [Adım 12] tek poll() döngüsü
    } catch (const std::exception &e) {
        std::cerr << "webserv: " << e.what() << std::endl;
        return 1;
    }
    return 0; // [Adım 13] run() dönünce temiz kapanış zaten yapılmış olur
}
