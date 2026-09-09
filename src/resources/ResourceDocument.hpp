#pragma once
#include "../core/AppModel.hpp"
#include <neoshared/ResourceDocument.hpp>
#include <algorithm>
#include <cctype>
namespace neossf {
struct ResourceDocument {
    neoshared::ResourceDocument source;
    AppModel model;
    static ResourceDocument load(neoshared::ResourceDocument input) {
        if (input.identity.empty() || input.type != 2060) throw std::runtime_error("This is not a valid NeoSSF resource handoff.");
        ResourceDocument result;
        result.model.loadSsfBytes(input.bytes);
        result.source = std::move(input);
        result.source.bytes.clear(); // the parsed model owns any preservation bytes it needs
        return result;
    }
    void checkSaveDestination(const std::filesystem::path& path) const {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {return static_cast<char>(std::tolower(c));});
        if (extension != ".ssf") throw std::runtime_error("Choose a separate .ssf working file.");
        neoshared::checkResourceOutput(path, source.protectedInputs);
    }
};
}
