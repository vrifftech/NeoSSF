#include "core/SsfPatcher.hpp"

#include "core/Common.hpp"
#include "core/CoreTypes.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace neossf {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool iequals(const std::string& left, const std::string& right) {
    return lowerAscii(left) == lowerAscii(right);
}

void validatePortableFilename(const std::string& filename,
                              const char* label,
                              const std::string& requiredExtension) {
    const std::filesystem::path path(filename);
    const bool invalidCharacter = std::any_of(filename.begin(), filename.end(), [](unsigned char ch) {
        return ch < 0x20u || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
               ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*' ||
               ch == '[' || ch == ']' || ch == '=' || ch == ';';
    });
    if (filename.empty() || path.filename() != path || path == "." || path == ".." ||
        invalidCharacter || filename.back() == '.' || filename.back() == ' ') {
        throw std::runtime_error(std::string(label) +
                                 " must be a portable plain filename without directory or INI syntax characters.");
    }
    if (lowerAscii(path.extension().string()) != requiredExtension) {
        throw std::runtime_error(std::string(label) + " must use the " + requiredExtension + " extension.");
    }
}

void validateOptions(const SsfTlkPatcherOptions& options) {
    validatePortableFilename(options.patchFilename, "SSF patch target filename", ".ssf");
    validatePortableFilename(options.appendFilename, "Append TLK filename", ".tlk");
    if (!iequals(options.appendFilename, "append.tlk")) {
        throw std::runtime_error(
            "Stock TSLPatcher requires the appended TLK payload to be named exactly append.tlk.");
    }
    if (iequals(options.patchFilename, options.appendFilename) ||
        iequals(options.patchFilename, "changes.ini")) {
        throw std::runtime_error("Generated package filenames must not collide.");
    }
}

TalkString cloneTlkEntry(const TalkString& source) {
    TalkString clone = source;
    clone.custom = true;
    return clone;
}

bool talkStringsEquivalentForPatcher(const TalkString& left, const TalkString& right) {
    return left.flags == right.flags &&
           left.soundResref == right.soundResref &&
           left.volumeVariance == right.volumeVariance &&
           left.pitchVariance == right.pitchVariance &&
           left.soundLength == right.soundLength &&
           left.text == right.text;
}

std::string patcherSlotLabel(std::size_t zeroBasedSlot) {
    if (zeroBasedSlot >= kSoundsetEntryCount) {
        throw std::out_of_range("TSLPatcher SSF slot index is out of bounds.");
    }
    // These display labels intentionally match the exact keys accepted by the
    // original TSLPatcher Bioware::SSF module, including Unknown (29)-(40).
    return soundsetDisplayLabel(zeroBasedSlot, kSoundsetEntryCount);
}

void rememberProtectedPath(SsfTlkPatcherResult& result, const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return;
    if (std::find(result.protectedInputFiles.begin(), result.protectedInputFiles.end(), absolute) ==
        result.protectedInputFiles.end()) {
        result.protectedInputFiles.push_back(absolute);
    }
}

bool pathsReferToSameFile(const std::filesystem::path& left,
                          const std::filesystem::path& right) {
    if (left.empty() || right.empty()) return false;
    std::error_code ec;
    if (std::filesystem::equivalent(left, right, ec) && !ec) return true;

    ec.clear();
    const auto normalizedLeft = std::filesystem::absolute(left, ec).lexically_normal();
    if (ec) return false;
    ec.clear();
    const auto normalizedRight = std::filesystem::absolute(right, ec).lexically_normal();
    if (ec) return false;
#if defined(_WIN32)
    return lowerAscii(normalizedLeft.generic_string()) == lowerAscii(normalizedRight.generic_string());
#else
    return normalizedLeft == normalizedRight;
#endif
}

void rejectInputOverwrite(const SsfTlkPatcherResult& result,
                          const std::vector<std::filesystem::path>& generatedFiles) {
    for (const auto& output : generatedFiles) {
        for (const auto& input : result.protectedInputFiles) {
            if (pathsReferToSameFile(output, input)) {
                throw std::runtime_error(
                    "Refusing to overwrite a comparison input while generating the SSF/TLK patch package: " +
                    output.string());
            }
        }
    }
}

