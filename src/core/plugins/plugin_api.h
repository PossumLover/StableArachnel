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
 * the virtuals sit. 86b028f moved `updateMayBreakDlc` from just after
 * `detectUpdate` to the end of the class without changing
 * ARACHNEL_PLUGIN_API_VERSION, which shifted `launchInfo` and every slot after it
 * by one for plugins built before that commit. The host then called
 * `launchInfo(LibraryGame&) -> LaunchInfo` and reached
 * `updateMayBreakDlc(LibraryGame&, CatalogEntry&) -> bool` instead: a hidden sret
 * pointer left unwritten and an argument read out of the wrong register. That is
 * issue #64 - "crash upon updating or launching a game" - and the
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

} // extern "C"
