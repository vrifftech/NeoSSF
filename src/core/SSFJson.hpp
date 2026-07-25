#pragma once

#include <string>

namespace neossf {


std::string ssfXmlToJson(const std::string& ssfXml);
std::string ssfJsonToXml(const std::string& jsonText);

} // namespace neossf
