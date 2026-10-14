#pragma once

#include "plugin_interface.h"

#include <cstddef>

#if defined(_WIN32)
#  if defined(ARACHNEL_PLUGIN_BUILD)
#    define ARACHNEL_PLUGIN_EXPORT __declspec(dllexport)
#  else
#    define ARACHNEL_PLUGIN_EXPORT __declspec(dllimport)
#  endif
#else
#  define ARACHNEL_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/**
 * Host speaks API 4 (JSON catalog boundary).
 * Plugins with apiVersion 2/3 still load (CatalogEntry ABI + sizeof gate).
 * API 4 plugins export arachnel_plugin_catalog_json / _free. CatalogEntry sizeof
 * is optional: match → host may call entryById / detectUpdate; mismatch → load
 * anyway, those DLL-crossing calls stay skipped.
 */
#define ARACHNEL_PLUGIN_API_VERSION 4
#define ARACHNEL_PLUGIN_API_VERSION_MIN 2

/**
 * Layout revision of ISourcePlugin's vtable.
 *
 * apiVersion says which *protocol* a plugin speaks; it says nothing about where
 * the virtuals sit. Upstream moved `updateMayBreakDlc` from just after
 * `detectUpdate` to the end of the class in 86b028f without changing
 * ARACHNEL_PLUGIN_API_VERSION, which shifted `launchInfo` and every slot after it
 * by one for plugins built before that commit. The host then called
 * `launchInfo(LibraryGame&) -> LaunchInfo` and reached
 * `updateMayBreakDlc(LibraryGame&, CatalogEntry&) -> bool` instead: a hidden sret
 * pointer left unwritten and an argument read out of the wrong register. That is
 * upstream issue #64 - "crash upon updating or launching a game" - and the
 * CatalogEntry size mismatch reported alongside it was only a co-traveller from
 * the same commit.
 *
 * Bump this on ANY change to the virtual layout: adding, removing, reordering, or
 * changing a signature. Appending at the very end is the one safe edit, and it
 * still needs a bump so the host knows which slots exist.
 *
 *   1 = before 86b028f (updateMayBreakDlc between detectUpdate and launchInfo)
 *   2 = current (updateMayBreakDlc and launchOptions appended at the end)
 */
#define ARACHNEL_PLUGIN_INTERFACE_REVISION 2
#define ARACHNEL_PLUGIN_INTERFACE_REVISION_MIN 1

/** sizeof() every struct that crosses the boundary, so drift names itself. */
struct ArachnelAbiSizes {
    unsigned int abiSizesStruct;
    unsigned int interfaceRevision;
    unsigned int catalogEntry;
    unsigned int catalogComponent;
    unsigned int libraryGame;
    unsigned int installContext;
    unsigned int addonInstallContext;
    unsigned int installResult;
    unsigned int installAnalysis;
    unsigned int launchInfo;
    unsigned int gameLaunchOption;
    unsigned int ownedDownloadProgress;
};

/** Filled from the SDK headers the caller compiled against. */
inline void arachnel_fill_abi_sizes(ArachnelAbiSizes* out)
{
    using namespace arachnel::core;
    out->abiSizesStruct = static_cast<unsigned int>(sizeof(ArachnelAbiSizes));
    out->interfaceRevision = ARACHNEL_PLUGIN_INTERFACE_REVISION;
    out->catalogEntry = static_cast<unsigned int>(sizeof(CatalogEntry));
    out->catalogComponent = static_cast<unsigned int>(sizeof(CatalogComponent));
    out->libraryGame = static_cast<unsigned int>(sizeof(LibraryGame));
    out->installContext = static_cast<unsigned int>(sizeof(InstallContext));
    out->addonInstallContext = static_cast<unsigned int>(sizeof(AddonInstallContext));
    out->installResult = static_cast<unsigned int>(sizeof(InstallResult));
    out->installAnalysis = static_cast<unsigned int>(sizeof(InstallAnalysis));
    out->launchInfo = static_cast<unsigned int>(sizeof(LaunchInfo));
    out->gameLaunchOption = static_cast<unsigned int>(sizeof(GameLaunchOption));
    out->ownedDownloadProgress = static_cast<unsigned int>(sizeof(OwnedDownloadProgress));
}

