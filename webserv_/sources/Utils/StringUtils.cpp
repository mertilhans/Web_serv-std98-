#include "StringUtils.hpp"
#include <cctype>

// OCF -- private, hic cagrilmaz (sinif sadece static metodlarla kullanilir).
StringUtils::StringUtils()
{
}

StringUtils::~StringUtils()
{
}

StringUtils::StringUtils(const StringUtils& ref)
{
	(void)ref;
}

StringUtils&	StringUtils::operator=(const StringUtils& ref)
{
	(void)ref;
	return (*this);
}

std::string	StringUtils::toLower(const std::string& str)
{
	std::string	result = str;

	for (size_t i = 0; i < result.length(); i++)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return (result);
}

std::string	StringUtils::toUpper(const std::string& str)
{
	std::string	result = str;

	for (size_t i = 0; i < result.length(); i++)
		result[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[i])));
	return (result);
}

std::string	StringUtils::trimLeadingSpaces(const std::string& str)
{
	size_t	start = str.find_first_not_of(' ');

	return (start == std::string::npos) ? "" : str.substr(start);
}

bool	StringUtils::isNumeric(const std::string& str)
{
	if (str.empty())
		return (false);
	for (size_t i = 0; i < str.length(); i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(str[i])))
			return (false);
	}
	return (true);
}
