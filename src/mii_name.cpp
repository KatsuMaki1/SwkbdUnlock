#include "mii_name.h"
#include <coreinit/dynload.h>
#include <coreinit/title.h>
#include <function_patcher/function_patching.h>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// FFL (the Mii library) and nn::ngc (the profanity filter) are statically linked
// into Mii Maker, the Wii U Menu and games, each with its own copy of the
// "@ -> ???" name scrub. Both targets below are functions whose first words have
// no relocations, so the same instructions appear in every copy. We scan each
// loaded executable for them instead of using hardcoded offsets.
// Words equal to WILD are skipped when matching (relative branches).
static constexpr uint32_t WILD = 0xFFFFFFFF;

static uint64_t currentTitle = 0;
static std::vector<PatchedFunctionHandle> handles;
static bool installedForTitle = false;
static void (*scrubOriginal)(char16_t *, uint32_t) = nullptr;
static int (*normalizeOriginal)(void *, void *, int, const char16_t *) = nullptr;

// "@ -> ???" scrub: scan for U+0040 or U+FF20, then copy "???".
static void keepAtName(char16_t *name, uint32_t length) {
    if (shouldKeepMiiAt()) return;
    scrubOriginal(name, length);
}

// nn::ngc text normalizer (called only by the profanity check). The filter
// treats '@' as a substitution character and flags e.g. "@Katsu", so feed it
// the text without '@'. Output is capped at 511 units by the original anyway.
static int normalizeNoAt(void *out, void *flags, int max, const char16_t *text) {
    if (!shouldKeepMiiAt() || !text) return normalizeOriginal(out, flags, max, text);
    char16_t buf[512];
    unsigned j = 0;
    for (unsigned i = 0; i < 511 && text[i]; ++i)
        if (text[i] != 0x40 && text[i] != 0xFF20) buf[j++] = text[i];
    buf[j] = 0;
    return normalizeOriginal(out, flags, max, buf);
}

// Second normalizer, used by the array form of the profanity check: (out, max, text).
static int (*normalize2Original)(void *, int, const char16_t *) = nullptr;
static int normalize2NoAt(void *out, int max, const char16_t *text) {
    if (!shouldKeepMiiAt() || !text) return normalize2Original(out, max, text);
    char16_t buf[512];
    unsigned j = 0;
    for (unsigned i = 0; i < 511 && text[i]; ++i)
        if (text[i] != 0x40 && text[i] != 0xFF20) buf[j++] = text[i];
    buf[j] = 0;
    return normalize2Original(out, max, buf);
}

template <size_t N>
static void hookBySignature(const std::vector<OSDynLoad_NotifyData> &modules, const uint32_t (&sig)[N],
                            uint32_t replace, uint32_t *replaceCall) {
    for (const auto &module : modules) {
        if (!module.name || !module.textAddr) continue;
        const std::string_view full(module.name);
        if (!full.ends_with(".rpx")) continue;  // ponytail: RPLs skipped, add if a title ships FFL in one
        const auto *text = reinterpret_cast<const uint32_t *>(module.textAddr);
        const uint32_t words = module.textSize / 4;
        for (uint32_t i = 0; i + N <= words; ++i) {
            bool ok = true;
            for (size_t k = 0; k < N && ok; ++k) ok = sig[k] == WILD || text[i + k] == sig[k];
            if (!ok) continue;
            if (FunctionPatcher_InitLibrary() != FUNCTION_PATCHER_RESULT_SUCCESS) return;
            const std::string exe(full.substr(full.find_last_of('/') + 1));
            function_replacement_data_t patch = {};
            patch.version = FUNCTION_REPLACEMENT_DATA_STRUCT_VERSION;
            patch.type = FUNCTION_PATCHER_REPLACE_FOR_EXECUTABLE_BY_ADDRESS;
            patch.replaceAddr = replace;
            patch.replaceCall = replaceCall;
            patch.targetProcess = FP_TARGET_PROCESS_ALL;
            patch.ReplaceInRPX.targetTitleIds = &currentTitle;
            patch.ReplaceInRPX.targetTitleIdsCount = 1;
            patch.ReplaceInRPX.versionMin = 0;
            patch.ReplaceInRPX.versionMax = 0xFFFF;
            patch.ReplaceInRPX.executableName = exe.c_str();
            patch.ReplaceInRPX.textOffset = i * 4;
            PatchedFunctionHandle h = 0;
            bool active = false;
            const auto result = FunctionPatcher_AddFunctionPatch(&patch, &h, &active);
            if (h) {
                if (result == FUNCTION_PATCHER_RESULT_SUCCESS && active) handles.push_back(h);
                else FunctionPatcher_RemoveFunctionPatch(h);
            }
            return;
        }
    }
}

void installMiiNamePatch() {
    if (installedForTitle) return;
    const int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0 || count > 512) return;
    std::vector<OSDynLoad_NotifyData> modules(count);
    if (!OSDynLoad_GetRPLInfo(0, count, modules.data())) return;
    currentTitle = OSGetTitleID();
    installedForTitle = true;

    constexpr uint32_t scrub[] = {
        0x39400000, 0x7C0A2040, 0x4C800020, 0x554C083C, 0x7C0C1A2E,
        0x2C000040, 0x4182001C, 0x2800FF20, 0x41820014, 0x394A0001,
        0x7C0A2040, 0x4180FFE0, 0x4E800020};
    hookBySignature(modules, scrub, reinterpret_cast<uint32_t>(&keepAtName),
                    reinterpret_cast<uint32_t *>(&scrubOriginal));

    // Start of nn::ngc normalizer (words 10 and 14 are relative `bl`).
    constexpr uint32_t normalize[] = {
        0x9421FFE0, 0x7C0802A6, 0xBF410008, 0x7C7C1B79, 0x7CBE2B78, 0x7CDF3378,
        0x90010024, 0x7C9D2378, 0x4082000C, 0x38600001, WILD, 0x2C1D0000,
        0x4082000C, 0x38600001, WILD, 0x2C1F0000};
    hookBySignature(modules, normalize, reinterpret_cast<uint32_t>(&normalizeNoAt),
                    reinterpret_cast<uint32_t *>(&normalizeOriginal));

    // Start of the second normalizer (words 9 and 13 are relative `bl`).
    constexpr uint32_t normalize2[] = {
        0x9421FFE0, 0x7C0802A6, 0xBF410008, 0x7C9E2378, 0x7CBF2B78, 0x90010024,
        0x7C7D1B79, 0x4082000C, 0x38600001, WILD, 0x2C1F0000, 0x4082000C,
        0x38600001, WILD, 0x3B600000};
    hookBySignature(modules, normalize2, reinterpret_cast<uint32_t>(&normalize2NoAt),
                    reinterpret_cast<uint32_t *>(&normalize2Original));
}

void removeMiiNamePatch() {
    for (auto h : handles) FunctionPatcher_RemoveFunctionPatch(h);
    handles.clear();
    installedForTitle = false;
}
