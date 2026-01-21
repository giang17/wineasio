/* Extended ASIO Test Program
 * 
 * Purpose: Test WineASIO 32-bit with CreateBuffers, Start, Stop and Callbacks
 * This extends the minimal test to find the exact crash point in 32-bit hosts.
 * 
 * Compile (32-bit):
 *   i686-w64-mingw32-gcc -o test_asio_extended.exe test_asio_extended.c -lole32 -luuid
 * 
 * Compile (64-bit):
 *   x86_64-w64-mingw32-gcc -o test_asio_extended64.exe test_asio_extended.c -lole32 -luuid
 * 
 * Run:
 *   WINEDEBUG=warn+wineasio wine test_asio_extended.exe
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

/* ASIO type definitions */
typedef long ASIOError;
typedef long ASIOBool;
typedef long long ASIOSamples;
typedef long long ASIOTimeStamp;

#define ASE_OK                  0
#define ASE_SUCCESS             0x3f4847a0
#define ASE_NotPresent          -1000
#define ASE_HWMalfunction       -999
#define ASE_InvalidParameter    -998
#define ASE_InvalidMode         -997
#define ASE_SPNotAdvancing      -996
#define ASE_NoClock             -995
#define ASE_NoMemory            -994

#define ASIOTrue    1
#define ASIOFalse   0

/* ASIOSampleType */
typedef long ASIOSampleType;
#define ASIOSTInt16MSB   0
#define ASIOSTInt24MSB   1
#define ASIOSTInt32MSB   2
#define ASIOSTFloat32MSB 3
#define ASIOSTFloat64MSB 4
#define ASIOSTInt32MSB16 8
#define ASIOSTInt32MSB18 9
#define ASIOSTInt32MSB20 10
#define ASIOSTInt32MSB24 11
#define ASIOSTInt16LSB   16
#define ASIOSTInt24LSB   17
#define ASIOSTInt32LSB   18
#define ASIOSTFloat32LSB 19
#define ASIOSTFloat64LSB 20
#define ASIOSTInt32LSB16 24
#define ASIOSTInt32LSB18 25
#define ASIOSTInt32LSB20 26
#define ASIOSTInt32LSB24 27

/* ASIOBufferInfo */
typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void *buffers[2];
} ASIOBufferInfo;

/* ASIOChannelInfo */
typedef struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
} ASIOChannelInfo;

/* ASIOTime */
typedef struct AsioTimeInfo {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samplePosition;
    double sampleRate;
    unsigned long flags;
    char reserved[12];
} AsioTimeInfo;

typedef struct ASIOTimeCode {
    double speed;
    ASIOSamples timeCodeSamples;
    unsigned long flags;
    char future[64];
} ASIOTimeCode;

typedef struct ASIOTime {
    long reserved[4];
    AsioTimeInfo timeInfo;
    ASIOTimeCode timeCode;
} ASIOTime;

