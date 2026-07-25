#include "core/AppModel.hpp"

#include "core/Common.hpp"
#include "core/SSFFile.hpp"
#include "SimpleXml.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <system_error>
#include <optional>
#include <utility>
#include <stdexcept>
#include <sstream>

namespace neossf {
namespace {

std::string eraseChar(std::string text, char value) {
    text.erase(std::remove(text.begin(), text.end(), value), text.end());
    return text;
}

std::string replaceChar(std::string text, char oldValue, char newValue) {
    std::replace(text.begin(), text.end(), oldValue, newValue);
    return text;
}

std::string trimXmlText(std::string text) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::uint32_t parseXmlUInt32(const std::string& text, const std::string& what) {
    if (text.empty()) throw std::invalid_argument("Missing XML " + what + ".");
    std::uint32_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::invalid_argument("Invalid XML " + what + ": " + text);
    }
    return value;
}

std::string lowerAsciiLocal(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::size_t ssfColumnIndex(const neotabular::Table& table, const std::string& name) {
    const std::string want = lowerAsciiLocal(name);
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        if (lowerAsciiLocal(table.columns[i]) == want) return i;
    }
    throw std::invalid_argument("Imported SSF table is missing required column: " + name);
}

std::size_t optionalSsfColumnIndex(const neotabular::Table& table, const std::string& name) {
    const std::string want = lowerAsciiLocal(name);
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        if (lowerAsciiLocal(table.columns[i]) == want) return i;
    }
    return table.columns.size();
}

std::size_t parseOneBasedIndex(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("Missing SSF row index.");
    std::size_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') throw std::invalid_argument("Invalid SSF row index: " + text);
        value = value * 10u + static_cast<std::size_t>(ch - '0');
        if (value > 100000u) throw std::invalid_argument("SSF row index is unreasonably large: " + text);
    }
    if (value == 0) throw std::invalid_argument("SSF row indexes are one-based.");
    return value;
}

SSFFormat formatForSoundFiles(const std::vector<std::string>& soundFiles, SSFFormat fallback) {
    std::size_t maxLen = 0;
    bool any = false;
    for (const auto& sound : soundFiles) {
        if (!sound.empty()) {
            any = true;
            maxLen = std::max(maxLen, sound.size());
        }
    }
    if (!any) return fallback;
    if (maxLen > 32u) {
        throw std::invalid_argument("Imported SSF SoundFile exceeds the 32-character NWN2 ResRef limit.");
    }
    if (fallback == SSFFormat::NWN2_V11) return fallback;
    if (fallback == SSFFormat::NWN_V10 && maxLen <= 16u) return fallback;
    return maxLen > 16u ? SSFFormat::NWN2_V11 : SSFFormat::NWN_V10;
}

void validateSoundFilesForFormat(const std::vector<std::string>& soundFiles, SSFFormat format) {
    const std::size_t limit = ssfSoundResRefLimit(format);
    for (const std::string& sound : soundFiles) {
        if (sound.empty()) continue;
        if (format == SSFFormat::KotOR_V11) {
            throw std::invalid_argument(
                "KotOR SSF V1.1 stores StrRefs only; SoundFile values belong to NWN/NWN2 SSF files.");
        }
        if (sound.size() > limit) {
            throw std::invalid_argument(
                "Imported SSF SoundFile exceeds the " + std::to_string(limit) +
                "-character limit for " + ssfFormatName(format) + ": " + sound);
        }
    }
}

std::optional<SSFFormat> declaredTableFormat(const neotabular::Table& table) {
    const std::size_t formatCol = optionalSsfColumnIndex(table, "SSFFormat");
    if (formatCol >= table.columns.size()) return std::nullopt;
    std::optional<SSFFormat> result;
    for (const auto& row : table.rows) {
        if (formatCol >= row.size() || row[formatCol].empty()) continue;
        const SSFFormat parsed = parseSsfFormat(row[formatCol]);
        if (result && *result != parsed) {
            throw std::invalid_argument("Imported SSF table contains conflicting SSFFormat values.");
        }
        result = parsed;
    }
    return result;
}

} // namespace