bool validateSsfCompatibility(SsfTlkPatcherResult& result,
                              const AppModel& original,
                              const AppModel& modified) {
    bool compatible = true;
    if (original.ssfFormat() != SSFFormat::KotOR_V11 ||
        modified.ssfFormat() != SSFFormat::KotOR_V11) {
        result.project.unsupported.push_back(
            "TSLPatcher/HoloPatcher SSFList supports KotOR SSF V1.1 files only; "
            "NWN/NWN2 sound-ResRef SSFs require whole-file distribution.");
        compatible = false;
    }
    if (original.entryCount() != kSoundsetEntryCount ||
        modified.entryCount() != kSoundsetEntryCount) {
        result.project.unsupported.push_back(
            "TSLPatcher/HoloPatcher SSFList requires the fixed 40-slot KotOR SSF layout.");
        compatible = false;
    }

    const std::size_t common = std::min(original.entryCount(), modified.entryCount());
    for (std::size_t slot = 0; slot < common; ++slot) {
        if (original.soundFile(slot) != modified.soundFile(slot)) {
            result.project.unsupported.push_back(
                "SSFList cannot modify a direct Sound ResRef: " + modified.entryLabel(slot));
        }
    }
    return compatible;
}

void validateOriginalTlkRows(SsfTlkPatcherResult& result,
                             const TalkTable& original,
                             const TalkTable& modified) {
    if (original.fileId() != makeFourCC("TLK ") || original.version() != makeFourCC("V3.0") ||
        modified.fileId() != makeFourCC("TLK ") || modified.version() != makeFourCC("V3.0")) {
        result.project.unsupported.push_back(
            "NeoSSF patch preparation requires classic KotOR TLK V3.0 input tables.");
        return;
    }
    if (original.language() != modified.language()) {
        result.project.unsupported.push_back(
            "Changing the TLK language ID is not representable by the append.tlk workflow.");
    }
    if (modified.count() < original.count()) {
        result.project.unsupported.push_back(
            "TLK deletion or truncation is not supported by NeoSSF's append.tlk workflow.");
    }

    const UInt32 commonCount = std::min(original.count(), modified.count());
    for (UInt32 strRef = 0; strRef < commonCount; ++strRef) {
        if (!talkStringsEquivalentForPatcher(original.entryAtStrRef(strRef),
                                             modified.entryAtStrRef(strRef))) {
            result.project.unsupported.push_back(
                "Existing TLK entry was modified at StrRef " + std::to_string(strRef) +
                "; use NeoTLK's HoloPatcher package export for existing-entry replacements.");
        }
    }
}

void addBaselineSsfAsset(SsfTlkPatcherResult& result,
                         const std::filesystem::path& baselineSsfAsset) {
    if (!result.hasSsfChanges() || !result.options.copyBaselineAsset) return;
    if (baselineSsfAsset.empty()) {
        result.project.unsupported.push_back(
            "A clean baseline SSF is required for a complete SSF patch package.");
        return;
    }
    result.project.assets.push_back({baselineSsfAsset, result.options.patchFilename});
    rememberProtectedPath(result, baselineSsfAsset);
}

} // namespace

