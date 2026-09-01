#include "core/AppModel.hpp"
#include "core/SSFFile.hpp"
#include "TabularData.hpp"
#include "core/SSFJson.hpp"
#include "core/SsfPatcher.hpp"
#include "TslPatcher.hpp"
#include "core/TLKFile.hpp"
#include "core/Version.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace neossf;

namespace {
void usage() {
    std::cout << "NeoSSF " << kVersion << " C++ core utility\n"
              << "Usage:\n"
              << "  neossf-cli --dump-ssf <file.ssf> [filter-term]\n"
              << "  neossf-cli --search-ssf <file.ssf> <term>\n"
              << "  neossf-cli --export-ssf <file.ssf> <csv|tsv|xml|json> <output> [filter-term]\n"
              << "  neossf-cli --import-ssf <input-table> <csv|tsv|xml|json> <output.ssf>\n"
              << "  neossf-cli --diff-tslpatcher <original.ssf> <modified-input> <output-dir|fragment.ini> [--modified-format csv|tsv|xml|json|ssf|kotor|native|auto] [--package|--fragment] [--filename name] [--ini installer.ini] [--original-tlk clean.tlk --modified-tlk edited.tlk] [--allow-unsupported]\n"
              << "  neossf-cli --diff-tslpatcher-import <original.ssf> <modified-input> <csv|tsv|xml|json|ssf|kotor|native|auto> <output-dir|fragment.ini> [--package|--fragment] [--filename name] [--ini installer.ini] [--original-tlk clean.tlk --modified-tlk edited.tlk] [--allow-unsupported]\n"
              << "    TLK pairs require package output and generate append.tlk plus dynamic StrRefN SSF assignments.\n"
              << "  neossf-cli --roundtrip-ssf <input.ssf> <output.ssf> [kotor|nwn|nwn2]\n"
              << "  neossf-cli --ssf-info <file.ssf>\n"
              << "  neossf-cli --tlk-info <dialog.tlk>\n";
}

std::string readTextFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to open text file: " + file.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to open text file for writing: " + file.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("Unable to write text file: " + file.string());
}

void printTableTsv(const neotabular::Table& table) {
    for (std::size_t c = 0; c < table.columns.size(); ++c) {
        if (c) std::cout << '\t';
        std::cout << table.columns[c];
    }
    std::cout << '\n';
    for (const auto& row : table.rows) {
        for (std::size_t c = 0; c < row.size(); ++c) {
            if (c) std::cout << '\t';
            std::cout << row[c];
        }
        std::cout << '\n';
    }
}

struct PatchOutputOptions {
    bool package = true;
    bool allowUnsupported = false;
    std::string patchFilename;
    std::string modifiedFormat = "auto";
    std::filesystem::path originalTlk;
    std::filesystem::path modifiedTlk;
    std::filesystem::path iniFilename = "changes.ini";

    bool hasTlkPair() const noexcept { return !originalTlk.empty() && !modifiedTlk.empty(); }
};

PatchOutputOptions parsePatchOutputOptions(int argc, char** argv, int begin, const std::filesystem::path& original) {
    PatchOutputOptions options;
    options.patchFilename = neotsl::basenameForPatch(original);
    for (int i = begin; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--package") options.package = true;
        else if (arg == "--fragment") options.package = false;
        else if (arg == "--filename") {
            if (i + 1 >= argc) throw std::runtime_error("--filename requires a value.");
            options.patchFilename = argv[++i];
        } else if (arg == "--ini") {
            if (i + 1 >= argc) throw std::runtime_error("--ini requires a filename.");
            options.iniFilename = argv[++i];
        } else if (arg == "--allow-unsupported") options.allowUnsupported = true;
        else if (arg == "--original-tlk") {
            if (i + 1 >= argc) throw std::runtime_error("--original-tlk requires a value.");
            options.originalTlk = argv[++i];
        } else if (arg == "--modified-tlk") {
            if (i + 1 >= argc) throw std::runtime_error("--modified-tlk requires a value.");
            options.modifiedTlk = argv[++i];
        } else if (arg == "--tslpatcher" || arg == "--holopatcher") {
            // NeoSSF's append.tlk + SSFList package is compatible with both patchers.
            options.package = true;
        } else if (arg == "--modified-format" || arg == "--input-format") {
            if (i + 1 >= argc) throw std::runtime_error(arg + " requires a value.");
            options.modifiedFormat = argv[++i];
        }
        else throw std::runtime_error("Unknown --diff-tslpatcher option: " + arg);
    }
    return options;
}

void writePatchOutput(const neotsl::PatchProject& project, const std::filesystem::path& output, const PatchOutputOptions& options) {
    if (!options.allowUnsupported) neotsl::throwIfUnsupported(project);
    else neotsl::printReport(project);
    if (options.package) {
        const std::filesystem::path iniPath = options.iniFilename.is_absolute()
            ? options.iniFilename
            : output / options.iniFilename;
        neotsl::writePackageToIni(project, iniPath, true);
    } else {
        neotsl::writeFragment(project, output);
    }
}

