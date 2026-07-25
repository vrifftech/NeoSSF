#include "core/SSFFile.hpp"

#include "core/BinaryIO.hpp"
#include "core/Common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace neossf {
namespace {

constexpr UInt32 kKotORSsfHeaderSize = 12u;
constexpr UInt32 kNwnSsfOffsetTable = 0x28u;
constexpr UInt32 kMaxReasonableSsfEntries = 4096u;

std::uintmax_t fileSizeChecked(const std::filesystem::path& filename) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(filename, ec);
    if (ec) {
        throw SSFError("Unable to inspect selected SSF file \"" + filename.string() + "\".", 1);
    }
    return size;
}

std::string readFixedAsciiString(std::istream& in, std::size_t width, const std::string& what) {
    std::string raw(width, '\0');
    binary::readExact(in, raw.data(), raw.size(), what);
    const auto nul = std::find(raw.begin(), raw.end(), '\0');
    raw.erase(nul, raw.end());
    return raw;
}

void writeFixedAsciiString(std::ostream& out, const std::string& value, std::size_t width, const std::string& what) {
    std::string raw(width, '\0');
    const std::size_t count = std::min(width, value.size());
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch > 0x7Fu) {
            throw SSFError("SSF sound ResRefs must be ASCII-only: " + value, 1);
        }
        raw[i] = static_cast<char>(ch);
    }
    binary::writeExact(out, raw.data(), raw.size(), what);
}

bool isNwn2V11Layout(std::uintmax_t size, UInt32 entryCount, UInt32 offsetTable) {
    if (entryCount == 0 || entryCount > kMaxReasonableSsfEntries) return false;
    if (offsetTable >= size) return false;
    const std::uintmax_t minimum = static_cast<std::uintmax_t>(offsetTable) +
                                  static_cast<std::uintmax_t>(entryCount) * (4u + 32u + 4u);
    return minimum <= size;
}

void seekTo(std::ifstream& in, UInt32 offset, const std::filesystem::path& filename) {
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) {
        throw SSFError("Unable to seek within SSF file \"" + filename.string() + "\".", 1);
    }
}

void validateNwnOffset(UInt32 offset, std::size_t soundFileLen, std::uintmax_t size, const std::filesystem::path& filename) {
    const std::uintmax_t needed = static_cast<std::uintmax_t>(offset) + static_cast<std::uintmax_t>(soundFileLen) + sizeof(UInt32);
    if (needed > size) {
        throw SSFError("SSF entry offset points outside selected file \"" + filename.string() + "\".", 1);
    }
}

std::string formatVersionText(SSFFormat format) {
    switch (format) {
    case SSFFormat::KotOR_V11: return "SSF V1.1 (KotOR/KotOR2 StrRef table)";
    case SSFFormat::NWN_V10: return "SSF V1.0 (NWN ResRef + StrRef table)";
    case SSFFormat::NWN2_V11: return "SSF V1.1 (NWN2 32-byte ResRef + StrRef table)";
    }
    return "SSF";
}

} // namespace

SSFError::SSFError(const std::string& message, int helpContext)
    : std::runtime_error(message), helpContext_(helpContext) {}

int SSFError::helpContext() const noexcept {
    return helpContext_;
}

std::string ssfFormatName(SSFFormat format) {
    return formatVersionText(format);
}

std::string ssfFormatToken(SSFFormat format) {
    switch (format) {
    case SSFFormat::KotOR_V11: return "KotOR_V11";
    case SSFFormat::NWN_V10: return "NWN_V10";
    case SSFFormat::NWN2_V11: return "NWN2_V11";
    }
    throw std::invalid_argument("Unknown SSF format value.");
}

SSFFormat parseSsfFormat(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char raw : value) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isalnum(ch) != 0) normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (normalized == "kotorv11" || normalized == "kotor" || normalized == "kotor1" ||
        normalized == "kotor2" || normalized == "ssfv11kotorkotor2strreftable") {
        return SSFFormat::KotOR_V11;
    }
    if (normalized == "nwnv10" || normalized == "nwn" || normalized == "nwn1" ||
        normalized == "v10" || normalized == "ssfv10nwnresrefstrreftable") {
        return SSFFormat::NWN_V10;
    }
    if (normalized == "nwn2v11" || normalized == "nwn2" || normalized == "v11nwn2" ||
        normalized == "ssfv11nwn232byteresrefstrreftable") {
        return SSFFormat::NWN2_V11;
    }
    throw std::invalid_argument("Unknown SSF format: " + std::string(value));
}

