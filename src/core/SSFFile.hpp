#pragma once

#include "core/CoreTypes.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neossf {

class SSFError : public std::runtime_error {
public:
    explicit SSFError(const std::string& message, int helpContext = 0);
    int helpContext() const noexcept;

private:
    int helpContext_ = 0;
};

enum class SSFFormat {
    KotOR_V11,
    NWN_V10,
    NWN2_V11,
};

std::string ssfFormatName(SSFFormat format);
std::string ssfFormatToken(SSFFormat format);
SSFFormat parseSsfFormat(std::string_view value);
std::size_t ssfSoundResRefLimit(SSFFormat format);

class SSFFile {
public:
    SSFFile();
    explicit SSFFile(const std::filesystem::path& filename);

    void load(const std::filesystem::path& filename);
    void save(const std::filesystem::path& filename = {});
    void newFile(const std::filesystem::path& filename);
    void reset();

    void setValue(const std::string& label, UInt32 strRef);
    UInt32 getValue(const std::string& label) const;
    std::string label(std::size_t oneBasedIndex) const;

    const std::array<UInt32, kSoundsetEntryCount>& entries() const noexcept;
    std::array<UInt32, kSoundsetEntryCount>& entries() noexcept;
    const std::vector<UInt32>& extraEntries() const noexcept;
    std::vector<UInt32>& extraEntries() noexcept;

    const std::vector<std::string>& soundFiles() const noexcept;
    std::vector<std::string>& soundFiles() noexcept;
    std::string soundFile(std::size_t zeroBasedIndex) const;
    void setSoundFile(std::size_t zeroBasedIndex, std::string soundFile);

    std::size_t diskEntryCount() const noexcept;
    bool loaded() const noexcept;
    const std::filesystem::path& filename() const noexcept;

    SSFFormat format() const noexcept;
    void setFormat(SSFFormat format) noexcept;

private:
    UInt32 entryValue(std::size_t zeroBasedIndex) const;
    void setEntryValue(std::size_t zeroBasedIndex, UInt32 value);
    void ensureSoundFileCount(std::size_t count);
    void loadKotORV11(std::ifstream& in, const std::filesystem::path& filename, std::uintmax_t size, UInt32 offset);
    void loadNwnResRefFormat(std::ifstream& in, const std::filesystem::path& filename, std::uintmax_t size,
                             UInt32 entryCount, UInt32 offsetTable, std::size_t soundFileLen, SSFFormat format);
    void writeKotORV11(std::ostream& out) const;
    void writeNwnResRefFormat(std::ostream& out, SSFFormat format) const;

    std::array<UInt32, kSoundsetEntryCount> entries_{};
    std::vector<UInt32> extraEntries_;
    std::vector<std::string> soundFiles_;
    std::filesystem::path ssfFile_;
    SSFFormat format_ = SSFFormat::KotOR_V11;
    bool loaded_ = false;
};

} // namespace neossf
