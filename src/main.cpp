#include <wups.h>
#include "mii_name.h"
#include <coreinit/title.h>
#include <coreinit/dynload.h>
#include <coreinit/memorymap.h>
#include <function_patcher/function_patching.h>
#include <nn/swkbd.h>
#include <wups/config/WUPSConfigCategory.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <cstring>
#include <string_view>
#include <vector>

WUPS_PLUGIN_NAME("Swkbd Unlock");
WUPS_PLUGIN_DESCRIPTION("Unlock swkbd symbols and optionally remove its input length limit");
WUPS_PLUGIN_VERSION("1.0.0");
WUPS_PLUGIN_AUTHOR("Katsu");
WUPS_PLUGIN_LICENSE("MIT");
WUPS_USE_STORAGE("swkbd_unlock_v2");

static bool symbols = true;
static bool unlimited = false;
static bool splatoonOnly = false;
static bool keepMiiAt = true;
static PatchedFunctionHandle deepHandle = 0;
static uint64_t deepTitle = 0;
static void (*realBuildRestrictions)(void *, const nn::swkbd::ConfigArg &);
static void ensureDeepPatch();

static bool applies() {
    if (!splatoonOnly) return true;
    const uint64_t id = OSGetTitleID();
    return id == 0x0005000010176900ULL || id == 0x0005000010176A00ULL || id == 0x0005000010162B00ULL;
}
bool shouldKeepMiiAt() { return symbols && keepMiiAt && applies(); }

static void changed(ConfigItemBoolean *item, bool value) {
    const std::string_view key(item->identifier);
    if (key == "symbols") symbols = value;
    else if (key == "unlimited") unlimited = value;
    else if (key == "splatoonOnly") splatoonOnly = value;
    else if (key == "keepMiiAt") keepMiiAt = value;
    WUPSStorageAPI::Store(item->identifier, value);
}

static WUPSConfigAPICallbackStatus opened(WUPSConfigCategoryHandle handle) {
    try {
        WUPSConfigCategory root(handle);
        root.add(WUPSConfigItemBoolean::Create("symbols", "Unlock symbols", true, symbols, changed));
        root.add(WUPSConfigItemBoolean::Create("keepMiiAt", "Keep @ in Mii names (PAL v50)", true, keepMiiAt, changed));
        root.add(WUPSConfigItemBoolean::Create("unlimited", "Unlimited keyboard length (experimental)", false, unlimited, changed));
        root.add(WUPSConfigItemBoolean::Create("splatoonOnly", "Splatoon only", false, splatoonOnly, changed));
        root.add(WUPSConfigItemStub::Create("Reopen keyboard after changing settings."));
        root.add(WUPSConfigItemStub::Create("Game storage and server limits still apply."));
        return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
    } catch (...) {
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }
}
static void closed() { WUPSStorageAPI::SaveStorage(); }

INITIALIZE_PLUGIN() {
    WUPSStorageAPI::GetOrStoreDefault("symbols", symbols, true);
    WUPSStorageAPI::GetOrStoreDefault("keepMiiAt", keepMiiAt, true);
    WUPSStorageAPI::GetOrStoreDefault("unlimited", unlimited, false);
    WUPSStorageAPI::GetOrStoreDefault("splatoonOnly", splatoonOnly, false);
    WUPSStorageAPI::SaveStorage();
    WUPSConfigAPIOptionsV1 options = {.name = "Swkbd Unlock"};
    WUPSConfigAPI_Init(options, opened, closed);
}

static void unlock(nn::swkbd::ConfigArg &arg) {
    arg.keyboardMode = nn::swkbd::KeyboardMode::Full;
    arg.accessFlags |= 0x7FFFF;
    // This RPL reads a 32-bit restriction mask at 0x28. Bit 2 blocks '@':
    // 020816b0 lwz r0,0x28(r27); 020816b4 test bit 2;
    // 02081734 li r7,0x40; 0208173c append to prohibited-character list.
    // WUT currently exposes these four bytes as unknown bytes / disableNewLine.
    const uint32_t noRestrictions = 0;
    std::memcpy(reinterpret_cast<unsigned char *>(&arg) + 0x28, &noRestrictions, 4);
}

// Verified against the provided RPL: the restriction builder loads this
// instance-owned UTF-16 buffer at 020816a0, before constructing its blacklist.
static void myBuildRestrictions(void *instance, const nn::swkbd::ConfigArg &args) {
    realBuildRestrictions(instance, args);
    if (!symbols || !applies()) return;
    auto slot = reinterpret_cast<char16_t **>(static_cast<unsigned char *>(instance) + 0x56128);
    if (!OSIsAddressValid(reinterpret_cast<uint32_t>(slot))) return;
    auto buffer = *slot;
    if (!buffer || !OSIsAddressValid(reinterpret_cast<uint32_t>(buffer))) return;
    buffer[0] = 0;
}