std::size_t ssfSoundResRefLimit(SSFFormat format) {
    switch (format) {
    case SSFFormat::NWN_V10: return 16u;
    case SSFFormat::NWN2_V11: return 32u;
    case SSFFormat::KotOR_V11: return 0u;
    }
    return 0u;
}

SSFFile::SSFFile() {
    reset();
}

SSFFile::SSFFile(const std::filesystem::path& filename) : SSFFile() {
    load(filename);
}

void SSFFile::reset() {
    entries_.fill(kUnsetStrRef);
    extraEntries_.clear();
    soundFiles_.assign(kSoundsetEntryCount, std::string{});
    ssfFile_.clear();
    format_ = SSFFormat::KotOR_V11;
    loaded_ = false;
}

void SSFFile::newFile(const std::filesystem::path& filename) {
    reset();
    ssfFile_ = filename;
    loaded_ = true;
}

void SSFFile::ensureSoundFileCount(std::size_t count) {
    if (soundFiles_.size() < count) {
        soundFiles_.resize(count);
    }
}

UInt32 SSFFile::entryValue(std::size_t zeroBasedIndex) const {
    if (zeroBasedIndex < entries_.size()) return entries_[zeroBasedIndex];
    const std::size_t extra = zeroBasedIndex - entries_.size();
    if (extra < extraEntries_.size()) return extraEntries_[extra];
    return kUnsetStrRef;
}

void SSFFile::setEntryValue(std::size_t zeroBasedIndex, UInt32 value) {
    if (zeroBasedIndex < entries_.size()) {
        entries_[zeroBasedIndex] = value;
        return;
    }
    const std::size_t extra = zeroBasedIndex - entries_.size();
    while (extra >= extraEntries_.size()) {
        extraEntries_.push_back(kUnsetStrRef);
    }
    extraEntries_[extra] = value;
}

void SSFFile::loadKotORV11(std::ifstream& in, const std::filesystem::path& filename, std::uintmax_t size, UInt32 offset) {
    if (offset > size || ((size - offset) % sizeof(UInt32)) != 0u) {
        throw SSFError("Selected file \"" + filename.string() + "\" is not a valid KotOR/KotOR2 SSF V1.1 file.", 1);
    }
    const auto entryCount64 = (size - offset) / sizeof(UInt32);
    if (entryCount64 > kMaxReasonableSsfEntries) {
        throw SSFError("SSF file has an unreasonable number of entries: " + std::to_string(entryCount64), 1);
    }
    const auto entryCount = static_cast<std::size_t>(entryCount64);
    entries_.fill(kUnsetStrRef);
    extraEntries_.clear();
    soundFiles_.assign(entryCount, std::string{});

    seekTo(in, offset, filename);
    for (std::size_t i = 0; i < entryCount; ++i) {
        setEntryValue(i, binary::readU32LE(in, "SSF StrRef entry"));
    }
    if (soundFiles_.size() < diskEntryCount()) soundFiles_.resize(diskEntryCount());
    format_ = SSFFormat::KotOR_V11;
}

void SSFFile::loadNwnResRefFormat(std::ifstream& in, const std::filesystem::path& filename, std::uintmax_t size,
                                  UInt32 entryCount32, UInt32 offsetTable, std::size_t soundFileLen, SSFFormat format) {
    if (entryCount32 == 0 || entryCount32 > kMaxReasonableSsfEntries) {
        throw SSFError("SSF file has an invalid entry count: " + std::to_string(entryCount32), 1);
    }
    const std::uintmax_t tableEnd = static_cast<std::uintmax_t>(offsetTable) + static_cast<std::uintmax_t>(entryCount32) * sizeof(UInt32);
    if (offsetTable > size || tableEnd > size) {
        throw SSFError("SSF offset table points outside selected file \"" + filename.string() + "\".", 1);
    }

    const auto entryCount = static_cast<std::size_t>(entryCount32);
    std::vector<UInt32> offsets(entryCount);
    seekTo(in, offsetTable, filename);
    for (std::size_t i = 0; i < entryCount; ++i) {
        offsets[i] = binary::readU32LE(in, "SSF entry offset");
        validateNwnOffset(offsets[i], soundFileLen, size, filename);
    }

    entries_.fill(kUnsetStrRef);
    extraEntries_.clear();
    soundFiles_.assign(entryCount, std::string{});

    for (std::size_t i = 0; i < entryCount; ++i) {
        seekTo(in, offsets[i], filename);
        soundFiles_[i] = readFixedAsciiString(in, soundFileLen, "SSF sound ResRef");
        setEntryValue(i, binary::readU32LE(in, "SSF StrRef entry"));
    }
    if (soundFiles_.size() < diskEntryCount()) soundFiles_.resize(diskEntryCount());
    format_ = format;
}

