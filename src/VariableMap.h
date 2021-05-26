#pragma once
#include <map>
#include <set>

#include "CommonTypes.h"

struct VariableMap {
    std::map<std::string, std::string> variables;

    std::string  GetStrValue(const std::string& key) const;
    std::string  GetStrValueFiltered(const std::string& key) const;
    StringVector GetListValue(const std::string& key) const;
    std::string  GetMappedValue(const std::string& key, const std::map<std::string, std::string>& mapping) const;
    bool         GetBoolValue(const std::string& key, bool def = false) const;

    std::string& operator[](const std::string& key) { return variables[key]; }

    void ParseFromXml(const std::string& blockName, const std::string& data);
};

std::ostream& operator<<(std::ostream& os, const VariableMap& info);

std::string  joinVector(const StringVector& lst, char sep = ' ');
StringVector strToList(const std::string& val, char sep = ';');
