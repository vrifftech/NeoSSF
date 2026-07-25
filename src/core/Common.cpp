#include "core/Common.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif


namespace neossf {

namespace {


constexpr const char* kManagedTempMarker = ".neossf-save-";
constexpr const char* kManagedTempSentinel = ".neossf-managed-temp";

bool isManagedTemporaryDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        return false;
    }
    const std::string name = directory.filename().string();
    if (name.find(kManagedTempMarker) == std::string::npos) {
        return false;
    }

    std::error_code ec;
    const auto directoryStatus = std::filesystem::symlink_status(directory, ec);
    if (ec || !std::filesystem::is_directory(directoryStatus)) {
        return false;
    }

    ec.clear();
    const auto sentinelStatus = std::filesystem::symlink_status(directory / kManagedTempSentinel, ec);
    return !ec && std::filesystem::is_regular_file(sentinelStatus);
}

void cleanupManagedTemporaryDirectory(const std::filesystem::path& temporary) noexcept {
    // Only clean containers created by makeTemporarySiblingPath(). The sentinel
    // file prevents removeFileNoThrow() from deleting an unrelated empty directory
    // that happens to use the same filename convention.
    if (temporary.filename() != "payload.tmp") {
        return;
    }
    const std::filesystem::path directory = temporary.parent_path();
    if (!isManagedTemporaryDirectory(directory)) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(directory / kManagedTempSentinel, ec);
    ec.clear();
    std::filesystem::remove(directory, ec);
}

void restorePermissionsNoThrow(const std::filesystem::path& path, std::filesystem::perms permissions) noexcept {
    std::error_code ec;
    std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace, ec);
}


std::error_code replaceCompletedSavePayload(const std::filesystem::path& temporary,
                                            const std::filesystem::path& resolvedTarget) {
    std::error_code ec;
    std::filesystem::rename(temporary, resolvedTarget, ec);
    return ec;
}

void validateCompletedTemporaryPayload(const std::filesystem::path& temporary) {
    if (temporary.filename() != "payload.tmp" || !isManagedTemporaryDirectory(temporary.parent_path())) {
        throw std::runtime_error("Refusing to commit unmanaged temporary save payload \"" + temporary.string() + "\".");
    }

    std::error_code ec;
    const auto status = std::filesystem::symlink_status(temporary, ec);
    if (ec) {
        throw std::runtime_error("Unable to inspect temporary save payload \"" + temporary.string() + "\": " + ec.message());
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("Refusing to commit non-regular temporary save payload \"" + temporary.string() + "\".");
    }
}

bool createPrivateTemporaryDirectory(const std::filesystem::path& directory, std::error_code& ec) {
    return std::filesystem::create_directory(directory, ec);
}

#ifndef _WIN32
void syncCompletedPayloadToDisk(const std::filesystem::path& path) {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        throw std::runtime_error("Unable to open completed temporary save payload for durability sync: \"" + path.string() + "\".");
    }
    if (::fsync(fd) != 0) {
        const int savedErrno = errno;
        (void)::close(fd);
        throw std::runtime_error("Unable to sync completed temporary save payload \"" + path.string() + "\": " +
                                 std::generic_category().message(savedErrno));
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("Unable to close completed temporary save payload after durability sync: \"" + path.string() + "\".");
    }
}

void syncDirectoryNoThrow(const std::filesystem::path& directory) noexcept {
    const std::filesystem::path target = directory.empty() ? std::filesystem::path(".") : directory;
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(target.c_str(), flags);
    if (fd < 0) {
        return;
    }
    (void)::fsync(fd);
    (void)::close(fd);
}
#else
void syncCompletedPayloadToDisk(const std::filesystem::path&) {}
void syncDirectoryNoThrow(const std::filesystem::path&) noexcept {}
#endif

std::uint64_t hashRegularFileContents(const std::filesystem::path& path, const std::string& what) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to read " + what + " contents for safety snapshot \"" + path.string() + "\".");
    }

    std::uint64_t hash = 14695981039346656037ull;
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    if (in.bad()) {
        throw std::runtime_error("Unable to complete " + what + " contents safety snapshot \"" + path.string() + "\".");
    }
    return hash;
}

std::filesystem::path canonicalizeSaveTargetLocation(const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(target, ec);
    if (ec || resolved.empty()) {
        throw std::runtime_error("Unable to resolve save target location \"" + target.string() + "\"" +
                                 (ec ? std::string(": ") + ec.message() : std::string{}));
    }
    resolved = resolved.lexically_normal();
    if (resolved.is_relative()) {
        ec.clear();
        resolved = std::filesystem::absolute(resolved, ec).lexically_normal();
        if (ec || resolved.empty()) {
            throw std::runtime_error("Unable to make save target location absolute \"" + target.string() + "\"" +
                                     (ec ? std::string(": ") + ec.message() : std::string{}));
        }
    }
    return resolved;
}

