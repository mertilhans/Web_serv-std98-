#ifndef CONFIGPARS_HPP
#define CONFIGPARS_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <cctype>

#include "common.hpp"

struct LocationConfig
{
    std::string             path;             // "/kapouet"
    std::vector<std::string> methods;         // GET, POST, DELETE
    std::string             root;             // "/tmp/www"
    std::string             index;            // "index.html"
    bool                    autoindex;
    std::string             uploadPath;       // upload klasörü
    std::string             cgiExtension;     // ".py"
    std::string             redirectTarget;   // return 301 ...

    LocationConfig() : autoindex(false) {}
};

struct ServerConfig
{
    std::string                     host;
    int                              port;
    std::vector<std::string>        serverNames;
    std::map<int, std::string>      errorPages;
    size_t                          clientMaxBodySize;
    std::vector<LocationConfig>     locations;

    ServerConfig() : port(80), clientMaxBodySize(1024 * 1024) {}
};

// Every ServerConfig that shares the same listen host:port. The right
// virtual host among these is picked later by matching the Host header.
//struct ServerMeta
//{
//    std::vector<ServerConfig> configs;
//};

struct Token
{
    std::string value;
    int         line;
};

class ConfigParser
{
public:
    // [Adım 3-8] tüm parse akışının giriş metodu
    std::vector<ServerConfig> parse(const std::string &path)
    {
        _path = path;
        std::string raw = readFile(path);          // [Adım 3] dosyayı tamamen oku
        std::vector<Token> tokens = tokenize(raw);  // [Adım 4] yorum/boşluk temizliği + tokenize

        std::vector<ServerConfig> result;
        size_t i = 0;
        while (i < tokens.size()) {
            expect(tokens, i, "server");
            expect(tokens, i, "{");
            result.push_back(parseServerBlock(tokens, i)); // [Adım 5-6]
            expect(tokens, i, "}");
        }
        return result; // [Adım 8] hazır ServerConfig listesi
    }

private:
    std::string readFile(const std::string &path)
    {
        std::string content;
        if (!readWholeFile(path, content))
            throw std::runtime_error(path + ": cannot open config file");
        return content;
    }

