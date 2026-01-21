/*
 * Extended Minimal WineASIO test DLL
 * Has all the ASIO COM exports and registry functions but NO Unix calls
 * This is to test if the crash is caused by Unix call machinery or something else
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <olectl.h>

/* Simple debug output */
#define DBG(fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), "[MinimalASIO] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_buf); \
    fprintf(stderr, "%s", _buf); \
    fflush(stderr); \
} while(0)

/* ASIO CLSID - {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = {
    0x48d0c522, 0xbfcc, 0x45cc, {0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8}
};

/* Forward declarations */
typedef struct IWineASIO IWineASIO;
typedef struct IWineASIOVtbl IWineASIOVtbl;
typedef struct WineASIOImpl WineASIOImpl;

/* Minimal ASIO type definitions */
typedef LONG ASIOBool;
typedef LONG ASIOSampleType;
typedef LONG ASIOError;

#define ASIOFalse 0
#define ASIOTrue 1
#define ASE_OK 0
#define ASE_NotPresent -1000
#define ASE_HWMalfunction -1001
#define ASE_InvalidParameter -1002

/* Minimal ASIODriverInfo */
typedef struct {
    LONG asioVersion;
    LONG driverVersion;
    char name[32];
    char errorMessage[124];
    void *sysRef;
} ASIODriverInfo;

/* Minimal vtable - just enough to satisfy COM */
struct IWineASIOVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    
    /* IASIO minimal */
    ASIOBool (STDMETHODCALLTYPE *init)(IWineASIO *This, void *sysHandle);
    void (STDMETHODCALLTYPE *getDriverName)(IWineASIO *This, char *name);
    LONG (STDMETHODCALLTYPE *getDriverVersion)(IWineASIO *This);
    void (STDMETHODCALLTYPE *getErrorMessage)(IWineASIO *This, char *string);
    ASIOError (STDMETHODCALLTYPE *start)(IWineASIO *This);
    ASIOError (STDMETHODCALLTYPE *stop)(IWineASIO *This);
    ASIOError (STDMETHODCALLTYPE *getChannels)(IWineASIO *This, LONG *numInputChannels, LONG *numOutputChannels);
    ASIOError (STDMETHODCALLTYPE *getLatencies)(IWineASIO *This, LONG *inputLatency, LONG *outputLatency);
    ASIOError (STDMETHODCALLTYPE *getBufferSize)(IWineASIO *This, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
    ASIOError (STDMETHODCALLTYPE *canSampleRate)(IWineASIO *This, double sampleRate);
    ASIOError (STDMETHODCALLTYPE *getSampleRate)(IWineASIO *This, double *sampleRate);
    ASIOError (STDMETHODCALLTYPE *setSampleRate)(IWineASIO *This, double sampleRate);
    ASIOError (STDMETHODCALLTYPE *getClockSources)(IWineASIO *This, void *clocks, LONG *numSources);
    ASIOError (STDMETHODCALLTYPE *setClockSource)(IWineASIO *This, LONG reference);
    ASIOError (STDMETHODCALLTYPE *getSamplePosition)(IWineASIO *This, void *sPos, void *tStamp);
    ASIOError (STDMETHODCALLTYPE *getChannelInfo)(IWineASIO *This, void *info);
    ASIOError (STDMETHODCALLTYPE *createBuffers)(IWineASIO *This, void *bufferInfos, LONG numChannels, LONG bufferSize, void *callbacks);
    ASIOError (STDMETHODCALLTYPE *disposeBuffers)(IWineASIO *This);
    ASIOError (STDMETHODCALLTYPE *controlPanel)(IWineASIO *This);
    ASIOError (STDMETHODCALLTYPE *future)(IWineASIO *This, LONG selector, void *opt);
    ASIOError (STDMETHODCALLTYPE *outputReady)(IWineASIO *This);
};

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
};

struct WineASIOImpl {
    IWineASIO IWineASIO_iface;
    LONG ref;
};

/* Implementation */
static inline WineASIOImpl *impl_from_IWineASIO(IWineASIO *iface)
{
    return (WineASIOImpl *)((char *)iface - offsetof(WineASIOImpl, IWineASIO_iface));
}

