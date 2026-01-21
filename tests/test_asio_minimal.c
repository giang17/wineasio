/* Minimal ASIO Test Program
 * 
 * Purpose: Load WineASIO 32-bit and call Init() to trigger vtable dump
 * Usage: Compile with MinGW 32-bit and run under Wine
 * 
 * Compile:
 *   i686-w64-mingw32-gcc -o test_asio_minimal.exe test_asio_minimal.c -lole32 -luuid
 * 
 * Run:
 *   WINEDEBUG=warn+wineasio wine test_asio_minimal.exe
 */

#include <windows.h>
#include <stdio.h>
#include <ole2.h>

/* WineASIO CLSID: {48D0C522-BFCC-45CC-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = {
    0x48d0c522, 0xbfcc, 0x45cc,
    {0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8}
};

/* IID_IUnknown */
static const GUID IID_IUnknown_Local = {
    0x00000000, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

/* Minimal ASIO interface definition */
typedef struct IWineASIO IWineASIO;

typedef struct IWineASIOVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    
    /* IASIO */
    LONG (STDMETHODCALLTYPE *Init)(IWineASIO *This, void *sysRef);
    void (STDMETHODCALLTYPE *GetDriverName)(IWineASIO *This, char *name);
    LONG (STDMETHODCALLTYPE *GetDriverVersion)(IWineASIO *This);
    void (STDMETHODCALLTYPE *GetErrorMessage)(IWineASIO *This, char *string);
    LONG (STDMETHODCALLTYPE *Start)(IWineASIO *This);
    LONG (STDMETHODCALLTYPE *Stop)(IWineASIO *This);
    LONG (STDMETHODCALLTYPE *GetChannels)(IWineASIO *This, LONG *numInputChannels, LONG *numOutputChannels);
    /* ... rest of vtable not needed for basic test ... */
} IWineASIOVtbl;

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
};

int main(int argc, char *argv[])
{
    HRESULT hr;
    IWineASIO *pAsio = NULL;
    char driverName[256] = {0};
    LONG version;
    LONG initResult;
    LONG numInputs = 0, numOutputs = 0;
    
    printf("===========================================\n");
    printf("WineASIO 32-bit Minimal Test\n");
    printf("===========================================\n\n");
    
    /* Initialize COM */
    printf("1. Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("   ERROR: CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("   OK: COM initialized\n\n");
    
    /* Create WineASIO instance */
    printf("2. Creating WineASIO instance...\n");
    printf("   CLSID: {48D0C522-BFCC-45CC-8B84-17F25F33E6E8}\n");
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown_Local, (void**)&pAsio);
    
    if (FAILED(hr)) {
        printf("   ERROR: CoCreateInstance failed: 0x%08lx\n", hr);
        printf("   Is WineASIO registered? Run: wine regsvr32 wineasio.dll\n");
        CoUninitialize();
        return 1;
    }
    
    if (pAsio == NULL) {
        printf("   ERROR: pAsio is NULL but HRESULT was success\n");
        CoUninitialize();
        return 1;
    }
    
    printf("   OK: WineASIO instance created at %p\n", pAsio);
    printf("   vtable pointer (lpVtbl): %p\n\n", pAsio->lpVtbl);
    
    /* At this point, vtable dump should be printed by WineASIO */
    printf("   >>> Check Wine debug output for '=== VTABLE DUMP ===' <<<\n\n");
    
    /* Get driver name */
    printf("3. Calling GetDriverName()...\n");
    if (pAsio->lpVtbl->GetDriverName) {
        pAsio->lpVtbl->GetDriverName(pAsio, driverName);
        printf("   Driver name: %s\n\n", driverName);
    } else {
        printf("   ERROR: GetDriverName is NULL!\n\n");
    }
    
    /* Get driver version */
    printf("4. Calling GetDriverVersion()...\n");
    if (pAsio->lpVtbl->GetDriverVersion) {
        version = pAsio->lpVtbl->GetDriverVersion(pAsio);
        printf("   Driver version: %ld (0x%lx)\n\n", version, version);
    } else {
        printf("   ERROR: GetDriverVersion is NULL!\n\n");
    }
    
    /* Initialize ASIO */
    printf("5. Calling Init(NULL)...\n");
    if (pAsio->lpVtbl->Init) {
        initResult = pAsio->lpVtbl->Init(pAsio, NULL);
        if (initResult) {
            printf("   OK: Init succeeded (returned %ld)\n\n", initResult);
        } else {
            printf("   ERROR: Init failed (returned 0)\n");
            printf("   Is JACK running? Start with: jackdbus auto\n\n");
        }
    } else {
        printf("   ERROR: Init is NULL!\n\n");
    }
    
    /* Get channels */
    printf("6. Calling GetChannels()...\n");
    if (pAsio->lpVtbl->GetChannels) {
        LONG result = pAsio->lpVtbl->GetChannels(pAsio, &numInputs, &numOutputs);
        if (result == 0) {  /* ASE_OK = 0 */
            printf("   OK: Inputs=%ld, Outputs=%ld\n\n", numInputs, numOutputs);
        } else {
            printf("   GetChannels returned: %ld\n\n", result);
        }
    } else {
        printf("   ERROR: GetChannels is NULL!\n\n");
    }
    
    /* Release instance */
    printf("7. Releasing WineASIO instance...\n");
    if (pAsio->lpVtbl->Release) {
        ULONG refCount = pAsio->lpVtbl->Release(pAsio);
        printf("   OK: Released (refcount=%lu)\n\n", refCount);
    } else {
        printf("   ERROR: Release is NULL!\n\n");
    }
    
    /* Cleanup */
    printf("8. Cleaning up...\n");
    CoUninitialize();
    printf("   OK: COM uninitialized\n\n");
    
    printf("===========================================\n");
    printf("Test completed successfully!\n");
    printf("===========================================\n");
    printf("\nIf crash occurred, check Wine debug output for:\n");
    printf("  - '=== VTABLE DUMP ===' - shows function pointers\n");
    printf("  - '>>> CALLED:' - shows which function was entered\n");
    printf("  - 'page fault' - shows crash address\n");
    
    return 0;
}