void SSFFile::load(const std::filesystem::path& filename) {
    std::error_code existsError;
    if (!std::filesystem::is_regular_file(filename, existsError) || existsError) {
        throw SSFError("Selected file \"" + filename.string() + "\" does not exist! Unable to load it.", 5);
    }
    const std::uintmax_t size = fileSizeChecked(filename);
    if (size < kKotORSsfHeaderSize) {
        throw SSFError("Selected file \"" + filename.string() + "\" is too small to be an SSF file.", 1);
    }

    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open selected file \"" + filename.string() + "\".");
    }

    const FourCC fileType = binary::readFourCC(in, "SSF file type");
    const FourCC fileVersion = binary::readFourCC(in, "SSF file version");
    if (fileType != makeFourCC("SSF ")) {
        throw SSFError("Selected file \"" + filename.string() + "\" is not an SSF file.", 1);
    }

    if (fileVersion == makeFourCC("V1.0")) {
        if (size < 16u) throw SSFError("Selected SSF V1.0 file is truncated.", 1);
        const UInt32 entryCount = binary::readU32LE(in, "SSF V1.0 entry count");
        const UInt32 offsetTable = binary::readU32LE(in, "SSF V1.0 offset table");
        loadNwnResRefFormat(in, filename, size, entryCount, offsetTable, 16u, SSFFormat::NWN_V10);
    } else if (fileVersion == makeFourCC("V1.1")) {
        const UInt32 firstField = binary::readU32LE(in, "SSF V1.1 first header field");
        UInt32 secondField = 0;
        if (size >= 16u) {
            secondField = binary::readU32LE(in, "SSF V1.1 second header field or first StrRef");
        }
        if (size >= 16u && isNwn2V11Layout(size, firstField, secondField)) {
            loadNwnResRefFormat(in, filename, size, firstField, secondField, 32u, SSFFormat::NWN2_V11);
        } else {
            loadKotORV11(in, filename, size, firstField);
        }
    } else {
        throw SSFError("Unsupported SSF file version \"" + fourCCToString(fileVersion) + "\".", 1);
    }

    ssfFile_ = filename;
    loaded_ = true;
}

void SSFFile::writeKotORV11(std::ostream& out) const {
    binary::writeFourCC(out, makeFourCC("SSF "), "SSF file type");
    binary::writeFourCC(out, makeFourCC("V1.1"), "SSF file version");
    binary::writeU32LE(out, kKotORSsfHeaderSize, "SSF start offset");
    for (std::size_t i = 0; i < diskEntryCount(); ++i) {
        binary::writeU32LE(out, entryValue(i), "SSF StrRef entry");
    }
}

void SSFFile::writeNwnResRefFormat(std::ostream& out, SSFFormat format) const {
    const std::size_t count = diskEntryCount();
    if (count > std::numeric_limits<UInt32>::max()) {
        throw SSFError("Too many SSF entries to save.", 1);
    }
    const std::size_t soundFileLen = ssfSoundResRefLimit(format);
    if (soundFileLen == 0) {
        throw SSFError("Internal error: requested ResRef SSF output for a format without sound ResRefs.", 1);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::string sound = soundFile(i);
        if (sound.size() > soundFileLen) {
            throw SSFError("Sound ResRef \"" + sound + "\" exceeds the " + std::to_string(soundFileLen) + "-character limit for " + ssfFormatName(format) + ".", 1);
        }
    }

    binary::writeFourCC(out, makeFourCC("SSF "), "SSF file type");
    binary::writeFourCC(out, format == SSFFormat::NWN_V10 ? makeFourCC("V1.0") : makeFourCC("V1.1"), "SSF file version");
    binary::writeU32LE(out, static_cast<UInt32>(count), "SSF entry count");
    binary::writeU32LE(out, kNwnSsfOffsetTable, "SSF offset table");
    for (std::size_t pos = 16u; pos < kNwnSsfOffsetTable; ++pos) {
        const char zero = '\0';
        binary::writeExact(out, &zero, 1, "SSF reserved header byte");
    }

    std::uintmax_t offset = kNwnSsfOffsetTable + static_cast<std::uintmax_t>(count) * sizeof(UInt32);
    const std::uintmax_t recordSize = static_cast<std::uintmax_t>(soundFileLen) + sizeof(UInt32);
    for (std::size_t i = 0; i < count; ++i) {
        if (offset > std::numeric_limits<UInt32>::max()) throw SSFError("SSF output is too large.", 1);
        binary::writeU32LE(out, static_cast<UInt32>(offset), "SSF entry offset");
        offset += recordSize;
    }
    for (std::size_t i = 0; i < count; ++i) {
        writeFixedAsciiString(out, soundFile(i), soundFileLen, "SSF sound ResRef");
        binary::writeU32LE(out, entryValue(i), "SSF StrRef entry");
    }
}

