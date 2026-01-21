/*
 * Minimal WineASIO test DLL - NO OLE32 LINKING
 * Full ASIO interface but avoids ole32/oleaut32 libraries entirely
 * Uses inline implementations of COM helpers
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

/* Avoid including objbase.h to prevent ole32 dependencies */
/* Define COM essentials ourselves */

typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;

typedef GUID IID;
typedef GUID CLSID;
typedef const GUID *REFGUID;
typedef const IID *REFIID;
typedef const CLSID *REFCLSID;

/* COM return codes */
#define S_OK                    ((HRESULT)0L)
#define S_FALSE                 ((HRESULT)1L)
#define E_NOINTERFACE           ((HRESULT)0x80004002L)
#define E_POINTER               ((HRESULT)0x80004003L)
#define E_OUTOFMEMORY           ((HRESULT)0x8007000EL)
#define CLASS_E_NOAGGREGATION   ((HRESULT)0x80040110L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)

/* Inline GUID comparison - avoids linking to ole32 */
static inline int MyIsEqualGUID(const GUID *a, const GUID *b)
{
    return memcmp(a, b, sizeof(GUID)) == 0;
}
#define IsEqualGUID(a, b) MyIsEqualGUID(a, b)

/* Simple debug output */
#define DBG(fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), "[MinimalASIO-NoOLE] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_buf); \
    fprintf(stderr, "%s", _buf); \
    fflush(stderr); \
} while(0)

/* Standard COM GUIDs - defined inline */
static const GUID MY_IID_IUnknown = {
    0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

static const GUID MY_IID_IClassFactory = {
    0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

/* ASIO CLSID - {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = {
    0x48d0c522, 0xbfcc, 0x45cc, {0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8}
};

/* Forward declarations */
typedef struct IWineASIO IWineASIO;
typedef struct IWineASIOVtbl IWineASIOVtbl;
typedef struct WineASIOImpl WineASIOImpl;
typedef struct IClassFactory IClassFactory;
typedef struct IClassFactoryVtbl IClassFactoryVtbl;
typedef struct WineASIOClassFactory WineASIOClassFactory;

/* IUnknown - for aggregation parameter */
typedef struct IUnknown IUnknown;

/* Minimal ASIO type definitions */
typedef LONG ASIOBool;
typedef LONG ASIOError;

#define ASIOFalse 0
#define ASIOTrue 1
#define ASE_OK 0
#define ASE_NotPresent -1000
#define ASE_InvalidParameter -1002

/* IWineASIO interface */
struct IWineASIOVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
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

/* IClassFactory interface */
struct IClassFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IClassFactory *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IClassFactory *This);
    ULONG (STDMETHODCALLTYPE *Release)(IClassFactory *This);
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(IClassFactory *This, IUnknown *pUnkOuter, REFIID riid, void **ppvObject);
    HRESULT (STDMETHODCALLTYPE *LockServer)(IClassFactory *This, BOOL fLock);
};

struct IClassFactory {
    const IClassFactoryVtbl *lpVtbl;
};

struct WineASIOClassFactory {
    IClassFactory IClassFactory_iface;
    LONG ref;
};

/* Helpers */
static inline WineASIOImpl *impl_from_IWineASIO(IWineASIO *iface)
{
    return (WineASIOImpl *)((char *)iface - (size_t)&((WineASIOImpl *)0)->IWineASIO_iface);
}

static inline WineASIOClassFactory *impl_from_IClassFactory(IClassFactory *iface)
{
    return (WineASIOClassFactory *)((char *)iface - (size_t)&((WineASIOClassFactory *)0)->IClassFactory_iface);
}

/* WineASIO Implementation */
static HRESULT STDMETHODCALLTYPE WineASIO_QueryInterface(IWineASIO *iface, REFIID riid, void **ppv)
{
    DBG("QueryInterface");
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &MY_IID_IUnknown) || IsEqualGUID(riid, &CLSID_WineASIO)) {
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
    return InterlockedIncrement(&This->ref);
}

