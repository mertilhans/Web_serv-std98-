#include <string>
#include <vector>
#include <iostream>
#include <exception>
#include <csignal>
#include <map>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "configparser.hpp"
#include "ConfigParser.hpp"
#include "Server.hpp"

#define DEFAULT_CONFIG_PATH "default.conf"

// raw::ListenConfig.ip/port network byte order'da (bkz. ConfigParser.cpp
// _parseListenValue) -- Server.cpp'nin beklediği host string + host-order
// port'a geri çeviriyoruz.
static std::string ipToHost(uint32_t ipNetworkOrder)
{
    struct in_addr addr;
    addr.s_addr = ipNetworkOrder;
    return std::string(inet_ntoa(addr));
}

// raw::LocationConfig -> Server.cpp'nin beklediği basit LocationConfig.
// index (vector) -> ilk eleman (tek dosya), cgiExtension (map) -> tek
// uzantı+interpreter çifti (bugünkü default.conf zaten location başına
// tek cgi_extension tanımlıyor, o yüzden kayıpsız).
static LocationConfig convertLocation(const raw::LocationConfig &rl)
{
    LocationConfig loc;

    loc.path = rl.path;
    loc.hasAllowMethods = rl.hasAllowMethods;
    loc.methods = rl.methods;
    loc.hasRoot = rl.hasRoot;
    loc.root = rl.root;
    loc.hasIndex = rl.hasIndex;
    loc.index = rl.index.empty() ? "index.html" : rl.index[0];
    loc.hasAutoindex = rl.hasAutoindex;
    loc.autoindex = rl.autoindex;
    loc.hasClientMaxBodySize = rl.hasClientMaxBodySize;
    loc.hasUploadPath = rl.hasUploadPath;
    loc.uploadPath = rl.uploadPath;
    loc.hasCgiExtension = !rl.cgiExtension.empty();
    if (!rl.cgiExtension.empty())
    {
        std::map<std::string, std::string>::const_iterator it = rl.cgiExtension.begin();
        loc.cgiExtension = it->first;
        loc.cgiInterpreter = it->second;
    }
    loc.hasRedirect = rl.hasRedirect;
    loc.redirectTarget = rl.redirectTarget;
    loc.redirectCode = rl.redirectCode;

    return loc;
}

// raw::ServerConfig -> Server.cpp'nin beklediği basit ServerConfig
// listesi. Bir rich server block'ta birden fazla 'listen' olabilir; her
// biri için ayrı bir basit ServerConfig üretiyoruz -- aynı host:port'u
// paylaşanları Server::setupSockets zaten kendi içinde tek socket'te
// birleştiriyor, o yüzden bilgi kaybı olmuyor.
static std::vector<ServerConfig> convertConfigs(const std::vector<raw::ServerConfig> &richConfigs)
{
    std::vector<ServerConfig> result;

    for (size_t i = 0; i < richConfigs.size(); ++i)
    {
        const raw::ServerConfig &rs = richConfigs[i];

        std::vector<LocationConfig> locs;
        for (size_t j = 0; j < rs.locations.size(); ++j)
            locs.push_back(convertLocation(rs.locations[j]));

        for (size_t k = 0; k < rs.listens.size(); ++k)
        {
            ServerConfig cfg;

            cfg.hasListen = true;
            cfg.host = ipToHost(rs.listens[k].ip);
            cfg.port = ntohs(rs.listens[k].port);
            cfg.hasServerName = rs.hasServerName;
            if (rs.hasServerName)
                cfg.serverNames.push_back(rs.serverName);
            cfg.errorPages = rs.errorPages;
            cfg.hasClientMaxBodySize = rs.hasClientMaxBodySize;
            cfg.clientMaxBodySize = rs.clientMaxBodySize;
            cfg.locations = locs;

            result.push_back(cfg);
        }
    }

    return result;
}

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
        parser.parseFile(configPath);
        std::vector<ServerConfig> configs = convertConfigs(parser.getServers());

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