void validateTlkPatchOptions(const PatchOutputOptions& options) {
    const bool hasOriginal = !options.originalTlk.empty();
    const bool hasModified = !options.modifiedTlk.empty();
    if (hasOriginal != hasModified) {
        throw std::runtime_error("--original-tlk and --modified-tlk must be supplied together.");
    }
    if ((hasOriginal || hasModified) && !options.package) {
        throw std::runtime_error("Combined SSF/TLK output requires a complete package because append.tlk is a required binary payload.");
    }
}

void writeCombinedPatchOutput(const AppModel& original,
                              const AppModel& modified,
                              const PatchOutputOptions& options,
                              const std::filesystem::path& output) {
    validateTlkPatchOptions(options);
    if (!options.hasTlkPair()) {
        SsfTlkPatcherOptions patcherOptions;
        patcherOptions.patchFilename = options.patchFilename;
        patcherOptions.copyBaselineAsset = options.package;
        auto result = diffSsfAndTlkForPatcher(
            original,
            modified,
            nullptr,
            0u,
            patcherOptions,
            options.package ? original.ssfFile() : std::filesystem::path{});
        if (options.package) {
            const std::filesystem::path iniPath = options.iniFilename.is_absolute()
                ? options.iniFilename
                : output / options.iniFilename;
            writeSsfTlkPatcherPackageToIni(result, iniPath, options.allowUnsupported);
        } else {
            writePatchOutput(result.project, output, options);
        }
        return;
    }

    TalkTable originalTlk(options.originalTlk.string());
    TalkTable modifiedTlk(options.modifiedTlk.string());
    SsfTlkPatcherOptions patcherOptions;
    patcherOptions.patchFilename = options.patchFilename;
    patcherOptions.originalTlkPath = options.originalTlk;
    patcherOptions.modifiedTlkPath = options.modifiedTlk;
    auto result = diffSsfAndTlkForPatcher(
        original,
        modified,
        originalTlk,
        modifiedTlk,
        patcherOptions,
        original.ssfFile());
    const std::filesystem::path iniPath = options.iniFilename.is_absolute()
        ? options.iniFilename
        : output / options.iniFilename;
    writeSsfTlkPatcherPackageToIni(result, iniPath, options.allowUnsupported);
    std::cout << "Generated complete TSLPatcher/HoloPatcher package: " << output.string() << '\n'
              << "  Changed SSF slots: " << result.changedSsfSlots << '\n'
              << "  Dynamic SSF StrRef assignments: " << result.dynamicSsfAssignments << '\n'
              << "  Fixed SSF StrRef assignments: " << result.fixedSsfAssignments << '\n'
              << "  Appended TLK entries: " << result.appendedTlkEntries << '\n';
}


