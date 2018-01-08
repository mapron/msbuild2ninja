#include "VariableMap.h"

#include <regex>
#include <sstream>

std::string VariableMap::GetStrValue(const std::string & key) const
{
	auto it = variables.find(key);
	if (it == variables.cend())
		return "";
	return it->second;
}

std::string VariableMap::GetStrValueFiltered(const std::string &key) const
{
	auto result = GetStrValue(key);
	result = std::regex_replace(result, std::regex("\\%\\(\\w+\\)"), " ");
	return result;
}

StringVector VariableMap::GetListValue(const std::string & key) const
{
	std::string val = GetStrValue(key);
	return strToList(val);
}

std::string VariableMap::GetMappedValue(const std::string & key, const std::map<std::string, std::string> & mapping) const
{
	std::string val = GetStrValue(key);
	auto it = mapping.find(val);
	if (it == mapping.cend())
		return "";

	return it->second;
}

bool VariableMap::GetBoolValue(const std::string & key, bool def) const
{
	std::string val = GetStrValue(key);
	if (val == "true")
		return true;

	if (val == "")
		return def;

	return false;
}

void VariableMap::ParseFromXml(const std::string &blockName, const std::string &data)
{
	const auto blockBegin = data.find("<" + blockName + ">");
	if (blockBegin == std::string::npos)
		return;
	const auto blockEnd = data.find("</" + blockName + ">", blockBegin);
	if (blockEnd == std::string::npos)
		return;
	std::regex exp2(R"rx(<(\w+)>([^<]+)</)rx");
	std::smatch res2;
	std::string::const_iterator searchStart2( data.cbegin() + blockBegin );
	while ( std::regex_search( searchStart2, data.cbegin() + blockEnd, res2, exp2 ) )
	{
		const std::string key = res2[1].str();
		const std::string value = res2[2].str();
	  //  std::cerr << key << "=" << value << std::endl;
		variables[key] = value;
		searchStart2 += res2.position() + res2.length();
	}
}

std::string joinVector(const StringVector & lst, char sep)
{
	std::ostringstream ss;
	ss << sep;
	for (const auto & el : lst)
		ss << el << sep;
	return ss.str();
}

StringVector strToList(const std::string & val, char sep)
{
	std::stringstream ss;
	ss.str(val);
	std::string item;
	StringVector result;
	while (std::getline(ss, item, sep))
	{
		if (!item.empty() && item.at(0) != '%')
		{
			result.push_back(item);
		}
	}
	return result;
}

std::ostream &operator <<(std::ostream &os, const VariableMap &info) {
	for (const auto & dep : info.variables)
		os << "\n\t\t" << dep.first << "=" << dep.second;
	return os;
}
