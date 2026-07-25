#include "core/CoreTypes.hpp"

#include <algorithm>

namespace neossf {

const std::array<SoundsetSlotLabel, kSoundsetEntryCount>& soundsetSlotLabels() {
    static const std::array<SoundsetSlotLabel, kSoundsetEntryCount> labels = {{
        {"Battlecry 1", "BattleCry1"},
        {"Battlecry 2", "BattleCry2"},
        {"Battlecry 3", "BattleCry3"},
        {"Battlecry 4", "BattleCry4"},
        {"Battlecry 5", "BattleCry5"},
        {"Battlecry 6", "BattleCry6"},
        {"Selected 1", "Selected1"},
        {"Selected 2", "Selected2"},
        {"Selected 3", "Selected3"},
        {"Attack 1", "GruntAttack1"},
        {"Attack 2", "GruntAttack2"},
        {"Attack 3", "GruntAttack3"},
        {"Pain 1", "Pain1"},
        {"Pain 2", "Pain2"},
        {"Low health", "NearDeath"},
        {"Death", "Death"},
        {"Critical hit", "Critical"},
        {"Target immune", "WeaponSucks"},
        {"Place mine", "FoundMine"},
        {"Disarm mine", "DisabledMine"},
        {"Stealth on", "Hide"},
        {"Search", "Search"},
        {"Pick lock start", "PickLock"},
        {"Pick lock fail", "CanDo"},
        {"Pick lock done", "CantDo"},
        {"Leave party", "Single"},
        {"Rejoin party", "Group"},
        {"Poisoned", "Poisoned"},
        {"Unknown (29)", ""},
        {"Unknown (30)", ""},
        {"Unknown (31)", ""},
        {"Unknown (32)", ""},
        {"Unknown (33)", ""},
        {"Unknown (34)", ""},
        {"Unknown (35)", ""},
        {"Unknown (36)", ""},
        {"Unknown (37)", ""},
        {"Unknown (38)", ""},
        {"Unknown (39)", ""},
        {"Unknown (40)", ""},
    }};
    return labels;
}

const std::array<SoundsetSlotLabel, kExtendedSoundsetEntryCount>& extendedSoundsetSlotLabels() {
    static const std::array<SoundsetSlotLabel, kExtendedSoundsetEntryCount> labels = {{
        {"Attack", "Attack"},
        {"Battlecry 1", "BattleCry1"},
        {"Battlecry 2", "BattleCry2"},
        {"Battlecry 3", "BattleCry3"},
        {"Heal me", "HealMe"},
        {"Help", "Help"},
        {"Enemies", "Enemies"},
        {"Flee", "Flee"},
        {"Taunt", "Taunt"},
        {"Guard me", "GuardMe"},
        {"Hold", "Hold"},
        {"Attack 1", "GruntAttack1"},
        {"Attack 2", "GruntAttack2"},
        {"Attack 3", "GruntAttack3"},
        {"Pain 1", "Pain1"},
        {"Pain 2", "Pain2"},
        {"Pain 3", "Pain3"},
        {"Low health", "NearDeath"},
        {"Death", "Death"},
        {"Poisoned", "Poisoned"},
        {"Spell failed", "SpellFailed"},
        {"Target immune", "WeaponSucks"},
        {"Follow me", "FollowMe"},
        {"Look here", "LookHere"},
        {"Group", "Group"},
        {"Move over", "MoveOver"},
        {"Pick lock", "PickLock"},
        {"Search", "Search"},
        {"Stealth on", "Hide"},
        {"Can do", "CanDo"},
        {"Can't do", "CantDo"},
        {"Task complete", "TaskComplete"},
        {"Encumbered", "Encumbered"},
        {"Selected", "Selected"},
        {"Hello", "Hello"},
        {"Yes", "Yes"},
        {"No", "No"},
        {"Stop", "Stop"},
        {"Rest", "Rest"},
        {"Bored", "Bored"},
        {"Goodbye", "Goodbye"},
        {"Thanks", "Thanks"},
        {"Laugh", "Laugh"},
        {"Cuss", "Cuss"},
        {"Cheer", "Cheer"},
        {"Talk to me", "TalkToMe"},
        {"Good idea", "GoodIdea"},
        {"Bad idea", "BadIdea"},
        {"Threaten", "Threaten"},
    }};
    return labels;
}

std::string soundsetDisplayLabel(std::size_t zeroBasedIndex, std::size_t totalEntryCount) {
    if (totalEntryCount == kExtendedSoundsetEntryCount) {
        return zeroBasedIndex < extendedSoundsetSlotLabels().size()
            ? std::string(extendedSoundsetSlotLabels()[zeroBasedIndex].display)
            : "Extra " + std::to_string(zeroBasedIndex + 1);
    }
    if (zeroBasedIndex < soundsetSlotLabels().size()) {
        return soundsetSlotLabels()[zeroBasedIndex].display;
    }
    return "Extra " + std::to_string(zeroBasedIndex + 1);
}

std::string soundsetXmlLabel(std::size_t zeroBasedIndex, std::size_t totalEntryCount) {
    if (totalEntryCount == kExtendedSoundsetEntryCount) {
        return zeroBasedIndex < extendedSoundsetSlotLabels().size()
            ? std::string(extendedSoundsetSlotLabels()[zeroBasedIndex].xml)
            : std::string();
    }
    if (totalEntryCount == kSoundsetEntryCount && zeroBasedIndex < soundsetSlotLabels().size()) {
        return soundsetSlotLabels()[zeroBasedIndex].xml;
    }
    return {};
}

std::string fourCCToString(const FourCC& value) {
    return std::string(value.begin(), value.end());
}

FourCC makeFourCC(const char (&literal)[5]) {
    return FourCC{literal[0], literal[1], literal[2], literal[3]};
}

std::string resRefToString(const ResRef& value) {
    auto end = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), end);
}

ResRef makeResRefFromString(const std::string& value) {
    ResRef result{};
    const std::size_t count = std::min<std::size_t>(result.size(), value.size());
    std::copy_n(value.begin(), count, result.begin());
    return result;
}

} // namespace neossf
