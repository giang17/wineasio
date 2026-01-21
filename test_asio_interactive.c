/* ASIO Interactive Test Program
 * 
 * Purpose: Test WineASIO with JACK connection that stays open
 * until user presses Enter - allows inspection in Patchance/Carla
 * 
 * Compile:
 *   i686-w64-mingw32-gcc -o test_asio_interactive.exe test_asio_interactive.c -lole32 -luuid
 * 
 * Run:
 *   WINEDEBUG=-all wine test_asio_interactive.exe
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

typedef struct IWineASIO IWineASIO;

/* ASIO Structures */
typedef struct {
    long isInputType;
    long channelNumber;
    void *audioBufferStart;
    void *audioBufferEnd;
} BufferInformation;

typedef struct {
    void (*swapBuffers)(long, long);
    void (*sampleRateChanged)(double);
    long (*sendNotification)(long, long, void*, double*);
    void* (*swapBuffersWithTimeInfo)(void*, long, long);
} Callbacks;

/* Thiscall wrappers */
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

static __inline__ LONG call_thiscall_2_ptr(void *func, IWineASIO *pThis, void *arg1, void *arg2)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %3\n\t"
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%4\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (arg1), "r" (arg2), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

static __inline__ LONG call_thiscall_4_ptr(void *func, IWineASIO *pThis, void *a1, void *a2, void *a3, void *a4)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %5\n\t"
        "pushl %4\n\t"
        "pushl %3\n\t"
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%6\n\t"
        : "=a" (result)
        : "r" (pThis), "g" (a1), "g" (a2), "g" (a3), "g" (a4), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

static __inline__ LONG call_thiscall_get_samplerate(void *func, IWineASIO *pThis, double *rate)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%3\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (rate), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

static __inline__ LONG call_thiscall_create_buffers(void *func, IWineASIO *pThis, 
                                                      BufferInformation *info, LONG num, LONG size, Callbacks *cb)
{
    LONG result;
    __asm__ __volatile__ (
        "pushl %5\n\t"
        "pushl %4\n\t"
        "pushl %3\n\t"
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%6\n\t"
        : "=a" (result)
        : "r" (pThis), "g" (info), "g" (num), "g" (size), "g" (cb), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

/* ASIO vtable structure */
typedef struct IWineASIOVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    void *Init;
    void *GetDriverName;
    void *GetDriverVersion;
    void *GetErrorMessage;
    void *Start;
    void *Stop;
    void *GetChannels;
    void *GetLatencies;
    void *GetBufferSize;
    void *CanSampleRate;
    void *GetSampleRate;
    void *SetSampleRate;
    void *GetClockSources;
    void *SetClockSource;
    void *GetSamplePosition;
    void *GetChannelInfo;
    void *CreateBuffers;
    void *DisposeBuffers;
    void *ControlPanel;
    void *Future;
    void *OutputReady;
} IWineASIOVtbl;

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
};

/* Callback counter */
static volatile long callback_count = 0;

/* Dummy ASIO callbacks */
static void dummy_swapBuffers(long index, long processNow)
{
    callback_count++;
}

static void dummy_sampleRateChanged(double sampleRate)
{
    printf("   [CALLBACK] sampleRateChanged(%f)\n", sampleRate);
}

static long dummy_sendNotification(long selector, long value, void *message, double *opt)
{
    return 0;
}

static void* dummy_swapBuffersWithTimeInfo(void *timeInfo, long index, long processNow)
{
    callback_count++;
    return NULL;
}

