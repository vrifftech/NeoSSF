#pragma once

#include "core/CoreTypes.hpp"
#include "core/TLKFile.hpp"
#include "core/SSFFile.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "TabularData.hpp"

namespace neossf {

struct ResEntry {
    std::string text;
    std::string sound;
};

// Preserve the native SSF family when a CSV/TSV filter yields no data rows.
// Import ignores the metadata-only row because its Index and StrRef are blank.
void ensureSsfTableMetadataRow(neotabular::Table& table, SSFFormat format);

class AppModel {
public:
    AppModel();

    const std::array<UInt32, kSoundsetEntryCount>& entries() const noexcept;
    std::array<UInt32, kSoundsetEntryCount>& entries() noexcept;
    std::size_t entryCount() const noexcept;
    std::string entryLabel(std::size_t zeroBasedRow) const;
    UInt32 entryValue(std::size_t zeroBasedRow) const;
    void setEntryValue(std::size_t zeroBasedRow, UInt32 value);

    TalkTable& tlkData() noexcept;
    const TalkTable& tlkData() const noexcept;

    ResEntry getTlkString(UInt32 strRef) const;
    void loadTlk(const std::filesystem::path& tlkFile);
    void loadSsf(const std::filesystem::path& ssfFile, bool precheckExists = true);
    void newSsf();
    void modifySlot(std::size_t oneBasedRow, const std::string& strRefText);
    void modifySoundFile(std::size_t oneBasedRow, const std::string& soundFile);
    UInt32 addTlkEntryAndAssign(std::size_t oneBasedRow, const std::string& text, const std::string& resref);
    void saveTlk(const std::filesystem::path& tlkFile);
    void saveSsf(const std::filesystem::path& ssfFile, std::function<void()> afterSave = {});
    SSFFormat ssfFormat() const noexcept;
    void setSsfFormat(SSFFormat format) noexcept;
    std::string soundFile(std::size_t zeroBasedRow) const;

    void resetEntries();

    neotabular::Table toTable(bool includeTlkPreview = true) const;
    void importSsfTable(const neotabular::Table& table);
    std::string toXml(bool includeTlkPreview = false) const;
    void importXml(const std::string& xmlText);

    const std::filesystem::path& tlkFile() const noexcept;
    const std::filesystem::path& ssfFile() const noexcept;
    void setTlkFile(const std::filesystem::path& path);
    void setSsfFile(const std::filesystem::path& path);

    bool tlkLoaded() const noexcept;
    void setTlkLoaded(bool loaded) noexcept;
    bool tlkModified() const noexcept;
    void setTlkModified(bool modified) noexcept;
    UInt32 tlkBaselineCount() const noexcept;

private:
    static UInt32 parseSlotValue(const std::string& strRefText);

    TalkTable tlkData_;
    std::array<UInt32, kSoundsetEntryCount> entries_{};
    std::vector<UInt32> extraSsfEntries_;
    std::vector<std::string> soundFiles_;
    SSFFormat ssfFormat_ = SSFFormat::KotOR_V11;
    std::filesystem::path tlkFile_;
    std::filesystem::path ssfFile_;
    bool tlkLoaded_ = false;
    bool tlkModified_ = false;
    UInt32 tlkBaselineCount_ = 0;
};

} // namespace neossf
