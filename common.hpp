#ifndef COMMON_HPP
#define COMMON_HPP

#include <string>
#include <fstream>
#include <sstream>

// Dosyanın tüm içeriğini `content` içine okur.
// Dosya açılamazsa false, başarılı olursa true döner (throw etmez, çünkü
// çağıran taraf -ConfigParser- hata mesajını kendi bağlamıyla üretmek istiyor).
inline bool readWholeFile(const std::string &path, std::string &content)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

#endif
