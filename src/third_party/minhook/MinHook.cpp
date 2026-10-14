#include "StdAfx.h"
#include <windows.h>
#include <vector>
#include <mutex>

struct HookEntry
{
    void* target = nullptr;
    void* detour = nullptr;
    void** original = nullptr;
    bool enabled = false;
};

static std::vector<HookEntry> g_hookEntries;
static std::mutex g_hookMutex;

MH_STATUS MH_Initialize(void)
{
    return MH_OK;
}

MH_STATUS MH_Uninitialize(void)
{
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_hookEntries.clear();
    return MH_OK;
}

MH_STATUS MH_CreateHook(void *pTarget, void *pDetour, void **ppOriginal)
{
    if (!pTarget || !pDetour) {
        return 1;
    }

    std::lock_guard<std::mutex> lock(g_hookMutex);
    for (const auto& entry : g_hookEntries) {
        if (entry.target == pTarget) {
            if (ppOriginal) *ppOriginal = pTarget;
            return MH_OK;
        }
    }

    HookEntry entry;
    entry.target = pTarget;
    entry.detour = pDetour;
    entry.original = ppOriginal;
    entry.enabled = false;
    if (ppOriginal) *ppOriginal = pTarget;
    g_hookEntries.push_back(entry);
    return MH_OK;
}

MH_STATUS MH_RemoveHook(void *pTarget)
{
    std::lock_guard<std::mutex> lock(g_hookMutex);
    for (auto it = g_hookEntries.begin(); it != g_hookEntries.end(); ++it) {
        if (it->target == pTarget) {
            g_hookEntries.erase(it);
            return MH_OK;
        }
    }
    return MH_OK;
}

MH_STATUS MH_EnableHook(void *pTarget)
{
    std::lock_guard<std::mutex> lock(g_hookMutex);
    for (auto& entry : g_hookEntries) {
        if (entry.target == pTarget) {
            entry.enabled = true;
            return MH_OK;
        }
    }
    return MH_OK;
}

MH_STATUS MH_DisableHook(void *pTarget)
{
    std::lock_guard<std::mutex> lock(g_hookMutex);
    for (auto& entry : g_hookEntries) {
        if (entry.target == pTarget) {
            entry.enabled = false;
            return MH_OK;
        }
    }
    return MH_OK;
}