std::string nativeReplacementTargetIdentity(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal().string() : canonical.lexically_normal().string();
}

SaveTargetSnapshot captureResolvedReplacementTargetSnapshot(const std::filesystem::path& resolvedTarget) {
    SaveTargetSnapshot snapshot;
    snapshot.resolvedTarget = resolvedTarget;

    std::error_code ec;
    const auto linkStatus = std::filesystem::symlink_status(resolvedTarget, ec);
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory) {
            return snapshot;
        }
        throw std::runtime_error("Unable to inspect replacement target \"" + resolvedTarget.string() + "\": " + ec.message());
    }

    if (std::filesystem::is_symlink(linkStatus)) {
        throw std::runtime_error("Refusing to replace symlink replacement target \"" + resolvedTarget.string() + "\".");
    }
    if (!std::filesystem::is_regular_file(linkStatus)) {
        throw std::runtime_error("Refusing to replace non-regular file \"" + resolvedTarget.string() + "\".");
    }

    std::error_code linkCountError;
    const auto linkCount = std::filesystem::hard_link_count(resolvedTarget, linkCountError);
    if (!linkCountError && linkCount > 1u) {
        throw std::runtime_error("Refusing to atomically replace hard-linked file \"" + resolvedTarget.string() +
                                 "\" because that would silently break the link group.");
    }

    snapshot.existed = true;

    ec.clear();
    snapshot.size = std::filesystem::file_size(resolvedTarget, ec);
    if (ec) {
        throw std::runtime_error("Unable to inspect replacement target size \"" + resolvedTarget.string() + "\": " + ec.message());
    }

    ec.clear();
    snapshot.lastWriteTime = std::filesystem::last_write_time(resolvedTarget, ec);
    snapshot.hasLastWriteTime = !ec;

    snapshot.identity = nativeReplacementTargetIdentity(resolvedTarget);
    snapshot.contentHash = hashRegularFileContents(resolvedTarget, "replacement target");
    snapshot.hasContentHash = true;
    return snapshot;
}

void compareStableFileSnapshot(const SaveTargetSnapshot& expected,
                               const SaveTargetSnapshot& current,
                               const std::string& missingMessage,
                               const std::string& swappedMessage,
                               const std::string& sizeMessage,
                               const std::string& modifiedMessage) {
    if (!current.existed) {
        throw std::runtime_error(missingMessage);
    }

    if (!expected.identity.empty() && !current.identity.empty() && expected.identity != current.identity) {
        throw std::runtime_error(swappedMessage);
    }

    if (expected.size != current.size) {
        throw std::runtime_error(sizeMessage);
    }

    if (expected.hasLastWriteTime && current.hasLastWriteTime && expected.lastWriteTime != current.lastWriteTime) {
        throw std::runtime_error(modifiedMessage);
    }

    if (expected.hasContentHash && current.hasContentHash && expected.contentHash != current.contentHash) {
        throw std::runtime_error(modifiedMessage);
    }
}

void validateReplacementTargetMatchesSnapshot(const SaveTargetSnapshot& expected) {
    const SaveTargetSnapshot current = captureResolvedReplacementTargetSnapshot(expected.resolvedTarget);

    if (!expected.existed) {
        if (current.existed) {
            throw std::runtime_error("Refusing to replace newly appeared file \"" + expected.resolvedTarget.string() +
                                     "\" during save commit.");
        }
        return;
    }

    compareStableFileSnapshot(expected,
                              current,
                              "Refusing to recreate replacement target \"" + expected.resolvedTarget.string() +
                                  "\" because it disappeared during save commit.",
                              "Refusing to replace \"" + expected.resolvedTarget.string() +
                                  "\" because it was swapped during save commit.",
                              "Refusing to replace \"" + expected.resolvedTarget.string() +
                                  "\" because it changed size during save commit.",
                              "Refusing to replace \"" + expected.resolvedTarget.string() +
                                  "\" because it was modified during save commit.");
}

} // namespace

bool iequals(const std::string& lhs, const std::string& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           });
}