static void removeDeepPatch() {
    if (deepHandle) {
        FunctionPatcher_RemoveFunctionPatch(deepHandle);
        deepHandle = 0;
    }
}

static void ensureDeepPatch() {
    if (deepHandle || !symbols || !applies()) return;
    const int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0 || count > 512) return;
    std::vector<OSDynLoad_NotifyData> modules(count);
    if (!OSDynLoad_GetRPLInfo(0, count, modules.data())) return;
    for (const auto &module : modules) {
        if (!module.name || !std::string_view(module.name).ends_with("swkbd.rpl")) continue;
        constexpr uint32_t offset = 0x81680;
        constexpr uint32_t expected[] = {0x9421FFD0, 0xBEE1000C, 0x3FC00005, 0x3BDE2E04,
                                        0x7C7F1B78, 0x7C0802A6, 0x90010034, 0x7FBFF214,
                                        0x853D3324, 0x3B800000, 0x7C9B2378, 0xB3890000,
                                        0x801B0028, 0x540A077B};
        if (module.textSize < offset + sizeof(expected)) return;
        const auto address = reinterpret_cast<const void *>(module.textAddr + offset);
        if (std::memcmp(address, expected, sizeof(expected)) != 0) return;
        if (FunctionPatcher_InitLibrary() != FUNCTION_PATCHER_RESULT_SUCCESS) return;
        deepTitle = OSGetTitleID();
        function_replacement_data_t patch = {};
        patch.version = FUNCTION_REPLACEMENT_DATA_STRUCT_VERSION;
        patch.type = FUNCTION_PATCHER_REPLACE_FOR_EXECUTABLE_BY_ADDRESS;
        patch.replaceAddr = reinterpret_cast<uint32_t>(&myBuildRestrictions);
        patch.replaceCall = reinterpret_cast<uint32_t *>(&realBuildRestrictions);
        patch.targetProcess = FP_TARGET_PROCESS_ALL;
        patch.ReplaceInRPX.targetTitleIds = &deepTitle;
        patch.ReplaceInRPX.targetTitleIdsCount = 1;
        patch.ReplaceInRPX.versionMin = 0;
        patch.ReplaceInRPX.versionMax = 0xFFFF;
        patch.ReplaceInRPX.executableName = "swkbd.rpl";
        patch.ReplaceInRPX.textOffset = offset;
        bool active = false;
        const auto status = FunctionPatcher_AddFunctionPatch(&patch, &deepHandle, &active);
        if (status != FUNCTION_PATCHER_RESULT_SUCCESS || !active) {
            removeDeepPatch();
            return;
        }
        return;
    }
}

ON_APPLICATION_START() { installMiiNamePatch(); }
ON_APPLICATION_ENDS() { removeMiiNamePatch(); removeDeepPatch(); }
DEINITIALIZE_PLUGIN() { removeMiiNamePatch(); removeDeepPatch(); }

DECL_FUNCTION(bool, AppearInputForm, const nn::swkbd::AppearArg &args) {
    installMiiNamePatch();
    ensureDeepPatch();
    if (!applies() || (!symbols && !unlimited)) return real_AppearInputForm(args);
    auto patched = args;
    if (symbols) unlock(patched.keyboardArg.configArg);
    if (unlimited) patched.inputFormArg.maxTextLength = -1;
    return real_AppearInputForm(patched);
}

DECL_FUNCTION(bool, AppearKeyboard, const nn::swkbd::KeyboardArg &args) {
    installMiiNamePatch();
    ensureDeepPatch();
    if (!applies() || !symbols) return real_AppearKeyboard(args);
    auto patched = args;
    unlock(patched.configArg);
    return real_AppearKeyboard(patched);
}

WUPS_MUST_REPLACE_FOR_PROCESS(AppearInputForm, WUPS_LOADER_LIBRARY_SWKBD, SwkbdAppearInputForm__3RplFRCQ3_2nn5swkbd9AppearArg, WUPS_FP_TARGET_PROCESS_ALL);
WUPS_MUST_REPLACE_FOR_PROCESS(AppearKeyboard, WUPS_LOADER_LIBRARY_SWKBD, SwkbdAppearKeyboard__3RplFRCQ3_2nn5swkbd11KeyboardArg, WUPS_FP_TARGET_PROCESS_ALL);