static HRESULT STDMETHODCALLTYPE WineASIO_QueryInterface(IWineASIO *iface, REFIID riid, void **ppv)
{
    DBG("QueryInterface called");
    if (!ppv) return E_POINTER;
    
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &CLSID_WineASIO)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE WineASIO_AddRef(IWineASIO *iface)
{
    WineASIOImpl *This = impl_from_IWineASIO(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    DBG("AddRef: %lu", ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE WineASIO_Release(IWineASIO *iface)
{
    WineASIOImpl *This = impl_from_IWineASIO(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    DBG("Release: %lu", ref);
    if (ref == 0) {
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static ASIOBool STDMETHODCALLTYPE WineASIO_init(IWineASIO *iface, void *sysHandle)
{
    (void)iface; (void)sysHandle;
    DBG("init called - returning FALSE (minimal test)");
    return ASIOFalse;  /* Minimal: don't initialize */
}

static void STDMETHODCALLTYPE WineASIO_getDriverName(IWineASIO *iface, char *name)
{
    (void)iface;
    DBG("getDriverName called");
    if (name) strcpy(name, "WineASIO (Minimal Test)");
}

static LONG STDMETHODCALLTYPE WineASIO_getDriverVersion(IWineASIO *iface)
{
    (void)iface;
    DBG("getDriverVersion called");
    return 13;  /* Version 1.3 */
}

static void STDMETHODCALLTYPE WineASIO_getErrorMessage(IWineASIO *iface, char *string)
{
    (void)iface;
    DBG("getErrorMessage called");
    if (string) strcpy(string, "Minimal test driver - no functionality");
}

static ASIOError STDMETHODCALLTYPE WineASIO_start(IWineASIO *iface)
{
    (void)iface;
    DBG("start called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_stop(IWineASIO *iface)
{
    (void)iface;
    DBG("stop called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getChannels(IWineASIO *iface, LONG *numInputChannels, LONG *numOutputChannels)
{
    (void)iface;
    DBG("getChannels called");
    if (numInputChannels) *numInputChannels = 0;
    if (numOutputChannels) *numOutputChannels = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getLatencies(IWineASIO *iface, LONG *inputLatency, LONG *outputLatency)
{
    (void)iface;
    DBG("getLatencies called");
    if (inputLatency) *inputLatency = 0;
    if (outputLatency) *outputLatency = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getBufferSize(IWineASIO *iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity)
{
    (void)iface;
    DBG("getBufferSize called");
    if (minSize) *minSize = 256;
    if (maxSize) *maxSize = 8192;
    if (preferredSize) *preferredSize = 1024;
    if (granularity) *granularity = 1;
    return ASE_OK;
}

static ASIOError STDMETHODCALLTYPE WineASIO_canSampleRate(IWineASIO *iface, double sampleRate)
{
    (void)iface; (void)sampleRate;
    DBG("canSampleRate called: %f", sampleRate);
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getSampleRate(IWineASIO *iface, double *sampleRate)
{
    (void)iface;
    DBG("getSampleRate called");
    if (sampleRate) *sampleRate = 48000.0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_setSampleRate(IWineASIO *iface, double sampleRate)
{
    (void)iface; (void)sampleRate;
    DBG("setSampleRate called: %f", sampleRate);
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getClockSources(IWineASIO *iface, void *clocks, LONG *numSources)
{
    (void)iface; (void)clocks;
    DBG("getClockSources called");
    if (numSources) *numSources = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_setClockSource(IWineASIO *iface, LONG reference)
{
    (void)iface; (void)reference;
    DBG("setClockSource called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getSamplePosition(IWineASIO *iface, void *sPos, void *tStamp)
{
    (void)iface; (void)sPos; (void)tStamp;
    DBG("getSamplePosition called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getChannelInfo(IWineASIO *iface, void *info)
{
    (void)iface; (void)info;
    DBG("getChannelInfo called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_createBuffers(IWineASIO *iface, void *bufferInfos, LONG numChannels, LONG bufferSize, void *callbacks)
{
    (void)iface; (void)bufferInfos; (void)numChannels; (void)bufferSize; (void)callbacks;
    DBG("createBuffers called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_disposeBuffers(IWineASIO *iface)
{
    (void)iface;
    DBG("disposeBuffers called");
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_controlPanel(IWineASIO *iface)
{
    (void)iface;
    DBG("controlPanel called");
    MessageBoxA(NULL, "WineASIO Minimal Test\n\nThis is a diagnostic build with no functionality.", "WineASIO", MB_OK);
    return ASE_OK;
}

static ASIOError STDMETHODCALLTYPE WineASIO_future(IWineASIO *iface, LONG selector, void *opt)
{
    (void)iface; (void)selector; (void)opt;
    DBG("future called: selector=%ld", selector);
    return ASE_InvalidParameter;
}

static ASIOError STDMETHODCALLTYPE WineASIO_outputReady(IWineASIO *iface)
{
    (void)iface;
    DBG("outputReady called");
    return ASE_NotPresent;
}

/* Vtable */
static const IWineASIOVtbl WineASIO_Vtbl = {
    WineASIO_QueryInterface,
    WineASIO_AddRef,
    WineASIO_Release,
    WineASIO_init,
    WineASIO_getDriverName,
    WineASIO_getDriverVersion,
    WineASIO_getErrorMessage,
    WineASIO_start,
    WineASIO_stop,
    WineASIO_getChannels,
    WineASIO_getLatencies,
    WineASIO_getBufferSize,
    WineASIO_canSampleRate,
    WineASIO_getSampleRate,
    WineASIO_setSampleRate,
    WineASIO_getClockSources,
    WineASIO_setClockSource,
    WineASIO_getSamplePosition,
    WineASIO_getChannelInfo,
    WineASIO_createBuffers,
    WineASIO_disposeBuffers,
    WineASIO_controlPanel,
    WineASIO_future,
    WineASIO_outputReady
};

/* Class Factory */
typedef struct {
    IClassFactory IClassFactory_iface;
    LONG ref;
} WineASIOClassFactory;

static inline WineASIOClassFactory *impl_from_IClassFactory(IClassFactory *iface)
{
    return (WineASIOClassFactory *)((char *)iface - offsetof(WineASIOClassFactory, IClassFactory_iface));
}

static HRESULT STDMETHODCALLTYPE ClassFactory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    DBG("ClassFactory_QueryInterface");
    if (!ppv) return E_POINTER;
    
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ClassFactory_AddRef(IClassFactory *iface)
{
    WineASIOClassFactory *This = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG STDMETHODCALLTYPE ClassFactory_Release(IClassFactory *iface)
{
    WineASIOClassFactory *This = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (ref == 0) {
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_CreateInstance(IClassFactory *iface, IUnknown *pUnkOuter, REFIID riid, void **ppv)
{
    WineASIOImpl *obj;
    
    DBG("ClassFactory_CreateInstance");
    
    (void)iface;
    
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;
    
    obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    
    obj->IWineASIO_iface.lpVtbl = &WineASIO_Vtbl;
    obj->ref = 1;
    
    *ppv = &obj->IWineASIO_iface;
    DBG("Created WineASIO instance at %p", *ppv);
    
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_LockServer(IClassFactory *iface, BOOL fLock)
{
    (void)iface; (void)fLock;
    DBG("ClassFactory_LockServer: %d", fLock);
    return S_OK;
}

static const IClassFactoryVtbl ClassFactory_Vtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

/* Global class factory instance */
static WineASIOClassFactory g_ClassFactory = {
    { &ClassFactory_Vtbl },
    1  /* Static instance, ref count starts at 1 */
};

/* DLL Exports */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DBG("DllMain: DLL_PROCESS_ATTACH hInst=%p", hInstDLL);
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        DBG("DllMain: DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    DBG("DllGetClassObject called");
    
    if (!ppv) return E_POINTER;
    
    if (IsEqualGUID(rclsid, &CLSID_WineASIO)) {
        DBG("  CLSID matches WineASIO");
        return ClassFactory_QueryInterface(&g_ClassFactory.IClassFactory_iface, riid, ppv);
    }
    
    DBG("  Unknown CLSID");
    *ppv = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    DBG("DllCanUnloadNow called");
    return S_FALSE;  /* Don't unload */
}

/* Registry helper functions */
static LONG create_reg_key(HKEY root, const char *path, HKEY *key)
{
    return RegCreateKeyExA(root, path, 0, NULL, 0, KEY_WRITE, NULL, key, NULL);
}

static LONG set_reg_value(HKEY key, const char *name, const char *value)
{
    return RegSetValueExA(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(strlen(value) + 1));
}

HRESULT WINAPI DllRegisterServer(void)
{
    HKEY key;
    char path[256];
    char clsid_str[] = "{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}";
    
    DBG("DllRegisterServer called");
    
    /* Register CLSID */
    snprintf(path, sizeof(path), "CLSID\\%s", clsid_str);
    if (create_reg_key(HKEY_CLASSES_ROOT, path, &key) == ERROR_SUCCESS) {
        set_reg_value(key, NULL, "WineASIO Driver");
        RegCloseKey(key);
    }
    
    snprintf(path, sizeof(path), "CLSID\\%s\\InprocServer32", clsid_str);
    if (create_reg_key(HKEY_CLASSES_ROOT, path, &key) == ERROR_SUCCESS) {
        set_reg_value(key, NULL, "wineasio.dll");
        set_reg_value(key, "ThreadingModel", "Apartment");
        RegCloseKey(key);
    }
    
    /* Register in ASIO drivers list */
    if (create_reg_key(HKEY_LOCAL_MACHINE, "Software\\ASIO\\WineASIO", &key) == ERROR_SUCCESS) {
        set_reg_value(key, "CLSID", clsid_str);
        set_reg_value(key, "Description", "WineASIO Driver (Minimal Test)");
        RegCloseKey(key);
    }
    
    DBG("DllRegisterServer: registration complete");
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    char clsid_str[] = "{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}";
    char path[256];
    
    DBG("DllUnregisterServer called");
    
    snprintf(path, sizeof(path), "CLSID\\%s\\InprocServer32", clsid_str);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, path);
    
    snprintf(path, sizeof(path), "CLSID\\%s", clsid_str);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, path);
    
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\ASIO\\WineASIO");
    
    DBG("DllUnregisterServer: unregistration complete");
    return S_OK;
}