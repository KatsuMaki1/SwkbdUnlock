#include "mii_name.h"
#include <coreinit/dynload.h>
#include <coreinit/title.h>
#include <function_patcher/function_patching.h>
#include <cstring>
#include <string_view>
#include <vector>

static constexpr uint64_t miiTitle = 0x000500101004A200ULL;
static PatchedFunctionHandle handle = 0;
static void (*original)(char16_t *, uint32_t) = nullptr;

// ffl_app.rpx 021cd568 scans for U+0040 or U+FF20 then copies "???".
// The callers ignore its return value. Preserve all original behavior when off.
static void keepAtName(char16_t *name, uint32_t length) {
    if (shouldKeepMiiAt()) return;
    original(name, length);
}

void installMiiNamePatch() {
    if (handle || OSGetTitleID() != miiTitle) return;
    const int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0 || count > 512) return;
    std::vector<OSDynLoad_NotifyData> modules(count);
    if (!OSDynLoad_GetRPLInfo(0, count, modules.data())) return;
    constexpr uint32_t expected[] = {
        0x39400000, 0x7C0A2040, 0x4C800020, 0x554C083C, 0x7C0C1A2E,
        0x2C000040, 0x4182001C, 0x2800FF20, 0x41820014, 0x394A0001,
        0x7C0A2040, 0x4180FFE0, 0x4E800020};
    for (const auto &module : modules) {
        if (!module.name || !std::string_view(module.name).ends_with("ffl_app.rpx")) continue;
        // Text starts at 02000020 in the ELF; loaders may include the syscall
        // section in textAddr. Both candidate bases require the full signature.
        for (const uint32_t offset : {0x1CD548u, 0x1CD568u}) {
            if (module.textSize < offset + sizeof(expected)) continue;
            if (std::memcmp(reinterpret_cast<void *>(module.textAddr + offset), expected, sizeof(expected))) continue;
            if (FunctionPatcher_InitLibrary() != FUNCTION_PATCHER_RESULT_SUCCESS) return;
            function_replacement_data_t patch = {};
            patch.version = FUNCTION_REPLACEMENT_DATA_STRUCT_VERSION;
            patch.type = FUNCTION_PATCHER_REPLACE_FOR_EXECUTABLE_BY_ADDRESS;
            patch.replaceAddr = reinterpret_cast<uint32_t>(&keepAtName);
            patch.replaceCall = reinterpret_cast<uint32_t *>(&original);
            patch.targetProcess = FP_TARGET_PROCESS_ALL;
            patch.ReplaceInRPX.targetTitleIds = &miiTitle;
            patch.ReplaceInRPX.targetTitleIdsCount = 1;
            patch.ReplaceInRPX.versionMin = 50;
            patch.ReplaceInRPX.versionMax = 50;
            patch.ReplaceInRPX.executableName = "ffl_app.rpx";
            patch.ReplaceInRPX.textOffset = offset;
            bool active = false;
            const auto result = FunctionPatcher_AddFunctionPatch(&patch, &handle, &active);
            if (result != FUNCTION_PATCHER_RESULT_SUCCESS || !active) {
                removeMiiNamePatch();
                return;
            }
            return;
        }
        return;
    }
}

void removeMiiNamePatch() {
    if (handle) {
        FunctionPatcher_RemoveFunctionPatch(handle);
        handle = 0;
    }
}

