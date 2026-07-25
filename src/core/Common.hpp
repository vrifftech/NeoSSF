#pragma once

#include "core/CoreTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace neossf {

bool iequals(const std::string& lhs, const std::string& rhs);

struct SaveTargetSnapshot {
    std::filesystem::path resolvedTarget;
    bool existed = false;
    std::uintmax_t size = 0;
    bool hasLastWriteTime = false;
    std::filesystem::file_time_type lastWriteTime{};
    std::string identity;
    bool hasContentHash = false;
    std::uint64_t contentHash = 0;
};

SaveTargetSnapshot prepareSaveTarget(const std::filesystem::path& target);
std::filesystem::path makeTemporarySiblingPath(const SaveTargetSnapshot& target);
void commitTemporarySaveFileToResolvedTarget(const std::filesystem::path& temporary, const SaveTargetSnapshot& target);
void removeFileNoThrow(const std::filesystem::path& path) noexcept;
bool isStartupAutoloadSsfPath(const std::filesystem::path& path);

} // namespace neossf
