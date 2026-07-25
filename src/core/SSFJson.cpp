#include "core/SSFJson.hpp"

#include "SimpleJson.hpp"
#include "SimpleXml.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace neossf {
namespace {

std::string trim(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool looksLikeJsonNumber(std::string_view text) {
    if (text.empty()) return false;
    std::size_t pos = 0;
    if (text[pos] == '-') ++pos;
    if (pos >= text.size()) return false;
    if (text[pos] == '0') {
        ++pos;
    } else if (std::isdigit(static_cast<unsigned char>(text[pos]))) {
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    } else {
        return false;
    }
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
        if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    return pos == text.size();
}

std::string numberOrZero(std::string text) {
    text = trim(std::move(text));
    return looksLikeJsonNumber(text) ? text : std::string("0");
}

std::string optionalStringOrNumber(const neojson::Value& object, const std::string& key, const std::string& fallback = {}) {
    const neojson::Value* value = object.find(key);
    if (!value || value->isNull()) return fallback;
    return value->asText(("JSON member '" + key + "'").c_str());
}

std::string optionalString(const neojson::Value& object, const std::string& key, const std::string& fallback = {}) {
    const neojson::Value* value = object.find(key);
    if (!value || value->isNull()) return fallback;
    return value->asString(("JSON member '" + key + "'").c_str());
}

std::string requireStringOrNumber(const neojson::Value& object, const std::string& key) {
    return object.at(key).asText(("JSON member '" + key + "'").c_str());
}

std::string escapeXmlAttr(const std::string& text) {
    return neoxml::escapeAttribute(text);
}

} // namespace

std::string ssfXmlToJson(const std::string& ssfXml) {
    const neoxml::Node root = neoxml::parse(ssfXml);
    if (root.name != "ssf") throw std::invalid_argument("SSF XML root must be <ssf>.");
    std::ostringstream out;
    out << "{\n  \"format\": \"SSF\",\n  \"version\": 1";
    const std::string ssfFormat = root.attribute("format");
    if (!ssfFormat.empty()) out << ",\n  \"ssfFormat\": " << neojson::quote(ssfFormat);
    out << ",\n  \"sounds\": [\n";
    std::size_t emitted = 0;
    for (const auto& child : root.children) {
        if (child.name != "sound") continue;
        if (emitted) out << ",\n";
        out << "    { \"id\": " << numberOrZero(child.attribute("id"));
        const std::string index = child.attribute("index");
        if (!index.empty()) out << ", \"index\": " << numberOrZero(index);
        const std::string label = child.attribute("label");
        if (!label.empty()) out << ", \"label\": " << neojson::quote(label);
        const std::string displayLabel = child.attribute("displayLabel");
        if (!displayLabel.empty()) out << ", \"displayLabel\": " << neojson::quote(displayLabel);
        const std::string strref = child.attribute("strref");
        out << ", \"strref\": " << (strref.empty() ? std::string("-1") : numberOrZero(strref));
        const auto soundFileIt = child.attributes.find("soundFile");
        const auto soundFileLowerIt = child.attributes.find("soundfile");
        const auto legacySoundIt = child.attributes.find("sound");
        std::string soundFile;
        bool hasSoundFile = false;
        if (soundFileIt != child.attributes.end()) {
            soundFile = soundFileIt->second;
            hasSoundFile = true;
        } else if (soundFileLowerIt != child.attributes.end()) {
            soundFile = soundFileLowerIt->second;
            hasSoundFile = true;
        } else if (legacySoundIt != child.attributes.end()) {
            soundFile = legacySoundIt->second;
            hasSoundFile = true;
        } else {
            soundFile = trim(child.text);
            hasSoundFile = !soundFile.empty();
        }
        if (hasSoundFile) out << ", \"soundFile\": " << neojson::quote(soundFile);
        const std::string text = child.attribute("text");
        if (!text.empty()) out << ", \"text\": " << neojson::quote(text);
        const std::string tlkSound = child.attribute("tlkSound");
        if (!tlkSound.empty()) out << ", \"tlkSound\": " << neojson::quote(tlkSound);
        out << " }";
        ++emitted;
    }
    out << "\n  ]\n}\n";
    return out.str();
}

std::string ssfJsonToXml(const std::string& jsonText) {
    const neojson::Value root = neojson::parse(jsonText);
    if (!root.isObject()) throw std::invalid_argument("SSF JSON must be an object.");
    std::ostringstream out;
    const std::string ssfFormat = optionalString(root, "ssfFormat");
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ssf";
    if (!ssfFormat.empty()) out << " format=\"" << escapeXmlAttr(ssfFormat) << "\"";
    out << ">\n";
    const auto& sounds = root.at("sounds").asArray("SSF JSON sounds");
    for (const auto& sound : sounds) {
        sound.asObject("SSF JSON sound");
        out << "  <sound id=\"" << escapeXmlAttr(requireStringOrNumber(sound, "id")) << "\"";
        const std::string index = optionalStringOrNumber(sound, "index");
        if (!index.empty()) out << " index=\"" << escapeXmlAttr(index) << "\"";
        const std::string label = optionalString(sound, "label");
        if (!label.empty()) out << " label=\"" << escapeXmlAttr(label) << "\"";
        const std::string displayLabel = optionalString(sound, "displayLabel");
        if (!displayLabel.empty()) out << " displayLabel=\"" << escapeXmlAttr(displayLabel) << "\"";
        const std::string strref = optionalStringOrNumber(sound, "strref", "-1");
        out << " strref=\"" << escapeXmlAttr(strref.empty() ? std::string("-1") : strref) << "\"";
        const neojson::Value* soundFileValue = sound.find("soundFile");
        if (soundFileValue && !soundFileValue->isNull()) {
            out << " soundFile=\"" << escapeXmlAttr(soundFileValue->asString("JSON member 'soundFile'")) << "\"";
        }
        const std::string text = optionalString(sound, "text");
        if (!text.empty()) out << " text=\"" << escapeXmlAttr(text) << "\"";
        const std::string tlkSound = optionalString(sound, "tlkSound");
        if (!tlkSound.empty()) out << " tlkSound=\"" << escapeXmlAttr(tlkSound) << "\"";
        out << "/>\n";
    }
    out << "</ssf>\n";
    return out.str();
}

} // namespace neossf