/* ASIOCallbacks */
typedef struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(double sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

/* ASIO Message selectors */
#define kAsioSelectorSupported      1
#define kAsioEngineVersion          2
#define kAsioResetRequest           3
#define kAsioBufferSizeChange       4
#define kAsioResyncRequest          5
#define kAsioLatenciesChanged       6
#define kAsioSupportsTimeInfo       7
#define kAsioSupportsTimeCode       8
#define kAsioMMCCommand             9
#define kAsioSupportsInputMonitor   10
#define kAsioSupportsInputGain      11
#define kAsioSupportsInputMeter     12
#define kAsioSupportsOutputGain     13
#define kAsioSupportsOutputMeter    14
#define kAsioOverload               15

/* Global state for callbacks */
static volatile int g_bufferSwitchCount = 0;
static volatile int g_sampleRateChangeCount = 0;
static volatile int g_asioMessageCount = 0;
static volatile int g_running = 0;

/* Callback implementations - ASIO uses cdecl, NOT stdcall */
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
{
    g_bufferSwitchCount++;
    if (g_bufferSwitchCount <= 5 || (g_bufferSwitchCount % 100) == 0) {
        printf("   [Callback] bufferSwitch(index=%ld, direct=%ld) - count=%d\n", 
               doubleBufferIndex, directProcess, g_bufferSwitchCount);
    }
}

void sampleRateDidChange(double sRate)
{
    g_sampleRateChangeCount++;
    printf("   [Callback] sampleRateDidChange(%.1f Hz)\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt)
{
    g_asioMessageCount++;
    printf("   [Callback] asioMessage(selector=%ld, value=%ld)\n", selector, value);
    
    switch (selector) {
        case kAsioSelectorSupported:
            /* Check if we support a specific selector */
            switch (value) {
                case kAsioEngineVersion:
                case kAsioResetRequest:
                case kAsioBufferSizeChange:
                case kAsioResyncRequest:
                case kAsioLatenciesChanged:
                case kAsioSupportsTimeInfo:
                    return 1;  /* supported */
                default:
                    return 0;  /* not supported */
            }
        case kAsioEngineVersion:
            return 2;  /* ASIO 2.0 */
        case kAsioResetRequest:
        case kAsioBufferSizeChange:
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
            return 1;
        default:
            return 0;
    }
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess)
{
    /* Just call the simpler bufferSwitch */
    bufferSwitch(doubleBufferIndex, directProcess);
    return params;
}

/* ASIO interface definition */
typedef struct IWineASIO IWineASIO;

typedef struct IWineASIOVtbl {
    /* IUnknown - offsets 0-2 */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    
    /* IASIO - offsets 3-23 */
    ASIOBool (STDMETHODCALLTYPE *Init)(IWineASIO *This, void *sysRef);                                  /* 3 */
    void (STDMETHODCALLTYPE *GetDriverName)(IWineASIO *This, char *name);                               /* 4 */
    long (STDMETHODCALLTYPE *GetDriverVersion)(IWineASIO *This);                                        /* 5 */
    void (STDMETHODCALLTYPE *GetErrorMessage)(IWineASIO *This, char *string);                           /* 6 */
    ASIOError (STDMETHODCALLTYPE *Start)(IWineASIO *This);                                              /* 7 */
    ASIOError (STDMETHODCALLTYPE *Stop)(IWineASIO *This);                                               /* 8 */
    ASIOError (STDMETHODCALLTYPE *GetChannels)(IWineASIO *This, long *numInput, long *numOutput);       /* 9 */
    ASIOError (STDMETHODCALLTYPE *GetLatencies)(IWineASIO *This, long *inputLat, long *outputLat);      /* 10 */
    ASIOError (STDMETHODCALLTYPE *GetBufferSize)(IWineASIO *This, long *minSize, long *maxSize, 
                                                  long *preferredSize, long *granularity);              /* 11 */
    ASIOError (STDMETHODCALLTYPE *CanSampleRate)(IWineASIO *This, double sampleRate);                   /* 12 */
    ASIOError (STDMETHODCALLTYPE *GetSampleRate)(IWineASIO *This, double *sampleRate);                  /* 13 */
    ASIOError (STDMETHODCALLTYPE *SetSampleRate)(IWineASIO *This, double sampleRate);                   /* 14 */
    ASIOError (STDMETHODCALLTYPE *GetClockSources)(IWineASIO *This, void *clocks, long *numSources);    /* 15 */
    ASIOError (STDMETHODCALLTYPE *SetClockSource)(IWineASIO *This, long reference);                     /* 16 */
    ASIOError (STDMETHODCALLTYPE *GetSamplePosition)(IWineASIO *This, ASIOSamples *sPos, 
                                                      ASIOTimeStamp *tStamp);                           /* 17 */
    ASIOError (STDMETHODCALLTYPE *GetChannelInfo)(IWineASIO *This, ASIOChannelInfo *info);              /* 18 */
    ASIOError (STDMETHODCALLTYPE *CreateBuffers)(IWineASIO *This, ASIOBufferInfo *bufferInfos, 
                                                  long numChannels, long bufferSize, 
                                                  ASIOCallbacks *callbacks);                            /* 19 */
    ASIOError (STDMETHODCALLTYPE *DisposeBuffers)(IWineASIO *This);                                     /* 20 */
    ASIOError (STDMETHODCALLTYPE *ControlPanel)(IWineASIO *This);                                       /* 21 */
    ASIOError (STDMETHODCALLTYPE *Future)(IWineASIO *This, long selector, void *opt);                   /* 22 */
    ASIOError (STDMETHODCALLTYPE *OutputReady)(IWineASIO *This);                                        /* 23 */
} IWineASIOVtbl;

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
};

/* Helper to print ASIOError */
const char* asioErrorString(ASIOError err)
{
    switch (err) {
        case ASE_OK: return "ASE_OK";
        case ASE_SUCCESS: return "ASE_SUCCESS";
        case ASE_NotPresent: return "ASE_NotPresent";
        case ASE_HWMalfunction: return "ASE_HWMalfunction";
        case ASE_InvalidParameter: return "ASE_InvalidParameter";
        case ASE_InvalidMode: return "ASE_InvalidMode";
        case ASE_SPNotAdvancing: return "ASE_SPNotAdvancing";
        case ASE_NoClock: return "ASE_NoClock";
        case ASE_NoMemory: return "ASE_NoMemory";
        default: return "Unknown";
    }
}

/* Helper to print sample type */
const char* sampleTypeString(ASIOSampleType type)
{
    switch (type) {
        case ASIOSTInt16LSB: return "Int16LSB";
        case ASIOSTInt24LSB: return "Int24LSB";
        case ASIOSTInt32LSB: return "Int32LSB";
        case ASIOSTFloat32LSB: return "Float32LSB";
        case ASIOSTFloat64LSB: return "Float64LSB";
        case ASIOSTInt16MSB: return "Int16MSB";
        case ASIOSTInt24MSB: return "Int24MSB";
        case ASIOSTInt32MSB: return "Int32MSB";
        case ASIOSTFloat32MSB: return "Float32MSB";
        case ASIOSTFloat64MSB: return "Float64MSB";
        default: return "Unknown";
    }
}

int main(int argc, char *argv[])
{
    HRESULT hr;
    IWineASIO *pAsio = NULL;
    ASIOError err;
    char driverName[256] = {0};
    char errorMsg[256] = {0};
    long version;
    ASIOBool initResult;
    long numInputs = 0, numOutputs = 0;
    long inputLatency = 0, outputLatency = 0;
    long minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    double sampleRate = 0;
    ASIOBufferInfo *bufferInfos = NULL;
    ASIOCallbacks callbacks;
    ASIOChannelInfo channelInfo;
    int numChannels = 0;
    int i;
    int testPhase = 0;
    
    printf("===========================================\n");
    printf("WineASIO Extended Test (CreateBuffers/Start/Callbacks)\n");
    printf("===========================================\n\n");
    
    /* Phase 1: Initialize COM */
    testPhase = 1;
    printf("[Phase %d] Initializing COM...\n", testPhase);
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("   FAILED: CoInitialize returned 0x%08lx\n", hr);
        return 1;
    }
    printf("   OK\n\n");
    
    /* Phase 2: Create WineASIO instance */
    testPhase = 2;
    printf("[Phase %d] Creating WineASIO instance...\n", testPhase);
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown_Local, (void**)&pAsio);
    if (FAILED(hr) || pAsio == NULL) {
        printf("   FAILED: CoCreateInstance returned 0x%08lx\n", hr);
        printf("   Is WineASIO registered? Run: wine regsvr32 wineasio.dll\n");
        CoUninitialize();
        return 1;
    }
    printf("   OK: instance=%p, vtable=%p\n\n", pAsio, pAsio->lpVtbl);
    
    /* Phase 3: Get driver info */
    testPhase = 3;
    printf("[Phase %d] Getting driver info...\n", testPhase);
    pAsio->lpVtbl->GetDriverName(pAsio, driverName);
    version = pAsio->lpVtbl->GetDriverVersion(pAsio);
    printf("   Name: %s\n", driverName);
    printf("   Version: %ld\n\n", version);
    
    /* Phase 4: Initialize ASIO */
    testPhase = 4;
    printf("[Phase %d] Calling Init(NULL)...\n", testPhase);
    initResult = pAsio->lpVtbl->Init(pAsio, NULL);
    if (!initResult) {
        pAsio->lpVtbl->GetErrorMessage(pAsio, errorMsg);
        printf("   FAILED: Init returned 0\n");
        printf("   Error: %s\n", errorMsg);
        printf("   Is JACK running?\n");
        goto cleanup;
    }
    printf("   OK\n\n");
    
    /* Phase 5: Get channels */
    testPhase = 5;
    printf("[Phase %d] Getting channel count...\n", testPhase);
    err = pAsio->lpVtbl->GetChannels(pAsio, &numInputs, &numOutputs);
    if (err != ASE_OK) {
        printf("   FAILED: %s (%ld)\n", asioErrorString(err), err);
        goto cleanup;
    }
    printf("   Inputs: %ld, Outputs: %ld\n\n", numInputs, numOutputs);
    
    /* Phase 6: Get buffer size */
    testPhase = 6;
    printf("[Phase %d] Getting buffer size...\n", testPhase);
    err = pAsio->lpVtbl->GetBufferSize(pAsio, &minSize, &maxSize, &preferredSize, &granularity);
    if (err != ASE_OK) {
        printf("   FAILED: %s (%ld)\n", asioErrorString(err), err);
        goto cleanup;
    }
    printf("   Min: %ld, Max: %ld, Preferred: %ld, Granularity: %ld\n\n", 
           minSize, maxSize, preferredSize, granularity);
    
    /* Phase 7: Get sample rate */
    testPhase = 7;
    printf("[Phase %d] Getting sample rate...\n", testPhase);
    err = pAsio->lpVtbl->GetSampleRate(pAsio, &sampleRate);
    if (err != ASE_OK) {
        printf("   FAILED: %s (%ld)\n", asioErrorString(err), err);
        goto cleanup;
    }
    printf("   Sample rate: %.1f Hz\n\n", sampleRate);
    
    /* Phase 8: Get channel info */
    testPhase = 8;
    printf("[Phase %d] Getting channel info...\n", testPhase);
    
    /* Get info for first input */
    if (numInputs > 0) {
        memset(&channelInfo, 0, sizeof(channelInfo));
        channelInfo.channel = 0;
        channelInfo.isInput = ASIOTrue;
        err = pAsio->lpVtbl->GetChannelInfo(pAsio, &channelInfo);
        if (err == ASE_OK) {
            printf("   Input 0: name='%s', type=%s (%ld)\n", 
                   channelInfo.name, sampleTypeString(channelInfo.type), channelInfo.type);
        }
    }
    
    /* Get info for first output */
    if (numOutputs > 0) {
        memset(&channelInfo, 0, sizeof(channelInfo));
        channelInfo.channel = 0;
        channelInfo.isInput = ASIOFalse;
        err = pAsio->lpVtbl->GetChannelInfo(pAsio, &channelInfo);
        if (err == ASE_OK) {
            printf("   Output 0: name='%s', type=%s (%ld)\n", 
                   channelInfo.name, sampleTypeString(channelInfo.type), channelInfo.type);
        }
    }
    printf("\n");
    
    /* Phase 9: Setup callbacks */
    testPhase = 9;
    printf("[Phase %d] Setting up callbacks...\n", testPhase);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    printf("   bufferSwitch: %p\n", callbacks.bufferSwitch);
    printf("   sampleRateDidChange: %p\n", callbacks.sampleRateDidChange);
    printf("   asioMessage: %p\n", callbacks.asioMessage);
    printf("   bufferSwitchTimeInfo: %p\n\n", callbacks.bufferSwitchTimeInfo);
    
    /* Phase 10: Create buffers */
    testPhase = 10;
    printf("[Phase %d] Creating buffers (THIS IS WHERE 32-BIT MIGHT CRASH)...\n", testPhase);
    
    /* Use 2 inputs + 2 outputs (or less if not available) */
    numChannels = 0;
    if (numInputs > 0) numChannels += (numInputs >= 2) ? 2 : numInputs;
    if (numOutputs > 0) numChannels += (numOutputs >= 2) ? 2 : numOutputs;
    
    if (numChannels == 0) {
        printf("   FAILED: No channels available\n");
        goto cleanup;
    }
    
    bufferInfos = (ASIOBufferInfo*)calloc(numChannels, sizeof(ASIOBufferInfo));
    if (!bufferInfos) {
        printf("   FAILED: Could not allocate buffer info array\n");
        goto cleanup;
    }
    
    i = 0;
    /* Setup input channels */
    if (numInputs >= 1) {
        bufferInfos[i].isInput = ASIOTrue;
        bufferInfos[i].channelNum = 0;
        bufferInfos[i].buffers[0] = NULL;
        bufferInfos[i].buffers[1] = NULL;
        i++;
    }
    if (numInputs >= 2) {
        bufferInfos[i].isInput = ASIOTrue;
        bufferInfos[i].channelNum = 1;
        bufferInfos[i].buffers[0] = NULL;
        bufferInfos[i].buffers[1] = NULL;
        i++;
    }
    /* Setup output channels */
    if (numOutputs >= 1) {
        bufferInfos[i].isInput = ASIOFalse;
        bufferInfos[i].channelNum = 0;
        bufferInfos[i].buffers[0] = NULL;
        bufferInfos[i].buffers[1] = NULL;
        i++;
    }
    if (numOutputs >= 2) {
        bufferInfos[i].isInput = ASIOFalse;
        bufferInfos[i].channelNum = 1;
        bufferInfos[i].buffers[0] = NULL;
        bufferInfos[i].buffers[1] = NULL;
        i++;
    }
    
    printf("   Requesting %d channels, buffer size %ld\n", numChannels, preferredSize);
    printf("   Calling CreateBuffers()...\n");
    fflush(stdout);
    
    err = pAsio->lpVtbl->CreateBuffers(pAsio, bufferInfos, numChannels, preferredSize, &callbacks);
    
    if (err != ASE_OK) {
        printf("   FAILED: CreateBuffers returned %s (%ld)\n", asioErrorString(err), err);
        free(bufferInfos);
        goto cleanup;
    }
    printf("   OK: Buffers created\n");
    
    /* Show buffer pointers */
    for (i = 0; i < numChannels; i++) {
        printf("   Channel %d (%s %ld): buf[0]=%p, buf[1]=%p\n",
               i, bufferInfos[i].isInput ? "in" : "out", bufferInfos[i].channelNum,
               bufferInfos[i].buffers[0], bufferInfos[i].buffers[1]);
    }
    printf("\n");
    
    /* Phase 11: Get latencies */
    testPhase = 11;
    printf("[Phase %d] Getting latencies...\n", testPhase);
    err = pAsio->lpVtbl->GetLatencies(pAsio, &inputLatency, &outputLatency);
    if (err == ASE_OK) {
        printf("   Input latency: %ld samples\n", inputLatency);
        printf("   Output latency: %ld samples\n\n", outputLatency);
    } else {
        printf("   GetLatencies returned: %s (%ld)\n\n", asioErrorString(err), err);
    }
    
    /* Phase 12: Start */
    testPhase = 12;
    printf("[Phase %d] Starting ASIO (THIS MIGHT ALSO CRASH)...\n", testPhase);
    fflush(stdout);
    
    g_running = 1;
    err = pAsio->lpVtbl->Start(pAsio);
    
    if (err != ASE_OK) {
        printf("   FAILED: Start returned %s (%ld)\n", asioErrorString(err), err);
        goto dispose_buffers;
    }
    printf("   OK: ASIO started\n\n");
    
    /* Phase 13: Let it run for a bit */
    testPhase = 13;
    printf("[Phase %d] Running for 2 seconds (waiting for callbacks)...\n", testPhase);
    fflush(stdout);
    
    Sleep(2000);
    
    printf("   Callback counts: bufferSwitch=%d, sampleRateChange=%d, asioMessage=%d\n\n",
           g_bufferSwitchCount, g_sampleRateChangeCount, g_asioMessageCount);
    
    /* Phase 14: Stop */
    testPhase = 14;
    printf("[Phase %d] Stopping ASIO...\n", testPhase);
    g_running = 0;
    err = pAsio->lpVtbl->Stop(pAsio);
    if (err != ASE_OK) {
        printf("   Stop returned: %s (%ld)\n", asioErrorString(err), err);
    } else {
        printf("   OK\n\n");
    }
    
dispose_buffers:
    /* Phase 15: Dispose buffers */
    testPhase = 15;
    printf("[Phase %d] Disposing buffers...\n", testPhase);
    err = pAsio->lpVtbl->DisposeBuffers(pAsio);
    if (err != ASE_OK) {
        printf("   DisposeBuffers returned: %s (%ld)\n", asioErrorString(err), err);
    } else {
        printf("   OK\n\n");
    }
    
    if (bufferInfos) {
        free(bufferInfos);
        bufferInfos = NULL;
    }
    
cleanup:
    /* Phase 16: Release */
    testPhase = 16;
    printf("[Phase %d] Releasing WineASIO...\n", testPhase);
    if (pAsio) {
        pAsio->lpVtbl->Release(pAsio);
        printf("   OK\n\n");
    }
    
    /* Phase 17: Cleanup COM */
    testPhase = 17;
    printf("[Phase %d] Uninitializing COM...\n", testPhase);
    CoUninitialize();
    printf("   OK\n\n");
    
    /* Summary */
    printf("===========================================\n");
    printf("TEST COMPLETED\n");
    printf("===========================================\n");
    printf("Last successful phase: %d\n", testPhase);
    printf("Buffer callbacks received: %d\n", g_bufferSwitchCount);
    printf("\n");
    printf("If crash occurred, note the phase number.\n");
    printf("Phase 10 = CreateBuffers, Phase 12 = Start\n");
    printf("===========================================\n");
    
    return 0;
}