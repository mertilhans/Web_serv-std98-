#ifndef FS_UTILS_HPP
#define FS_UTILS_HPP

#include <string>

// Stateless dosya-sistemi yardimcilari -- Server (dosya okuma, path
// traversal kontrolu) ve ileride baska modullerin de kullanabilecegi,
// hicbir parser/config-politikasi icermeyen genel islemler.
// Not: "gecerli bir root/upload_dir/index-dosyasi mi" gibi ConfigParser'a
// ozel KABUL KRITERLERI (PATH_MAX siniri, belirli on-ekler vb.) burada
// DEGIL -- onlar bu parser'in kendi politikasi, ConfigParser'in private
// metodu olarak kaliyor.
class FsUtils
{
public:
	static bool	readWholeFile(const std::string& path, std::string& content);
	// path icinde ".." segmenti var mi (path traversal korumasi).
	static bool	hasDotDotSegment(const std::string& path);

	// stat()/access() sarmalayicilari -- "bu yol var mi / dizin mi / duz
	// dosya mi / okunabilir mi" sorularini tek yerde topluyor, RequestHandler
	// ve CgiProcess gibi modullerin ham stat()/access() cagrisini
	// tekrar tekrar yazmasina gerek kalmiyor.
	static bool	exists(const std::string& path);
	static bool	isDirectory(const std::string& path);
	static bool	isRegularFile(const std::string& path);
	static bool	isReadable(const std::string& path);

private:
	// Sinif hic orneklenmez (sadece static metodlar) -- OCF yine de burada,
	// private'da, hicbir zaman kullanilmayacak sekilde tanimli tutuluyor.
	FsUtils();
	~FsUtils();
	FsUtils(const FsUtils& ref);
	FsUtils&	operator=(const FsUtils& ref);
};

#endif
