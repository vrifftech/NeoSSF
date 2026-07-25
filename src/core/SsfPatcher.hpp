#pragma once

#include "core/AppModel.hpp"
#include "TslPatcher.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace neossf {

// Stock TSLPatcher and HoloPatcher use the same SSFList + append.tlk
// mechanism for the operations NeoSSF can prepare. Existing TLK-row
// replacement remains a NeoTLK/HoloPatcher workflow.
struct SsfTlkPatcherOptions {
    std::string patchFilename;
    std::string appendFilename = "append.tlk";
    bool copyBaselineAsset = true;

    // Optional source paths used only to prevent accidental input overwrite.
    std::filesystem::path originalTlkPath;
    std::filesystem::path modifiedTlkPath;
};

struct SsfTlkPatcherResult {
    SsfTlkPatcherOptions options;
    neotsl::PatchProject project;
    TalkTable appendTable;
    std::size_t changedSsfSlots = 0;
    std::size_t dynamicSsfAssignments = 0;
    std::size_t fixedSsfAssignments = 0;
    std::size_t appendedTlkEntries = 0;
    std::vector<std::filesystem::path> protectedInputFiles;

    bool hasSsfChanges() const noexcept { return changedSsfSlots != 0; }
    bool hasAppendTable() const noexcept { return appendedTlkEntries != 0; }
    bool hasPatchableChanges() const noexcept {
        return hasSsfChanges() || hasAppendTable();
    }
};

// GUI/session form. tlkBaselineCount is the row count recorded when the TLK
// was loaded into the active NeoSSF tab. Rows at or after that count are
// written to append.tlk, and SSF references to those rows become StrRefN
// tokens so their final installed values are assigned dynamically.
SsfTlkPatcherResult diffSsfAndTlkForPatcher(
    const AppModel& originalSsf,
    const AppModel& modifiedSsf,
    const TalkTable* modifiedTlk,
    UInt32 tlkBaselineCount,
    const SsfTlkPatcherOptions& options = {},
    const std::filesystem::path& baselineSsfAsset = {});

// File-comparison form used by the CLI. In addition to preparing append.tlk,
// this verifies that rows already present in the clean/original TLK were not
// edited, because NeoSSF's combined workflow is deliberately append-only.
SsfTlkPatcherResult diffSsfAndTlkForPatcher(
    const AppModel& originalSsf,
    const AppModel& modifiedSsf,
    const TalkTable& originalTlk,
    const TalkTable& modifiedTlk,
    const SsfTlkPatcherOptions& options = {},
    const std::filesystem::path& baselineSsfAsset = {});

void writeSsfTlkPatcherPackage(
    SsfTlkPatcherResult& result,
    const std::filesystem::path& outputDirectory,
    bool allowUnsupported = false);

} // namespace neossf
