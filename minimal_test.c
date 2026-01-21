/*
 * Minimal WineASIO test DLL
 * This is a bare-bones COM DLL to test if the crash is in WineASIO or MinGW/Wine
 */

#include <windows.h>
#include <objbase.h>

/* ASIO CLSID - must match what's registered */
/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = {
    0x48d0c522, 0xbfcc, 0x45cc, {0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8}
};

/* Simple debug output */
static void debug_msg(const char *msg)
{
    OutputDebugStringA("[MinimalASIO] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hInstDLL;
    (void)lpvReserved;
    
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        debug_msg("DllMain: DLL_PROCESS_ATTACH");
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        debug_msg("DllMain: DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    debug_msg("DllGetClassObject called");
    
    (void)rclsid;
    (void)riid;
    
    if (ppv)
        *ppv = NULL;
    
    /* We don't actually implement a class factory, just return not available */
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    debug_msg("DllCanUnloadNow called");
    return S_FALSE;  /* Don't unload */
}

HRESULT WINAPI DllRegisterServer(void)
{
    debug_msg("DllRegisterServer called");
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    debug_msg("DllUnregisterServer called");
    return S_OK;
}