void writeSaveSnapshotSentinel(std::ostream& sentinel, const SaveTargetSnapshot& target) {
    const auto ticks = target.hasLastWriteTime
        ? target.lastWriteTime.time_since_epoch().count()
        : std::filesystem::file_time_type::duration::rep{};
    sentinel << "NEOSSF_SAVE_SNAPSHOT_V1\n"
             << std::quoted(target.resolvedTarget.string()) << '\n'
             << (target.existed ? 1 : 0) << ' '
             << target.size << ' '
             << (target.hasLastWriteTime ? 1 : 0) << ' '
             << ticks << ' '
             << (target.hasContentHash ? 1 : 0) << ' '
             << target.contentHash << '\n'
             << std::quoted(target.identity) << '\n';
    if (!sentinel) {
        throw std::runtime_error("Unable to write managed temporary save sentinel.");
    }
}

SaveTargetSnapshot readSaveSnapshotSentinel(const std::filesystem::path& temporary) {
    if (temporary.filename() != "payload.tmp" || !isManagedTemporaryDirectory(temporary.parent_path())) {
        throw std::runtime_error("Refusing to use unmanaged temporary save payload \"" + temporary.string() + "\".");
    }

    std::ifstream sentinel(temporary.parent_path() / kManagedTempSentinel, std::ios::binary);
    if (!sentinel) {
        throw std::runtime_error("Unable to read managed temporary save sentinel for \"" + temporary.string() + "\".");
    }

    std::string magic;
    std::getline(sentinel, magic);
    if (magic != "NEOSSF_SAVE_SNAPSHOT_V1") {
        throw std::runtime_error("Managed temporary save sentinel is missing snapshot metadata for \"" + temporary.string() + "\".");
    }

    std::string resolvedTargetText;
    int existed = 0;
    int hasLastWriteTime = 0;
    int hasContentHash = 0;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type::duration::rep ticks{};
    std::uint64_t contentHash = 0;
    std::string identity;

    sentinel >> std::quoted(resolvedTargetText);
    sentinel >> existed >> size >> hasLastWriteTime >> ticks >> hasContentHash >> contentHash;
    sentinel >> std::quoted(identity);
    if (!sentinel || resolvedTargetText.empty()) {
        throw std::runtime_error("Managed temporary save sentinel is malformed for \"" + temporary.string() + "\".");
    }

    SaveTargetSnapshot snapshot;
    snapshot.resolvedTarget = std::filesystem::path(resolvedTargetText);
    snapshot.existed = existed != 0;
    snapshot.size = size;
    snapshot.hasLastWriteTime = hasLastWriteTime != 0;
    if (snapshot.hasLastWriteTime) {
        snapshot.lastWriteTime = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(ticks));
    }
    snapshot.hasContentHash = hasContentHash != 0;
    snapshot.contentHash = contentHash;
    snapshot.identity = std::move(identity);
    return snapshot;
}

void makeFileWritable(const std::filesystem::path& path) {
    std::error_code existsError;
    if (!std::filesystem::is_regular_file(path, existsError) || existsError) {
        return;
    }

    std::error_code ec;
    const auto current = std::filesystem::status(path, ec).permissions();
    if (!ec) {
        std::filesystem::permissions(path, current | std::filesystem::perms::owner_write, ec);
    }
}


std::filesystem::path resolveSaveTargetPath(const std::filesystem::path& target);

SaveTargetSnapshot prepareSaveTarget(const std::filesystem::path& target) {
    const std::filesystem::path resolvedTarget = resolveSaveTargetPath(target);
    return captureResolvedReplacementTargetSnapshot(resolvedTarget);
}

std::filesystem::path resolveSaveTargetPath(const std::filesystem::path& target) {
    if (target.empty()) {
        throw std::runtime_error("Unable to save to an empty filename.");
    }

    std::filesystem::path current = target;
    for (unsigned int depth = 0; depth < 32u; ++depth) {
        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(current, statusError);
        if (statusError) {
            if (statusError == std::errc::no_such_file_or_directory ||
                statusError == std::errc::not_a_directory) {
                return canonicalizeSaveTargetLocation(current);
            }
            throw std::runtime_error("Unable to inspect save target \"" + current.string() + "\": " + statusError.message());
        }
        if (!std::filesystem::is_symlink(status)) {
            return canonicalizeSaveTargetLocation(current);
        }

        std::error_code readError;
        std::filesystem::path linkTarget = std::filesystem::read_symlink(current, readError);
        if (readError || linkTarget.empty()) {
            throw std::runtime_error("Unable to resolve save-target symlink \"" + current.string() + "\": " + readError.message());
        }
        if (linkTarget.is_relative()) {
            current = current.parent_path() / linkTarget;
        } else {
            current = std::move(linkTarget);
        }
        current = current.lexically_normal();
    }

    throw std::runtime_error("Save target \"" + target.string() + "\" contains too many nested symlinks.");
}