void ensureSsfTableMetadataRow(neotabular::Table& table, SSFFormat format) {
    if (!table.rows.empty()) return;
    if (table.columns.empty()) {
        table.columns = {"Index", "Label", "StrRef", "SoundFile", "Text", "Sound", "SSFFormat"};
    }
    std::vector<std::string> row(table.columns.size());
    const std::size_t formatCol = optionalSsfColumnIndex(table, "SSFFormat");
    if (formatCol < row.size()) row[formatCol] = ssfFormatToken(format);
    table.rows.push_back(std::move(row));
}

AppModel::AppModel() {
    entries_.fill(kUnsetStrRef);
    soundFiles_.assign(kSoundsetEntryCount, std::string{});
}

const std::array<UInt32, kSoundsetEntryCount>& AppModel::entries() const noexcept { return entries_; }
std::array<UInt32, kSoundsetEntryCount>& AppModel::entries() noexcept { return entries_; }
std::size_t AppModel::entryCount() const noexcept { return entries_.size() + extraSsfEntries_.size(); }

std::string AppModel::entryLabel(std::size_t zeroBasedRow) const {
    if (zeroBasedRow < entryCount()) {
        return soundsetDisplayLabel(zeroBasedRow, entryCount());
    }
    throw std::out_of_range("Soundset row index out of bounds.");
}

UInt32 AppModel::entryValue(std::size_t zeroBasedRow) const {
    if (zeroBasedRow < entries_.size()) {
        return entries_[zeroBasedRow];
    }
    const std::size_t extraIndex = zeroBasedRow - entries_.size();
    if (extraIndex < extraSsfEntries_.size()) {
        return extraSsfEntries_[extraIndex];
    }
    throw std::out_of_range("Soundset row index out of bounds.");
}

void AppModel::setEntryValue(std::size_t zeroBasedRow, UInt32 value) {
    if (soundFiles_.size() <= zeroBasedRow) soundFiles_.resize(zeroBasedRow + 1u);
    if (zeroBasedRow < entries_.size()) {
        entries_[zeroBasedRow] = value;
        return;
    }
    const std::size_t extraIndex = zeroBasedRow - entries_.size();
    if (extraIndex < extraSsfEntries_.size()) {
        extraSsfEntries_[extraIndex] = value;
        return;
    }
    throw std::out_of_range("Soundset row index out of bounds.");
}

TalkTable& AppModel::tlkData() noexcept { return tlkData_; }
const TalkTable& AppModel::tlkData() const noexcept { return tlkData_; }

ResEntry AppModel::getTlkString(UInt32 strRef) const {
    ResEntry result;

    for (const TalkString& entry : tlkData_.entries()) {
        if (entry.strRef == strRef) {
            result.text = replaceChar(eraseChar(entry.text, '\r'), '\n', ' ');
            result.sound = entry.soundString();
            return result;
        }
    }

    result.text = "NOT FOUND";
    result.sound = "N/A";
    return result;
}

void AppModel::loadTlk(const std::filesystem::path& tlkFile) {
    TalkTable loaded;
    loaded.load(tlkFile.string());
    tlkData_ = std::move(loaded);
    tlkFile_ = tlkFile;
    tlkLoaded_ = true;
    tlkModified_ = false;
    tlkBaselineCount_ = tlkData_.count();
}

void AppModel::loadSsf(const std::filesystem::path& ssfFile, bool precheckExists) {
    if (precheckExists) {
        std::error_code existsError;
        if (!std::filesystem::is_regular_file(ssfFile, existsError) || existsError) {
            throw std::runtime_error("No valid file specified to load! Aborting...");
        }
    }

    SSFFile loaded(ssfFile);
    entries_ = loaded.entries();
    extraSsfEntries_ = loaded.extraEntries();
    soundFiles_ = loaded.soundFiles();
    if (soundFiles_.size() < entryCount()) soundFiles_.resize(entryCount());
    ssfFormat_ = loaded.format();
    ssfFile_ = ssfFile;
}

void AppModel::newSsf() {
    ssfFile_ = "new.ssf";
    ssfFormat_ = SSFFormat::KotOR_V11;
    resetEntries();
}