static ULONG STDMETHODCALLTYPE WineASIO_Release(IWineASIO *iface)
{
    WineASIOImpl *This = impl_from_IWineASIO(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (ref == 0) HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static ASIOBool STDMETHODCALLTYPE WineASIO_init(IWineASIO *iface, void *sysHandle)
{
    (void)iface; (void)sysHandle;
    DBG("init - returning FALSE");
    return ASIOFalse;
}

static void STDMETHODCALLTYPE WineASIO_getDriverName(IWineASIO *iface, char *name)
{
    (void)iface;
    if (name) strcpy(name, "WineASIO (NoOLE Test)");
}

static LONG STDMETHODCALLTYPE WineASIO_getDriverVersion(IWineASIO *iface)
{
    (void)iface;
    return 13;
}

static void STDMETHODCALLTYPE WineASIO_getErrorMessage(IWineASIO *iface, char *string)
{
    (void)iface;
    if (string) strcpy(string, "Minimal test - no OLE32");
}

static ASIOError STDMETHODCALLTYPE WineASIO_start(IWineASIO *iface) { (void)iface; return ASE_NotPresent; }
static ASIOError STDMETHODCALLTYPE WineASIO_stop(IWineASIO *iface) { (void)iface; return ASE_NotPresent; }

static ASIOError STDMETHODCALLTYPE WineASIO_getChannels(IWineASIO *iface, LONG *in, LONG *out)
{
    (void)iface;
    if (in) *in = 0;
    if (out) *out = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getLatencies(IWineASIO *iface, LONG *in, LONG *out)
{
    (void)iface;
    if (in) *in = 0;
    if (out) *out = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getBufferSize(IWineASIO *iface, LONG *min, LONG *max, LONG *pref, LONG *gran)
{
    (void)iface;
    if (min) *min = 256;
    if (max) *max = 8192;
    if (pref) *pref = 1024;
    if (gran) *gran = 1;
    return ASE_OK;
}

static ASIOError STDMETHODCALLTYPE WineASIO_canSampleRate(IWineASIO *iface, double rate)
{
    (void)iface; (void)rate;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getSampleRate(IWineASIO *iface, double *rate)
{
    (void)iface;
    if (rate) *rate = 48000.0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_setSampleRate(IWineASIO *iface, double rate)
{
    (void)iface; (void)rate;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getClockSources(IWineASIO *iface, void *clocks, LONG *num)
{
    (void)iface; (void)clocks;
    if (num) *num = 0;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_setClockSource(IWineASIO *iface, LONG ref)
{
    (void)iface; (void)ref;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getSamplePosition(IWineASIO *iface, void *pos, void *ts)
{
    (void)iface; (void)pos; (void)ts;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_getChannelInfo(IWineASIO *iface, void *info)
{
    (void)iface; (void)info;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_createBuffers(IWineASIO *iface, void *bufs, LONG num, LONG size, void *cb)
{
    (void)iface; (void)bufs; (void)num; (void)size; (void)cb;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_disposeBuffers(IWineASIO *iface)
{
    (void)iface;
    return ASE_NotPresent;
}

static ASIOError STDMETHODCALLTYPE WineASIO_controlPanel(IWineASIO *iface)
{
    (void)iface;
    DBG("controlPanel");
    return ASE_OK;
}

static ASIOError STDMETHODCALLTYPE WineASIO_future(IWineASIO *iface, LONG sel, void *opt)
{
    (void)iface; (void)sel; (void)opt;
    return ASE_InvalidParameter;
}

static ASIOError STDMETHODCALLTYPE WineASIO_outputReady(IWineASIO *iface)
{
    (void)iface;
    return ASE_NotPresent;
}

static const IWineASIOVtbl WineASIO_Vtbl = {
    WineASIO_QueryInterface, WineASIO_AddRef, WineASIO_Release,
    WineASIO_init, WineASIO_getDriverName, WineASIO_getDriverVersion,
    WineASIO_getErrorMessage, WineASIO_start, WineASIO_stop,
    WineASIO_getChannels, WineASIO_getLatencies, WineASIO_getBufferSize,
    WineASIO_canSampleRate, WineASIO_getSampleRate, WineASIO_setSampleRate,
    WineASIO_getClockSources, WineASIO_setClockSource, WineASIO_getSamplePosition,
    WineASIO_getChannelInfo, WineASIO_createBuffers, WineASIO_disposeBuffers,
    WineASIO_controlPanel, WineASIO_future, WineASIO_outputReady
};

/* Class Factory Implementation */
static HRESULT STDMETHODCALLTYPE CF_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &MY_IID_IUnknown) || IsEqualGUID(riid, &MY_IID_IClassFactory)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE CF_AddRef(IClassFactory *iface)
{
    WineASIOClassFactory *This = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG STDMETHODCALLTYPE CF_Release(IClassFactory *iface)
{
    WineASIOClassFactory *This = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (ref == 0) HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT STDMETHODCALLTYPE CF_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    WineASIOImpl *obj;
    (void)iface;
    (void)riid;
    
    DBG("CreateInstance");
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;
    
    obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    
    obj->IWineASIO_iface.lpVtbl = &WineASIO_Vtbl;
    obj->ref = 1;
    *ppv = &obj->IWineASIO_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE CF_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface; (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl CF_Vtbl = {
    CF_QueryInterface, CF_AddRef, CF_Release, CF_CreateInstance, CF_LockServer
};

static WineASIOClassFactory g_ClassFactory = { { &CF_Vtbl }, 1 };

/* DLL Entry Points */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DBG("DllMain PROCESS_ATTACH hInst=%p", hInst);
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    DBG("DllGetClassObject");
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(rclsid, &CLSID_WineASIO)) {
        return CF_QueryInterface(&g_ClassFactory.IClassFactory_iface, riid, ppv);
    }
    *ppv = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    DBG("DllRegisterServer (no-op)");
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    DBG("DllUnregisterServer (no-op)");
    return S_OK;
}