std::filesystem::path makeTemporarySiblingPath(const SaveTargetSnapshot& target) {
    if (target.resolvedTarget.empty()) {
        throw std::runtime_error("Unable to create a temporary save path for an empty filename.");
    }
    // Fail before staging data if the selected replacement path changed after the
    // caller captured it. The commit path repeats this validation immediately
    // before replacement to close the later race window too.
    validateReplacementTargetMatchesSnapshot(target);

    const std::filesystem::path& resolvedTarget = target.resolvedTarget;
    std::filesystem::path parent = resolvedTarget.parent_path();
    if (parent.empty()) {
        parent = ".";
    }

    std::string baseName = resolvedTarget.filename().string();
    if (baseName.empty()) {
        baseName = "target";
    }

    // Reserve a private temporary directory atomically, then place the staged file
    // inside it. This avoids the check-then-open race that can happen when a save
    // path is chosen only by probing for non-existence and then opening with trunc.
    const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0; attempt < 1000u; ++attempt) {
        std::filesystem::path directory = parent / (baseName + kManagedTempMarker + std::to_string(seed) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (!createPrivateTemporaryDirectory(directory, ec)) {
            continue;
        }
        std::ofstream sentinel(directory / kManagedTempSentinel, std::ios::binary | std::ios::trunc);
        if (sentinel) {
            writeSaveSnapshotSentinel(sentinel, target);
            sentinel.close();
            if (sentinel) {
                return directory / "payload.tmp";
            }
        }

        std::filesystem::remove(directory / kManagedTempSentinel, ec);
        ec.clear();
        std::filesystem::remove(directory, ec);
    }

    throw std::runtime_error("Unable to create a unique temporary save path next to \"" + resolvedTarget.string() + "\".");
}

void removeFileNoThrow(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    cleanupManagedTemporaryDirectory(path);
}

void commitTemporarySaveFileToResolvedTarget(const std::filesystem::path& temporary, const SaveTargetSnapshot& target) {
    if (temporary.empty() || target.resolvedTarget.empty()) {
        throw std::runtime_error("Unable to commit a save with an empty filename.");
    }

    validateCompletedTemporaryPayload(temporary);
    syncCompletedPayloadToDisk(temporary);

    // Callers pass the already resolved replacement path that was used to choose
    // the sibling temp directory. Do not resolve again here: if a symlink appears
    // at that path between staging and commit, following it would write/move data
    // to a different referent than the one preflighted. Also verify the target's
    // existence and identity against the pre-write snapshot so an unrelated file
    // cannot appear, disappear, or be swapped during the save window.
    const std::filesystem::path& resolvedTarget = target.resolvedTarget;
    validateReplacementTargetMatchesSnapshot(target);

    std::error_code statusError;
    const bool targetExisted = target.existed;
    const auto originalPermissions = targetExisted
        ? std::filesystem::status(resolvedTarget, statusError).permissions()
        : std::filesystem::perms::unknown;
    const bool haveOriginalPermissions = targetExisted && !statusError;

    auto tryRename = [&]() -> std::error_code {
        return replaceCompletedSavePayload(temporary, resolvedTarget);
    };

    std::error_code ec = tryRename();
    if (ec && targetExisted) {
        // Only mutate destination metadata after a completed staged file exists and
        // the first replacement attempt failed. Revalidate before and after the
        // permission adjustment so the retry cannot overwrite a target that changed
        // while save was in progress.
        const std::error_code firstError = ec;
        validateReplacementTargetMatchesSnapshot(target);
        makeFileWritable(resolvedTarget);
        validateReplacementTargetMatchesSnapshot(target);
        ec = tryRename();
        if (ec && haveOriginalPermissions) {
            restorePermissionsNoThrow(resolvedTarget, originalPermissions);
        }
        if (ec) {
            throw std::runtime_error("Unable to replace \"" + resolvedTarget.string() + "\" with completed save data: " +
                                     firstError.message() + "; retry after permission adjustment: " + ec.message());
        }
    } else if (ec) {
        throw std::runtime_error("Unable to replace \"" + resolvedTarget.string() + "\" with completed save data: " + ec.message());
    }

    if (haveOriginalPermissions) {
        restorePermissionsNoThrow(resolvedTarget, originalPermissions);
    }

    const std::filesystem::path targetParent = resolvedTarget.parent_path().empty()
        ? std::filesystem::path(".")
        : resolvedTarget.parent_path();
    syncDirectoryNoThrow(targetParent);
    cleanupManagedTemporaryDirectory(temporary);
    syncDirectoryNoThrow(targetParent);
}

bool isStartupAutoloadSsfPath(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return false;
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".ssf";
}

} // namespace neossf