void AppModel::modifySlot(std::size_t oneBasedRow, const std::string& strRefText) {
    if (oneBasedRow < 1 || oneBasedRow > entryCount()) {
        throw std::out_of_range("Soundset row index out of bounds.");
    }
    setEntryValue(oneBasedRow - 1, parseSlotValue(strRefText));
}

void AppModel::modifySoundFile(std::size_t oneBasedRow, const std::string& soundFile) {
    if (oneBasedRow < 1 || oneBasedRow > entryCount()) {
        throw std::out_of_range("Soundset row index out of bounds.");
    }
    const std::size_t index = oneBasedRow - 1u;
    if (soundFiles_.size() <= index) soundFiles_.resize(index + 1u);
    soundFiles_[index] = soundFile;
    if (!soundFile.empty() && ssfFormat_ == SSFFormat::KotOR_V11) {
        ssfFormat_ = soundFile.size() > 16u ? SSFFormat::NWN2_V11 : SSFFormat::NWN_V10;
    }
}

UInt32 AppModel::addTlkEntryAndAssign(std::size_t oneBasedRow, const std::string& text, const std::string& resref) {
    if (!tlkLoaded_) {
        throw TlkError("Unable to add new entry. No TLK file is open!");
    }
    if (oneBasedRow < 1 || oneBasedRow > entryCount()) {
        throw std::out_of_range("Soundset row index out of bounds.");
    }

    const UInt32 savedStrRef = tlkData_.count();

    TalkString entry;
    entry.flags = kTextPresent | kSoundPresent | kSoundLengthPresent;
    entry.volumeVariance = 0;
    entry.pitchVariance = 0;
    entry.soundLength = 0.0f;
    entry.soundResref = neotlk::ResRef::fromString(resref);
    entry.text = eraseChar(text, '\r');

    tlkData_.addEntry(std::move(entry));
    tlkModified_ = true;
    setEntryValue(oneBasedRow - 1, savedStrRef);
    if (ssfFormat_ != SSFFormat::KotOR_V11 && !resref.empty()) {
        modifySoundFile(oneBasedRow, resref);
    }
    return savedStrRef;
}

void AppModel::saveTlk(const std::filesystem::path& tlkFile) {
    tlkData_.save(tlkFile.string());
    tlkFile_ = tlkFile;
    tlkModified_ = false;
}

void AppModel::saveSsf(const std::filesystem::path& ssfFile, std::function<void()> afterSave) {
    SSFFile output;
    output.newFile(ssfFile);
    output.setFormat(ssfFormat_);
    output.entries() = entries_;
    output.extraEntries() = extraSsfEntries_;
    output.soundFiles() = soundFiles_;
    if (output.soundFiles().size() < entryCount()) output.soundFiles().resize(entryCount());
    output.save(ssfFile);

    ssfFile_ = ssfFile;
    if (afterSave) {
        afterSave();
    }
}

SSFFormat AppModel::ssfFormat() const noexcept { return ssfFormat_; }
void AppModel::setSsfFormat(SSFFormat format) noexcept { ssfFormat_ = format; }

std::string AppModel::soundFile(std::size_t zeroBasedRow) const {
    return zeroBasedRow < soundFiles_.size() ? soundFiles_[zeroBasedRow] : std::string{};
}

void AppModel::resetEntries() {
    entries_.fill(kUnsetStrRef);
    extraSsfEntries_.clear();
    soundFiles_.assign(kSoundsetEntryCount, std::string{});
}

neotabular::Table AppModel::toTable(bool includeTlkPreview) const {
    neotabular::Table table;
    table.columns = {"Index", "Label", "StrRef", "SoundFile", "Text", "Sound", "SSFFormat"};
    for (std::size_t i = 0; i < entryCount(); ++i) {
        const UInt32 value = entryValue(i);
        std::string text;
        std::string sound;
        if (includeTlkPreview && tlkLoaded_ && value != kUnsetStrRef) {
            const ResEntry resolved = getTlkString(value);
            text = resolved.text;
            sound = resolved.sound;
        }
        table.rows.push_back({std::to_string(i + 1), entryLabel(i),
                              value == kUnsetStrRef ? std::string("-1") : std::to_string(value),
                              soundFile(i), text, sound, ssfFormatToken(ssfFormat_)});
    }
    return table;
}