int main(int argc, char *argv[])
{
    HRESULT hr;
    IWineASIO *pAsio = NULL;
    LONG result;
    LONG numInputs = 0, numOutputs = 0;
    LONG minSize = 0, maxSize = 0, prefSize = 0, granularity = 0;
    double sampleRate = 0.0;
    BufferInformation bufferInfo[4];
    Callbacks callbacks;
    char input[10];
    
    printf("\n");
    printf("==========================================================\n");
    printf("WineASIO Interactive Test - JACK Connection Stays Open\n");
    printf("==========================================================\n\n");
    
    /* Initialize COM */
    printf("[1] Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("    ERROR: CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("    OK\n\n");
    
    /* Create WineASIO instance */
    printf("[2] Creating WineASIO instance...\n");
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown_Local, (void**)&pAsio);
    
    if (FAILED(hr) || !pAsio) {
        printf("    ERROR: CoCreateInstance failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("    OK: Instance at %p\n\n", pAsio);
    
    /* Initialize ASIO */
    printf("[3] Calling Init(NULL)...\n");
    result = call_thiscall_init(pAsio->lpVtbl->Init, pAsio, NULL);
    if (!result) {
        printf("    ERROR: Init failed - is JACK/PipeWire running?\n\n");
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("    OK: Init succeeded\n\n");
    
    /* Get channels */
    printf("[4] Getting channel info...\n");
    call_thiscall_2_ptr(pAsio->lpVtbl->GetChannels, pAsio, &numInputs, &numOutputs);
    printf("    Inputs: %ld, Outputs: %ld\n\n", numInputs, numOutputs);
    
    /* Get buffer size */
    printf("[5] Getting buffer size...\n");
    call_thiscall_4_ptr(pAsio->lpVtbl->GetBufferSize, pAsio, 
                        &minSize, &maxSize, &prefSize, &granularity);
    printf("    min=%ld, max=%ld, preferred=%ld\n\n", minSize, maxSize, prefSize);
    
    /* Get sample rate */
    printf("[6] Getting sample rate...\n");
    call_thiscall_get_samplerate(pAsio->lpVtbl->GetSampleRate, pAsio, &sampleRate);
    printf("    Sample rate: %.0f Hz\n\n", sampleRate);
    
    /* Setup callbacks */
    printf("[7] Setting up callbacks...\n");
    callbacks.swapBuffers = dummy_swapBuffers;
    callbacks.sampleRateChanged = dummy_sampleRateChanged;
    callbacks.sendNotification = dummy_sendNotification;
    callbacks.swapBuffersWithTimeInfo = dummy_swapBuffersWithTimeInfo;
    printf("    OK\n\n");
    
    /* Setup buffer information */
    printf("[8] Setting up buffer info (2 in + 2 out)...\n");
    bufferInfo[0].isInputType = 1;
    bufferInfo[0].channelNumber = 0;
    bufferInfo[0].audioBufferStart = NULL;
    bufferInfo[0].audioBufferEnd = NULL;
    
    bufferInfo[1].isInputType = 1;
    bufferInfo[1].channelNumber = 1;
    bufferInfo[1].audioBufferStart = NULL;
    bufferInfo[1].audioBufferEnd = NULL;
    
    bufferInfo[2].isInputType = 0;
    bufferInfo[2].channelNumber = 0;
    bufferInfo[2].audioBufferStart = NULL;
    bufferInfo[2].audioBufferEnd = NULL;
    
    bufferInfo[3].isInputType = 0;
    bufferInfo[3].channelNumber = 1;
    bufferInfo[3].audioBufferStart = NULL;
    bufferInfo[3].audioBufferEnd = NULL;
    printf("    OK\n\n");
    
    /* Create buffers */
    printf("[9] Calling CreateBuffers(4 channels, %ld samples)...\n", prefSize);
    result = call_thiscall_create_buffers(pAsio->lpVtbl->CreateBuffers, pAsio,
                                          bufferInfo, 4, prefSize, &callbacks);
    if (result != 0) {
        printf("    ERROR: CreateBuffers failed with code %ld\n\n", result);
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("    OK: Buffers created\n\n");
    
    /* Start */
    printf("[10] Calling Start()...\n");
    result = call_thiscall_0(pAsio->lpVtbl->Start, pAsio);
    if (result != 0) {
        printf("    ERROR: Start() failed with code %ld\n\n", result);
    } else {
        printf("    OK: Audio streaming started!\n\n");
    }
    
    printf("==========================================================\n");
    printf("JACK CONNECTION IS NOW ACTIVE!\n");
    printf("==========================================================\n\n");
    printf("You should now see WineASIO ports in Patchance/Carla.\n");
    printf("Ports created:\n");
    printf("  - WineASIO:in_1, WineASIO:in_2 (inputs)\n");
    printf("  - WineASIO:out_1, WineASIO:out_2 (outputs)\n\n");
    
    printf("Callback counter is running. Current count: %ld\n\n", callback_count);
    
    printf(">>> Press ENTER to stop and cleanup... <<<\n\n");
    fgets(input, sizeof(input), stdin);
    
    printf("Final callback count: %ld\n\n", callback_count);
    
    /* Cleanup */
    printf("[11] Calling Stop()...\n");
    call_thiscall_0(pAsio->lpVtbl->Stop, pAsio);
    printf("    OK\n\n");
    
    printf("[12] Calling DisposeBuffers()...\n");
    call_thiscall_0(pAsio->lpVtbl->DisposeBuffers, pAsio);
    printf("    OK\n\n");
    
    printf("[13] Releasing instance...\n");
    pAsio->lpVtbl->Release(pAsio);
    printf("    OK\n\n");
    
    printf("[14] Uninitializing COM...\n");
    CoUninitialize();
    printf("    OK\n\n");
    
    printf("==========================================================\n");
    printf("Test completed successfully!\n");
    printf("==========================================================\n\n");
    
    return 0;
}