std::string lowerAsciiLocal(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string extensionImportFormat(const std::filesystem::path& path) {
    std::string ext = lowerAsciiLocal(path.extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    if (ext == "csv" || ext == "tsv" || ext == "xml" || ext == "json") return ext;
    return "native";
}

bool isNativeSsfImportFormat(std::string formatName) {
    formatName = lowerAsciiLocal(std::move(formatName));
    return formatName == "native" || formatName == "kotor" || formatName == "ssf";
}

AppModel loadSsfFromImport(const std::filesystem::path& originalPath,
                           const std::filesystem::path& inputPath,
                           std::string formatName) {
    formatName = lowerAsciiLocal(std::move(formatName));
    if (formatName.empty() || formatName == "auto") formatName = extensionImportFormat(inputPath);

    AppModel model;
    if (isNativeSsfImportFormat(formatName)) {
        model.loadSsf(inputPath);
        return model;
    }

    const auto format = neotabular::parseFormat(formatName);
    if (format == neotabular::Format::Xml) {
        model.importXml(readTextFile(inputPath));
    } else if (format == neotabular::Format::Json) {
        model.importXml(ssfJsonToXml(readTextFile(inputPath)));
    } else {
        model.loadSsf(originalPath);
        model.importSsfTable(neotabular::readTable(inputPath, format));
    }
    return model;
}


} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" || std::string(argv[1]) == "help")) {
            usage();
            return 0;
        }
        if (argc >= 2 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-v" || std::string(argv[1]) == "version")) {
            std::cout << "NeoSSF " << kVersion << '\n';
            return 0;
        }
        if (argc < 3) {
            usage();
            return 1;
        }

        const std::string command = argv[1];
        const std::filesystem::path path = argv[2];

        if (command == "--dump-ssf") {
            if (argc < 3 || argc > 4) {
                usage();
                return 1;
            }
            AppModel model;
            model.loadSsf(path);
            auto table = model.toTable(false);
            if (argc == 4) {
                table = neotabular::filterRows(table, argv[3]);
                ensureSsfTableMetadataRow(table, model.ssfFormat());
            }
            printTableTsv(table);
            return 0;
        }

        if (command == "--search-ssf") {
            if (argc != 4) {
                usage();
                return 1;
            }
            AppModel model;
            model.loadSsf(path);
            auto table = neotabular::filterRows(model.toTable(false), argv[3]);
            ensureSsfTableMetadataRow(table, model.ssfFormat());
            printTableTsv(table);
            return 0;
        }

        if (command == "--export-ssf") {
            if (argc < 5 || argc > 6) {
                usage();
                return 1;
            }
            AppModel model;
            model.loadSsf(path);
            const auto format = neotabular::parseFormat(argv[3]);
            if (format == neotabular::Format::Xml || format == neotabular::Format::Json) {
                if (argc == 6) {
                    throw std::runtime_error("Filtering semantic SSF XML/JSON export is not supported; export CSV/TSV for filtered spreadsheet output.");
                }
                const std::string xml = model.toXml(false);
                writeTextFile(argv[4], format == neotabular::Format::Json ? ssfXmlToJson(xml) : xml);
            } else {
                auto table = model.toTable(false);
                if (argc == 6) {
                    table = neotabular::filterRows(table, argv[5]);
                    ensureSsfTableMetadataRow(table, model.ssfFormat());
                }
                neotabular::writeTable(table, argv[4], format);
            }
            return 0;
        }

        if (command == "--import-ssf") {
            if (argc != 5) {
                usage();
                return 1;
            }
            AppModel model;
            const auto format = neotabular::parseFormat(argv[3]);
            if (format == neotabular::Format::Xml) {
                model.importXml(readTextFile(path));
            } else if (format == neotabular::Format::Json) {
                model.importXml(ssfJsonToXml(readTextFile(path)));
            } else {
                model.importSsfTable(neotabular::readTable(path, format));
            }
            model.saveSsf(std::filesystem::path(argv[4]));
            return 0;
        }


        if (command == "--diff-tslpatcher" || command == "diff-tslpatcher" || command == "--diff-tslpatcher-import" || command == "diff-tslpatcher-import") {
            const bool explicitImport = command == "--diff-tslpatcher-import" || command == "diff-tslpatcher-import";
            if ((!explicitImport && argc < 5) || (explicitImport && argc < 6)) {
                usage();
                return 1;
            }
            const std::filesystem::path originalPath = argv[2];
            const std::filesystem::path modifiedPath = argv[3];
            if (explicitImport) {
                auto options = parsePatchOutputOptions(argc, argv, 6, originalPath);
                options.modifiedFormat = argv[4];
                AppModel original;
                original.loadSsf(originalPath);
                AppModel modified = loadSsfFromImport(originalPath, modifiedPath, options.modifiedFormat);
                writeCombinedPatchOutput(original, modified, options, argv[5]);
                return 0;
            }
            const std::filesystem::path output = argv[4];
            const auto options = parsePatchOutputOptions(argc, argv, 5, originalPath);
            AppModel original;
            original.loadSsf(originalPath);
            AppModel modified = loadSsfFromImport(originalPath, modifiedPath, options.modifiedFormat);
            writeCombinedPatchOutput(original, modified, options, output);
            return 0;
        }

        if (command == "--roundtrip-ssf") {
            if (argc != 4 && argc != 5) {
                usage();
                return 1;
            }
            SSFFile ssf(path);
            if (argc == 5) {
                const std::string format = lowerAsciiLocal(argv[4]);
                if (format == "kotor" || format == "kotor1" || format == "kotor2") ssf.setFormat(SSFFormat::KotOR_V11);
                else if (format == "nwn" || format == "nwn1" || format == "v1.0") ssf.setFormat(SSFFormat::NWN_V10);
                else if (format == "nwn2" || format == "v1.1-nwn2") ssf.setFormat(SSFFormat::NWN2_V11);
                else throw std::runtime_error("Unknown SSF output format: " + format);
            }
            ssf.save(std::filesystem::path(argv[3]));
            return 0;
        }

        if (command == "--ssf-info") {
            SSFFile ssf(path);
            std::size_t soundFileCount = 0;
            for (const auto& sound : ssf.soundFiles()) if (!sound.empty()) ++soundFileCount;
            std::cout << "Type: SSF" << '\n'
                      << "Format: " << ssfFormatName(ssf.format()) << '\n'
                      << "EditableEntries: " << ssf.entries().size() << '\n'
                      << "ExtraEntries: " << ssf.extraEntries().size() << '\n'
                      << "DiskEntries: " << ssf.diskEntryCount() << '\n'
                      << "DirectSoundFiles: " << soundFileCount << '\n';
            return 0;
        }


        if (command == "--tlk-info") {
            TalkTable tlk(path.string());
            std::cout << "Type: " << fourCCToString(tlk.fileId()) << '\n'
                      << "Version: " << fourCCToString(tlk.version()) << '\n'
                      << "LanguageId: " << tlk.language() << '\n'
                      << "StringCount: " << tlk.count() << '\n';
            return 0;
        }

        usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 2;
    }
}