extern "C" {

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_api_version();

/** ABI canary: sizeof(CatalogEntry). Required for API 2/3. Optional for API 4. */
ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_catalog_entry_size();

/**
 * Vtable layout revision this plugin was built against.
 * Optional: a plugin that does not export it is assumed to predate the export,
 * and the host infers the revision from sizeof(CatalogEntry).
 */
ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_interface_revision();

/**
 * Sizes of every shared struct, so a mismatch can name the struct that drifted
 * instead of failing somewhere in the middle of a call. Optional.
 */
ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_abi_sizes(ArachnelAbiSizes* out);

ARACHNEL_PLUGIN_EXPORT arachnel::core::ISourcePlugin* arachnel_plugin_create(
    const char* plugin_root_utf8);

ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_destroy(arachnel::core::ISourcePlugin* plugin);

/**
 * API 4: serialize plugin->catalog() to UTF-8 JSON.
 * Caller must free *out_utf8 with arachnel_plugin_catalog_json_free.
 * Returns 0 on success.
 */
ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_catalog_json(arachnel::core::ISourcePlugin* plugin,
                                                       char** out_utf8, size_t* out_len);

ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_catalog_json_free(char* p);

/*
 * Catalog source extension (optional, additive).
 *
 * The plugin tells the host where its raw catalog lives and how to turn one batch of raw rows
 * into catalog entries; the host does the rest (HTTP with ETag/gzip, TTL, on-disk cache,
 * streaming row scan, binary snapshot). A plugin no longer has to download, parse and re-serialize
 * the whole catalog itself.
 *
 * Compatibility rules - read before changing anything here:
 *  - These exports are OPTIONAL. ARACHNEL_PLUGIN_API_VERSION is NOT bumped for them and a
 *    plugin.json apiVersion/minArachnel must not be raised because of them. A host that does not
 *    know them never looks them up, so a plugin that exports them still loads and works on any
 *    older Arachnel through arachnel_plugin_catalog_json.
 *  - A plugin that exports them MUST keep arachnel_plugin_catalog_json fully working. The host
 *    falls back to it whenever the source path is missing, reports an unsupported version, fails,
 *    or yields too few rows.
 *  - The host only uses the extension when arachnel_plugin_source_ext_version() returns a value
 *    in [1, ARACHNEL_PLUGIN_SOURCE_EXT_VERSION]. New fields are only ever added to the JSON
 *    below (readers ignore unknown keys); anything incompatible gets a new ext version.
 *  - Only plain C types and UTF-8 JSON cross the boundary; memory is freed by the side that
 *    allocated it (arachnel_plugin_source_free).
 *
 * arachnel_plugin_catalog_source -> JSON descriptor, e.g.
 *   {"url":"https://host/catalog.json",   // required, http(s)
 *    "rowsKeys":["entries","games"],      // top-level array holding the rows; [] = top-level array
 *    "ttlSeconds":300,                    // reuse the cached raw feed this long without asking
 *    "minRows":10,                        // fewer kept rows = treat as a failed sync
 *    "parallel":true,                     // normalize_rows is thread-safe and may run concurrently
 *    "headers":{"X-Token":"..."},         // optional extra request headers
 *    "userAgent":"..."}                   // optional
 *
 * arachnel_plugin_normalize_rows: input is a UTF-8 JSON array of raw rows; output is
 *   {"schema":"arachnel.plugin.catalog.v1","entries":[...],"supersedes":["id",...]}
 * `entries` uses the same entry objects as arachnel_plugin_catalog_json. Rows the plugin does not
 * want are simply left out. `supersedes` lists entry ids that must not appear in the final
 * catalog because another row makes them redundant (e.g. a DLC row listed by its parent game);
 * the host applies it across all batches. Returns 0 on success.
 */
#define ARACHNEL_PLUGIN_SOURCE_EXT_VERSION 1

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_source_ext_version();

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_catalog_source(arachnel::core::ISourcePlugin* plugin,
                                                         char** out_json, size_t* out_len);

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_normalize_rows(arachnel::core::ISourcePlugin* plugin,
                                                         const char* rows_json, size_t rows_len,
                                                         char** out_json, size_t* out_len);

/** Frees buffers returned by arachnel_plugin_catalog_source / arachnel_plugin_normalize_rows. */
ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_source_free(char* p);

} // extern "C"