void AppModel::importSsfTable(const neotabular::Table& table) {
    const std::size_t indexCol = ssfColumnIndex(table, "Index");
    const std::size_t strRefCol = ssfColumnIndex(table, "StrRef");
    const std::size_t soundFileCol = optionalSsfColumnIndex(table, "SoundFile");
    const std::optional<SSFFormat> declaredFormat = declaredTableFormat(table);

    std::vector<std::string> importedSounds;
    if (soundFileCol < table.columns.size()) {
        importedSounds.reserve(table.rows.size());
        for (const auto& row : table.rows) {
            if (soundFileCol < row.size()) importedSounds.push_back(row[soundFileCol]);
        }
    }
    const SSFFormat targetFormat = declaredFormat
        ? *declaredFormat
        : formatForSoundFiles(importedSounds, ssfFormat_);
    validateSoundFilesForFormat(importedSounds, targetFormat);
    ssfFormat_ = targetFormat;

    for (const auto& row : table.rows) {
        if (indexCol >= row.size() || strRefCol >= row.size()) continue;
        if (row[indexCol].empty() && row[strRefCol].empty()) continue; // Metadata-only flat-export row.
        const std::size_t oneBased = parseOneBasedIndex(row[indexCol]);
        while (oneBased > entryCount()) {
            extraSsfEntries_.push_back(kUnsetStrRef);
        }
        if (soundFiles_.size() < entryCount()) soundFiles_.resize(entryCount());
        modifySlot(oneBased, row[strRefCol]);
        if (soundFileCol < row.size()) {
            modifySoundFile(oneBased, row[soundFileCol]);
        }
    }
}

std::string AppModel::toXml(bool includeTlkPreview) const {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<ssf format=\"" << neoxml::escapeAttribute(ssfFormatToken(ssfFormat_)) << "\">\n";
    const std::size_t total = entryCount();
    for (std::size_t i = 0; i < total; ++i) {
        const UInt32 value = entryValue(i);
        const std::string directSound = soundFile(i);
        out << "  <sound id=\"" << i << "\"";
        out << " index=\"" << (i + 1u) << "\"";
        const std::string label = soundsetXmlLabel(i, total);
        if (!label.empty()) {
            out << " label=\"" << neoxml::escapeAttribute(label) << "\"";
        }
        const std::string displayLabel = entryLabel(i);
        if (!displayLabel.empty()) {
            out << " displayLabel=\"" << neoxml::escapeAttribute(displayLabel) << "\"";
        }
        out << " strref=\"" << (value == kUnsetStrRef ? std::string("-1") : std::to_string(value)) << "\"";
        if (ssfFormat_ != SSFFormat::KotOR_V11 || !directSound.empty()) {
            out << " soundFile=\"" << neoxml::escapeAttribute(directSound) << "\"";
        }
        if (includeTlkPreview && tlkLoaded_ && value != kUnsetStrRef) {
            const ResEntry resolved = getTlkString(value);
            if (!resolved.text.empty()) {
                out << " text=\"" << neoxml::escapeAttribute(resolved.text) << "\"";
            }
            if (!resolved.sound.empty()) {
                out << " tlkSound=\"" << neoxml::escapeAttribute(resolved.sound) << "\"";
            }
        }
        out << "/>\n";
    }
    out << "</ssf>\n";
    return out.str();
}

