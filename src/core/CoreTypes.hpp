#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace neossf {

using UInt32 = std::uint32_t;
using ResRef = std::array<char, 16>;
using FourCC = std::array<char, 4>;

constexpr UInt32 kUnsetStrRef = 0xFFFFFFFFu;
constexpr std::size_t kSoundsetEntryCount = 40;
constexpr std::size_t kExtendedSoundsetEntryCount = 49;
constexpr UInt32 kTextPresent = 0x0001u;
constexpr UInt32 kSoundPresent = 0x0002u;
constexpr UInt32 kSoundLengthPresent = 0x0004u;

struct SoundsetSlotLabel {
    const char* display;
    const char* xml;
};

const std::array<SoundsetSlotLabel, kSoundsetEntryCount>& soundsetSlotLabels();
const std::array<SoundsetSlotLabel, kExtendedSoundsetEntryCount>& extendedSoundsetSlotLabels();
std::string soundsetDisplayLabel(std::size_t zeroBasedIndex, std::size_t totalEntryCount = kSoundsetEntryCount);
std::string soundsetXmlLabel(std::size_t zeroBasedIndex, std::size_t totalEntryCount);

std::string fourCCToString(const FourCC& value);
FourCC makeFourCC(const char (&literal)[5]);
std::string resRefToString(const ResRef& value);
ResRef makeResRefFromString(const std::string& value);

} // namespace neossf