    std::vector<Token> tokenize(const std::string &raw)
    {

		//config dosyasının tüm parser işlemleri burada yapılmaktadır...
        std::vector<Token> tokens;
        std::string current;
        int line = 1;
        bool inComment = false;

        for (size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\n') {
                inComment = false;
                if (!current.empty()) { tokens.push_back(makeToken(current, line)); current.clear(); }
                ++line;
                continue;
            }
            if (inComment)
                continue;
            if (c == '#') {
                if (!current.empty()) { tokens.push_back(makeToken(current, line)); current.clear(); }
                inComment = true;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) { tokens.push_back(makeToken(current, line)); current.clear(); }
                continue;
            }
            if (c == '{' || c == '}' || c == ';') {
                if (!current.empty()) { tokens.push_back(makeToken(current, line)); current.clear(); }
                std::string sym(1, c);
                tokens.push_back(makeToken(sym, line));
                continue;
            }
            current += c;
        }
        if (!current.empty())
            tokens.push_back(makeToken(current, line));
        return tokens;
    }

    Token makeToken(const std::string &value, int line)
    {
        Token t;
        t.value = value;
        t.line = line;
        return t;
    }

    void expect(std::vector<Token> &tokens, size_t &i, const std::string &val)
    {
        if (i >= tokens.size())
            throw std::runtime_error(_path + ": unexpected end of file, expected '" + val + "'");
        if (tokens[i].value != val)
            throwSyntaxError(tokens[i]);
        ++i;
    }

    bool atEnd(std::vector<Token> &tokens, size_t i, const std::string &closing)
    {
        return i >= tokens.size() || tokens[i].value == closing;
    }

    ServerConfig parseServerBlock(std::vector<Token> &tokens, size_t &i)
    {
        ServerConfig cfg;
        while (!atEnd(tokens, i, "}")) {
            if (tokens[i].value == "listen")       parseListen(cfg, tokens, i);
            else if (tokens[i].value == "server_name") parseServerName(cfg, tokens, i);
            else if (tokens[i].value == "error_page")  parseErrorPage(cfg, tokens, i);
            else if (tokens[i].value == "client_max_body_size") parseMaxBody(cfg, tokens, i);
            else if (tokens[i].value == "location") cfg.locations.push_back(parseLocationBlock(tokens, i));
            else
                throwSyntaxError(tokens[i]); // [Adım 7] tanınmayan direktif -> satır no ile hata
        }
        if (i >= tokens.size())
            throw std::runtime_error(_path + ": unexpected end of file inside 'server' block");
        return cfg;
    }

    void parseListen(ServerConfig &cfg, std::vector<Token> &tokens, size_t &i)
    {
        expect(tokens, i, "listen");
        if (i >= tokens.size()) throwUnexpectedEof();
        std::string value = tokens[i].value;
        ++i;
        size_t colon = value.find(':');
        if (colon == std::string::npos) {
            cfg.host = "0.0.0.0";
            cfg.port = std::atoi(value.c_str());
        } else {
            cfg.host = value.substr(0, colon);
            cfg.port = std::atoi(value.substr(colon + 1).c_str());
        }
        expect(tokens, i, ";");
    }

    void parseServerName(ServerConfig &cfg, std::vector<Token> &tokens, size_t &i)
    {
        expect(tokens, i, "server_name");
        while (i < tokens.size() && tokens[i].value != ";") {
            cfg.serverNames.push_back(tokens[i].value);
            ++i;
        }
        expect(tokens, i, ";");
    }

    void parseErrorPage(ServerConfig &cfg, std::vector<Token> &tokens, size_t &i)
    {
        expect(tokens, i, "error_page");
        std::vector<std::string> parts;
        while (i < tokens.size() && tokens[i].value != ";") {
            parts.push_back(tokens[i].value);
            ++i;
        }
        expect(tokens, i, ";");
        if (parts.size() < 2)
            throw std::runtime_error(_path + ": error_page needs at least one code and a path");
        const std::string &pagePath = parts.back();
        for (size_t k = 0; k + 1 < parts.size(); ++k)
            cfg.errorPages[std::atoi(parts[k].c_str())] = pagePath;
    }

    void parseMaxBody(ServerConfig &cfg, std::vector<Token> &tokens, size_t &i)
    {
        expect(tokens, i, "client_max_body_size");
        if (i >= tokens.size()) throwUnexpectedEof();
        cfg.clientMaxBodySize = static_cast<size_t>(std::atol(tokens[i].value.c_str()));
        ++i;
        expect(tokens, i, ";");
    }

    LocationConfig parseLocationBlock(std::vector<Token> &tokens, size_t &i)
    {
        expect(tokens, i, "location");
        LocationConfig loc;
        if (i >= tokens.size()) throwUnexpectedEof();
        loc.path = tokens[i].value;
        ++i;
        expect(tokens, i, "{");

        while (!atEnd(tokens, i, "}")) {
            const std::string &directive = tokens[i].value;
            if (directive == "root") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.root = tokens[i].value; ++i;
                expect(tokens, i, ";");
            } else if (directive == "index") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.index = tokens[i].value; ++i;
                expect(tokens, i, ";");
            } else if (directive == "methods") {
                ++i;
                while (i < tokens.size() && tokens[i].value != ";") { loc.methods.push_back(tokens[i].value); ++i; }
                expect(tokens, i, ";");
            } else if (directive == "autoindex") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.autoindex = (tokens[i].value == "on"); ++i;
                expect(tokens, i, ";");
            } else if (directive == "upload_path") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.uploadPath = tokens[i].value; ++i;
                expect(tokens, i, ";");
            } else if (directive == "cgi_extension") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.cgiExtension = tokens[i].value; ++i;
                expect(tokens, i, ";");
            } else if (directive == "return") {
                ++i;
                if (i >= tokens.size()) throwUnexpectedEof();
                loc.redirectTarget = tokens[i].value; ++i;
                expect(tokens, i, ";");
            } else {
                throwSyntaxError(tokens[i]);
            }
        }
        if (i >= tokens.size())
            throw std::runtime_error(_path + ": unexpected end of file inside 'location' block");
        expect(tokens, i, "}");
        return loc;
    }

    void throwUnexpectedEof()
    {
        throw std::runtime_error(_path + ": unexpected end of file");
    }

    void throwSyntaxError(const Token &t) // "config.conf:42: unexpected token 'foo'"
    {
        throw std::runtime_error(_path + ":" + toString(t.line) + ": unexpected token '" + t.value + "'");
    }

    std::string toString(int n) const
    {
        std::ostringstream oss;
        oss << n;
        return oss.str();
    }

    std::string _path;
};

#endif
