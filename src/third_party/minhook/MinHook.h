#pragma once

/* Minimal MinHook public header adapted for embedding into this demo.
   This is a trimmed-down declaration surface sufficient for our usage.
   For full MinHook features, use the upstream project: https://github.com/TsudaKageyu/minhook
*/

#ifndef MH_MINHOOK_H
#define MH_MINHOOK_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN64
typedef unsigned long long MH_STATUS;
#else
typedef unsigned long MH_STATUS;
#endif

#define MH_OK 0

// Initialize/Uninitialize MinHook
MH_STATUS MH_Initialize(void);
MH_STATUS MH_Uninitialize(void);

// Create/Remove/Enable/Disable hooks
MH_STATUS MH_CreateHook(void *pTarget, void *pDetour, void **ppOriginal);
MH_STATUS MH_RemoveHook(void *pTarget);
MH_STATUS MH_EnableHook(void *pTarget);
MH_STATUS MH_DisableHook(void *pTarget);

#ifdef __cplusplus
}
#endif

#endif // MH_MINHOOK_H