void AppModel::importXml(const std::string& xmlText) {
    const neoxml::Node root = neoxml::parse(xmlText);
    if (root.name != "ssf") {
        throw std::invalid_argument("XML does not describe an SSF file: expected <ssf> root.");
    }

    std::optional<SSFFormat> declaredFormat;
    const std::string rootFormat = root.attribute("format");
    if (!rootFormat.empty()) declaredFormat = parseSsfFormat(rootFormat);

    std::vector<std::string> importedSounds;
    importedSounds.reserve(root.children.size());
    for (const auto& child : root.children) {
        if (child.name != "sound") {
            throw std::invalid_argument("XML SSF contains unexpected element <" + child.name + ">; expected <sound>.");
        }
        const auto soundFileIt = child.attributes.find("soundFile");
        const auto soundFileLowerIt = child.attributes.find("soundfile");
        const auto legacySoundIt = child.attributes.find("sound");
        if (soundFileIt != child.attributes.end()) importedSounds.push_back(soundFileIt->second);
        else if (soundFileLowerIt != child.attributes.end()) importedSounds.push_back(soundFileLowerIt->second);
        else if (legacySoundIt != child.attributes.end()) importedSounds.push_back(legacySoundIt->second);
        else importedSounds.push_back(trimXmlText(child.text));
    }

    const SSFFormat targetFormat = declaredFormat
        ? *declaredFormat
        : formatForSoundFiles(importedSounds, ssfFormat_);
    validateSoundFilesForFormat(importedSounds, targetFormat);

    resetEntries();
    ssfFormat_ = targetFormat;
    for (const auto& child : root.children) {
        const std::size_t zeroBased = static_cast<std::size_t>(parseXmlUInt32(child.attribute("id"), "sound id"));
        while (zeroBased >= entryCount()) {
            extraSsfEntries_.push_back(kUnsetStrRef);
        }
        if (soundFiles_.size() < entryCount()) soundFiles_.resize(entryCount());
        UInt32 strRef = kUnsetStrRef;
        const std::string strRefText = child.attribute("strref");
        if (!strRefText.empty()) {
            strRef = parseSlotValue(strRefText);
        }
        setEntryValue(zeroBased, strRef);
        const auto soundFileIt = child.attributes.find("soundFile");
        const auto soundFileLowerIt = child.attributes.find("soundfile");
        const auto legacySoundIt = child.attributes.find("sound");
        std::string sound;
        bool hasDirectSoundField = false;
        if (soundFileIt != child.attributes.end()) {
            sound = soundFileIt->second;
            hasDirectSoundField = true;
        } else if (soundFileLowerIt != child.attributes.end()) {
            sound = soundFileLowerIt->second;
            hasDirectSoundField = true;
        } else if (legacySoundIt != child.attributes.end()) {
            sound = legacySoundIt->second;
            hasDirectSoundField = true;
        } else {
            sound = trimXmlText(child.text);
            hasDirectSoundField = !sound.empty();
        }
        if (hasDirectSoundField) modifySoundFile(zeroBased + 1u, sound);
    }
}

const std::filesystem::path& AppModel::tlkFile() const noexcept { return tlkFile_; }
const std::filesystem::path& AppModel::ssfFile() const noexcept { return ssfFile_; }
void AppModel::setTlkFile(const std::filesystem::path& path) { tlkFile_ = path; }
void AppModel::setSsfFile(const std::filesystem::path& path) { ssfFile_ = path; }
bool AppModel::tlkLoaded() const noexcept { return tlkLoaded_; }
void AppModel::setTlkLoaded(bool loaded) noexcept {
    tlkLoaded_ = loaded;
    if (!loaded) tlkBaselineCount_ = 0;
}
bool AppModel::tlkModified() const noexcept { return tlkModified_; }
void AppModel::setTlkModified(bool modified) noexcept { tlkModified_ = modified; }
UInt32 AppModel::tlkBaselineCount() const noexcept { return tlkBaselineCount_; }

UInt32 AppModel::parseSlotValue(const std::string& strRefText) {
    if (strRefText.empty() || strRefText == "-1") {
        return kUnsetStrRef;
    }
    if (strRefText.front() == '-' || strRefText.front() == '+') {
        throw std::invalid_argument("'" + strRefText + "' is not a valid unsigned StrRef value");
    }

    UInt32 value = 0;
    const char* first = strRefText.data();
    const char* last = strRefText.data() + strRefText.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last || value == kUnsetStrRef) {
        throw std::invalid_argument("'" + strRefText + "' is not a valid unsigned StrRef value");
    }
    return value;
}

} // namespace neossf
