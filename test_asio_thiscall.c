/* ASIO Test Program with proper thiscall calling convention
 * 
 * Purpose: Test WineASIO 32-bit using the correct thiscall calling convention
 *          that real ASIO hosts (REAPER, Cubase, FL Studio) use.
 * 
 * On 32-bit Windows, ASIO methods use __thiscall convention where:
 * - The 'this' pointer is passed in ECX register
 * - Other arguments are pushed on the stack right-to-left
 * - The callee cleans up the stack
 * 
 * Compile:
 *   i686-w64-mingw32-gcc -o test_asio_thiscall.exe test_asio_thiscall.c -lole32 -luuid
 * 
 * Run:
 *   WINEDEBUG=warn+wineasio wine test_asio_thiscall.exe
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

/* Forward declaration */
typedef struct IWineASIO IWineASIO;

/*
 * Thiscall wrappers for calling ASIO methods
 * 
 * Since MinGW doesn't support __thiscall directly, we use inline assembly
 * to make calls with the 'this' pointer in ECX register.
 */

/* Call a method with no extra args: long method(this) */
static __inline__ LONG call_thiscall_0(void *func, IWineASIO *pThis)
{
    LONG result;
    __asm__ __volatile__ (
        "movl %1, %%ecx\n\t"
        "call *%2\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

/* Call a method with one pointer arg: void method(this, char *arg) */
static __inline__ void call_thiscall_1_ptr(void *func, IWineASIO *pThis, void *arg)
{
    __asm__ __volatile__ (
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%0\n\t"
        :
        : "r" (func), "r" (pThis), "r" (arg)
        : "eax", "ecx", "edx", "memory"
    );
}

/* Call Init: long Init(this, void *sysRef) */
static __inline__ LONG call_thiscall_init(void *func, IWineASIO *pThis, void *sysRef)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%3\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (sysRef), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

/* Call GetChannels: long GetChannels(this, long *in, long *out) */
static __inline__ LONG call_thiscall_getchannels(void *func, IWineASIO *pThis, LONG *pIn, LONG *pOut)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %3\n\t"
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%4\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (pIn), "r" (pOut), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

/* ASIO vtable structure - same layout as IWineASIOVtbl */
typedef struct IWineASIOVtbl {
    /* IUnknown - use stdcall */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    
    /* IASIO - use thiscall (these are raw function pointers) */
    void *Init;              /* LONG Init(void *sysRef) */
    void *GetDriverName;     /* void GetDriverName(char *name) */
    void *GetDriverVersion;  /* LONG GetDriverVersion() */
    void *GetErrorMessage;   /* void GetErrorMessage(char *string) */
    void *Start;             /* LONG Start() */
    void *Stop;              /* LONG Stop() */
    void *GetChannels;       /* LONG GetChannels(LONG *in, LONG *out) */
    void *GetLatencies;      /* LONG GetLatencies(LONG *in, LONG *out) */
    void *GetBufferSize;     /* LONG GetBufferSize(LONG *min, LONG *max, LONG *pref, LONG *gran) */
    void *CanSampleRate;     /* LONG CanSampleRate(double rate) */
    void *GetSampleRate;     /* LONG GetSampleRate(double *rate) */
    void *SetSampleRate;     /* LONG SetSampleRate(double rate) */
    void *GetClockSources;   /* LONG GetClockSources(void *clocks, LONG *num) */
    void *SetClockSource;    /* LONG SetClockSource(LONG ref) */
    void *GetSamplePosition; /* LONG GetSamplePosition(void *sPos, void *tStamp) */
    void *GetChannelInfo;    /* LONG GetChannelInfo(void *info) */
    void *CreateBuffers;     /* LONG CreateBuffers(void *infos, LONG num, LONG size, void *cb) */
    void *DisposeBuffers;    /* LONG DisposeBuffers() */
    void *ControlPanel;      /* LONG ControlPanel() */
    void *Future;            /* LONG Future(LONG sel, void *params) */
    void *OutputReady;       /* LONG OutputReady() */
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
    printf("WineASIO 32-bit Thiscall Test\n");
    printf("===========================================\n\n");
    
    printf("This test uses the correct thiscall calling convention\n");
    printf("that real ASIO hosts (REAPER, Cubase, FL Studio) use.\n\n");
    
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
    
    /* Get driver name - using thiscall */
    printf("3. Calling GetDriverName() [thiscall]...\n");
    if (pAsio->lpVtbl->GetDriverName) {
        call_thiscall_1_ptr(pAsio->lpVtbl->GetDriverName, pAsio, driverName);
        printf("   OK: Driver name: \"%s\"\n\n", driverName);
    } else {
        printf("   ERROR: GetDriverName is NULL!\n\n");
    }
    
    /* Get driver version - using thiscall */
    printf("4. Calling GetDriverVersion() [thiscall]...\n");
    if (pAsio->lpVtbl->GetDriverVersion) {
        version = call_thiscall_0(pAsio->lpVtbl->GetDriverVersion, pAsio);
        printf("   OK: Driver version: %ld (0x%lx)\n\n", version, version);
    } else {
        printf("   ERROR: GetDriverVersion is NULL!\n\n");
    }
    
    /* Initialize ASIO - using thiscall */
    printf("5. Calling Init(NULL) [thiscall]...\n");
    if (pAsio->lpVtbl->Init) {
        initResult = call_thiscall_init(pAsio->lpVtbl->Init, pAsio, NULL);
        if (initResult) {
            printf("   OK: Init succeeded (returned %ld)\n\n", initResult);
        } else {
            printf("   ERROR: Init failed (returned 0)\n");
            printf("   Is JACK running? Start with: jackdbus auto\n\n");
        }
    } else {
        printf("   ERROR: Init is NULL!\n\n");
    }
    
    /* Get channels - using thiscall */
    printf("6. Calling GetChannels() [thiscall]...\n");
    if (pAsio->lpVtbl->GetChannels) {
        LONG result = call_thiscall_getchannels(pAsio->lpVtbl->GetChannels, pAsio, &numInputs, &numOutputs);
        if (result == 0) {  /* ASE_OK = 0 */
            printf("   OK: Inputs=%ld, Outputs=%ld\n\n", numInputs, numOutputs);
        } else {
            printf("   GetChannels returned error: %ld\n\n", result);
        }
    } else {
        printf("   ERROR: GetChannels is NULL!\n\n");
    }
    
    /* Release instance - IUnknown uses stdcall, not thiscall */
    printf("7. Releasing WineASIO instance [stdcall]...\n");
    if (pAsio->lpVtbl->Release) {
        ULONG refCount = pAsio->lpVtbl->Release(pAsio);
        printf("   OK: Released (refcount=%lu)\n\n", refCount);
    } else {
        printf("   ERROR: Release is NULL!\n\n");
    }
    
    /* Uninitialize COM */
    printf("8. Uninitializing COM...\n");
    CoUninitialize();
    printf("   OK: COM uninitialized\n\n");
    
    printf("===========================================\n");
    printf("Test completed successfully!\n");
    printf("===========================================\n");
    
    return 0;
}