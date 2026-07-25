# NeoSSF

[![CI](https://github.com/vrifftech/NeoSSF/actions/workflows/ci.yml/badge.svg)](https://github.com/vrifftech/NeoSSF/actions/workflows/ci.yml)

C++17 SSF/TLK editor core, CLI, and optional wxWidgets desktop GUI.

NeoSSF owns SSF editing and SSF/TLK patch-package logic. Native TLK parsing, editing, encoding, and save behavior come from the shared `neoshared::tlk` target.

## Build

This repository consumes shared code from the separate `neoshared` repository. Clone the repositories as siblings:

```text
workspace/
  neoshared/
  NeoSSF/
```

CMake automatically detects `../neoshared`. For another layout, pass `--neoshared-root /path/to/neoshared` to `build.sh`, `-NeoSharedRoot C:\path\to\neoshared` to `build.ps1`, or set `NEOSHARED_ROOT` directly.


Linux GUI build:

```sh
./scripts/build.sh --wx ON --require-wx ON --jobs "$(nproc)"
```

Linux CLI/core-only build:

```sh
./scripts/build.sh --wx OFF --jobs "$(nproc)"
```

Windows GUI build with static wxWidgets from vcpkg:

```powershell
.\scripts\build.ps1 `
  -Wx ON `
  -RequireWx ON `
  -VcpkgRoot C:\vcpkg `
  -VcpkgTriplet x64-windows-static `
  -Parallel ([Environment]::ProcessorCount)
```

Use `-Wx OFF` on Windows for a CLI/core-only build. The default build directory is `build/`.

## SSF format support

NeoSSF opens and saves the three SSF layouts currently used by the supported games:

- KotOR / KotOR II `SSF V1.1` StrRef-only tables.
- Neverwinter Nights / NWN EE `SSF V1.0` sound ResRef + StrRef tables with 16-byte sound ResRefs.
- Neverwinter Nights 2 `SSF V1.1` sound ResRef + StrRef tables with 32-byte sound ResRefs.

Loading a TLK is optional. Without a TLK, StrRefs remain editable as numbers and NWN/NWN2 direct `SoundFile` ResRefs remain visible and editable. Loading a TLK only fills the preview text/sound columns and enables creating new TLK entries from the SSF editor.

## Tabular import/export

`neossf-cli` supports `--search-ssf`, `--export-ssf`, and `--import-ssf` for CSV, TSV, XML, and JSON. CSV/TSV use the spreadsheet table schema and include an `SSFFormat` column (`KotOR_V11`, `NWN_V10`, or `NWN2_V11`) so a short NWN2 sound ResRef cannot silently downgrade the output to the NWN layout. XML and JSON use complete semantic SSF sound records and carry the same native format at the document root. NWN/NWN2 direct `SoundFile` ResRefs are exported explicitly instead of being hidden in text payloads. The `Text` and `Sound` columns in CSV/TSV are resolved TLK preview values and are ignored on import; canonical SSF fields are `StrRef` for KotOR and `SoundFile` for NWN/NWN2. The GUI provides matching **Import** and **Export** menus, filtering, and StrRef-cell copy/paste. Semantic XML/JSON export is intentionally complete and unfiltered; use CSV/TSV to export a filtered visible row set.

## TSLPatcher/HoloPatcher output

TSLPatcher and HoloPatcher both support KotOR `SSF V1.1` patching through `[SSFList]`. They also allow an SSF slot to use a `StrRefN` token produced by `[TLKList]`, so a new line can be appended to the user's `dialog.tlk` and assigned to the SSF without relying on a fixed final StrRef.

The GUI command is:

```text
Export -> Export TSLPatcher/HoloPatcher SSF + TLK Package...
```

The GUI workflow is:

1. Open the SSF that you are editing.
2. Load the appropriate `dialog.tlk` when TLK preview/entry creation is needed.
3. Use NeoSSF's **Add TLK Entry** operation. NeoSSF records the TLK row count present when the TLK was loaded.
4. Select **Export TSLPatcher/HoloPatcher SSF + TLK Package...**.
5. Choose the clean original SSF, the target SSF filename, and the destination `tslpatchdata` folder.

Rows added after the active tab loaded its TLK are written to `append.tlk`. Any SSF slot that references one of those rows is written as a dynamic token such as:

```ini
[TLKList]
StrRef0=0

[SSFList]
File0=my_soundset.ssf

[my_soundset.ssf]
Battlecry 1=StrRef0
Unknown (29)=StrRef0
```

The installer resolves `StrRef0` to the actual destination StrRef assigned in the user's TLK. The same package is usable by stock TSLPatcher and HoloPatcher.

For file-to-file or automated preparation, provide clean and modified TLKs to the CLI:

```sh
neossf-cli --diff-tslpatcher original.ssf modified.ssf tslpatchdata \
  --filename my_soundset.ssf \
  --original-tlk dialog_original.tlk \
  --modified-tlk dialog_modified.tlk
```

The combined SSF/TLK workflow always creates a complete package because `append.tlk` contains the actual strings and cannot be represented by an INI fragment alone. SSF-only package or fragment output remains available:

```sh
neossf-cli --diff-tslpatcher original.ssf modified.ssf tslpatchdata --package --filename my_soundset.ssf
neossf-cli --diff-tslpatcher original.ssf modified.ssf soundset_fragment.ini --fragment --filename my_soundset.ssf
```

Compatibility and safety rules:

- All 40 KotOR SSF slots are addressable, including the exact TSLPatcher keys `Unknown (29)` through `Unknown (40)`.
- NWN and NWN2 direct sound ResRef fields are not patchable through `[SSFList]`; distribute those SSFs as whole files.
- NeoSSF's combined exporter is append-only for TLK data. Existing TLK-row edits, deletions, and language-ID changes are rejected. Use NeoTLK's HoloPatcher package export for existing-entry replacements.
- The clean SSF is staged only when SSF slots changed, allowing the patcher to create the target file if the user has no existing Override copy.
- TLK entries added in the active tab but no longer referenced by an SSF slot are retained in `append.tlk` and reported as a warning.

Patcher generation accepts imported modified-side SSF data through `--modified-format csv|tsv|xml|json|ssf|kotor|native|auto` or `--diff-tslpatcher-import`. CSV/TSV use the flat slot table; XML/JSON use the semantic SSF formats.

Stock `[SSFList]` patch generation is limited to KotOR SSF V1.1. NWN/NWN2 SSFs retain full native and interchange editing support, but their direct sound ResRefs are not representable by the KotOR patcher handler.


## Continuous integration

GitHub Actions checks out `vrifftech/neoshared` beside this repository, then builds the full wxWidgets application on Ubuntu 24.04 and Windows Server 2025 with Visual Studio 2026. Successful non-pull-request runs publish staged Linux and Windows artifacts.

The shared dependency defaults to `neoshared/main`. Set the repository Actions variable `NEOSHARED_REF` to a release tag or commit SHA to pin normal CI builds. A manual workflow run can override the ref, and the workflow accepts the `neoshared-updated` repository-dispatch event for cross-repository compatibility checks.