SsfTlkPatcherResult diffSsfAndTlkForPatcher(
    const AppModel& originalSsf,
    const AppModel& modifiedSsf,
    const TalkTable* modifiedTlk,
    UInt32 tlkBaselineCount,
    const SsfTlkPatcherOptions& requestedOptions,
    const std::filesystem::path& baselineSsfAsset) {
    SsfTlkPatcherResult result;
    result.options = requestedOptions;
    if (result.options.patchFilename.empty()) {
        result.options.patchFilename = neotsl::basenameForPatch(originalSsf.ssfFile());
    }
    validateOptions(result.options);

    rememberProtectedPath(result, originalSsf.ssfFile());
    rememberProtectedPath(result, modifiedSsf.ssfFile());
    rememberProtectedPath(result, result.options.originalTlkPath);
    rememberProtectedPath(result, result.options.modifiedTlkPath);

    const bool ssfCompatible = validateSsfCompatibility(result, originalSsf, modifiedSsf);

    std::unordered_map<UInt32, std::size_t> appendedTokenByStrRef;
    if (modifiedTlk != nullptr) {
        if (!modifiedTlk->fileExists() ||
            modifiedTlk->fileId() != makeFourCC("TLK ") ||
            modifiedTlk->version() != makeFourCC("V3.0")) {
            result.project.unsupported.push_back(
                "NeoSSF patch preparation requires a loaded classic KotOR TLK V3.0 table.");
        } else if (tlkBaselineCount > modifiedTlk->count()) {
            result.project.unsupported.push_back(
                "The modified TLK contains fewer rows than the recorded clean baseline.");
        } else {
            result.appendTable.newFile();
            result.appendTable.setLanguage(modifiedTlk->language());
            for (UInt32 strRef = tlkBaselineCount; strRef < modifiedTlk->count(); ++strRef) {
                const std::size_t appendIndex = result.appendedTlkEntries;
                result.appendTable.addEntry(cloneTlkEntry(modifiedTlk->entryAtStrRef(strRef)));
                result.project.add("TLKList",
                                   "StrRef" + std::to_string(appendIndex),
                                   std::to_string(appendIndex));
                appendedTokenByStrRef.emplace(strRef, appendIndex);
                ++result.appendedTlkEntries;
            }
        }
    } else if (tlkBaselineCount != 0) {
        result.project.unsupported.push_back(
            "A TLK baseline count was supplied without a modified TLK table.");
    }

    std::set<std::size_t> referencedAppendRows;
    if (ssfCompatible) {
        for (std::size_t slot = 0; slot < kSoundsetEntryCount; ++slot) {
            const UInt32 before = originalSsf.entryValue(slot);
            const UInt32 after = modifiedSsf.entryValue(slot);
            const auto dynamic = appendedTokenByStrRef.find(after);

            // A reference to a newly appended row must always be emitted as a
            // token. The clean SSF can coincidentally contain the same numeric
            // value, but the user's final installed StrRef is assigned at run time.
            if (before == after && dynamic == appendedTokenByStrRef.end()) continue;

            if (!result.hasSsfChanges()) {
                result.project.add("SSFList", "File0", result.options.patchFilename);
                result.project.section(result.options.patchFilename);
            }

            std::string value;
            if (dynamic != appendedTokenByStrRef.end()) {
                value = "StrRef" + std::to_string(dynamic->second);
                referencedAppendRows.insert(dynamic->second);
                ++result.dynamicSsfAssignments;
            } else {
                value = after == kUnsetStrRef ? std::string("-1") : std::to_string(after);
                ++result.fixedSsfAssignments;
            }
            result.project.add(result.options.patchFilename,
                               patcherSlotLabel(slot),
                               std::move(value));
            ++result.changedSsfSlots;
        }
    }

    addBaselineSsfAsset(result, baselineSsfAsset);

    if (result.appendedTlkEntries > referencedAppendRows.size()) {
        const std::size_t unreferenced = result.appendedTlkEntries - referencedAppendRows.size();
        result.project.warnings.push_back(
            std::to_string(unreferenced) + " appended TLK entr" +
            (unreferenced == 1 ? std::string("y is") : std::string("ies are")) +
            " not referenced by a changed SSF slot; the entr" +
            (unreferenced == 1 ? std::string("y will") : std::string("ies will")) +
            " still be included in append.tlk.");
    }
    if (!result.hasPatchableChanges() && result.project.unsupported.empty()) {
        result.project.warnings.push_back(
            "No SSF slot changes or newly appended TLK entries were detected.");
    }

    return result;
}

SsfTlkPatcherResult diffSsfAndTlkForPatcher(
    const AppModel& originalSsf,
    const AppModel& modifiedSsf,
    const TalkTable& originalTlk,
    const TalkTable& modifiedTlk,
    const SsfTlkPatcherOptions& options,
    const std::filesystem::path& baselineSsfAsset) {
    SsfTlkPatcherResult result = diffSsfAndTlkForPatcher(
        originalSsf,
        modifiedSsf,
        &modifiedTlk,
        originalTlk.count(),
        options,
        baselineSsfAsset);
    validateOriginalTlkRows(result, originalTlk, modifiedTlk);
    return result;
}

void writeSsfTlkPatcherPackage(SsfTlkPatcherResult& result,
                               const std::filesystem::path& outputDirectory,
                               bool allowUnsupported) {
    validateOptions(result.options);
    if (!allowUnsupported) neotsl::throwIfUnsupported(result.project);
    else neotsl::printReport(result.project);

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        throw std::runtime_error("Unable to create SSF/TLK patcher package folder: " +
                                 outputDirectory.string() + ": " + ec.message());
    }

    std::vector<std::filesystem::path> generatedFiles{outputDirectory / "changes.ini"};
    if (result.hasSsfChanges()) {
        generatedFiles.push_back(outputDirectory / result.options.patchFilename);
    }
    if (result.hasAppendTable()) {
        generatedFiles.push_back(outputDirectory / result.options.appendFilename);
    }
    rejectInputOverwrite(result, generatedFiles);

    if (result.hasAppendTable()) {
        result.appendTable.save((outputDirectory / result.options.appendFilename).string());
    }
    neotsl::writePackage(result.project, outputDirectory, true);
}

} // namespace neossf