void SSFFile::save(const std::filesystem::path& filename) {
    if (!loaded_) {
        throw SSFError("No file has been loaded! Unable to save.", 6);
    }

    const std::filesystem::path requestedTarget = filename.empty() ? ssfFile_ : filename;
    const SaveTargetSnapshot target = prepareSaveTarget(requestedTarget);
    const std::filesystem::path temporary = makeTemporarySiblingPath(target);
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Unable to open temporary SSF file \"" + temporary.string() + "\" for writing.");
        }

        if (format_ == SSFFormat::KotOR_V11) writeKotORV11(out);
        else writeNwnResRefFormat(out, format_);

        out.flush();
        if (!out) {
            throw std::runtime_error("Unable to write SSF file \"" + requestedTarget.string() + "\".");
        }
        out.close();
        if (!out) {
            throw std::runtime_error("Unable to close SSF file \"" + requestedTarget.string() + "\" after writing.");
        }

        commitTemporarySaveFileToResolvedTarget(temporary, target);
    } catch (...) {
        removeFileNoThrow(temporary);
        throw;
    }

    ssfFile_ = requestedTarget;
}

void SSFFile::setValue(const std::string& label, UInt32 strRef) {
    for (std::size_t i = 0; i < diskEntryCount(); ++i) {
        if (iequals(label, soundsetDisplayLabel(i, diskEntryCount()))) {
            setEntryValue(i, strRef);
            return;
        }
    }

    throw SSFError("Unable to change value in SSF file, label \"" + label + "\" is not a valid entry label!", 2);
}

UInt32 SSFFile::getValue(const std::string& label) const {
    for (std::size_t i = 0; i < diskEntryCount(); ++i) {
        if (iequals(label, soundsetDisplayLabel(i, diskEntryCount()))) {
            return entryValue(i);
        }
    }

    throw SSFError("Unable to read value in SSF file, label \"" + label + "\" is not a valid entry label!", 3);
}

std::string SSFFile::label(std::size_t oneBasedIndex) const {
    if (oneBasedIndex >= 1 && oneBasedIndex <= diskEntryCount()) {
        return soundsetDisplayLabel(oneBasedIndex - 1, diskEntryCount());
    }
    throw SSFError("Label index out of bounds.", 4);
}

const std::array<UInt32, kSoundsetEntryCount>& SSFFile::entries() const noexcept {
    return entries_;
}

std::array<UInt32, kSoundsetEntryCount>& SSFFile::entries() noexcept {
    return entries_;
}

const std::vector<UInt32>& SSFFile::extraEntries() const noexcept {
    return extraEntries_;
}

std::vector<UInt32>& SSFFile::extraEntries() noexcept {
    return extraEntries_;
}

const std::vector<std::string>& SSFFile::soundFiles() const noexcept {
    return soundFiles_;
}

std::vector<std::string>& SSFFile::soundFiles() noexcept {
    return soundFiles_;
}

std::string SSFFile::soundFile(std::size_t zeroBasedIndex) const {
    return zeroBasedIndex < soundFiles_.size() ? soundFiles_[zeroBasedIndex] : std::string{};
}

void SSFFile::setSoundFile(std::size_t zeroBasedIndex, std::string soundFile) {
    ensureSoundFileCount(zeroBasedIndex + 1u);
    soundFiles_[zeroBasedIndex] = std::move(soundFile);
}

std::size_t SSFFile::diskEntryCount() const noexcept {
    return entries_.size() + extraEntries_.size();
}

bool SSFFile::loaded() const noexcept {
    return loaded_;
}

const std::filesystem::path& SSFFile::filename() const noexcept {
    return ssfFile_;
}

SSFFormat SSFFile::format() const noexcept {
    return format_;
}

void SSFFile::setFormat(SSFFormat format) noexcept {
    format_ = format;
}

} // namespace neossf
