/* ASIO Start Test Program with proper thiscall calling convention
 * 
 * Purpose: Test WineASIO 32-bit Start() method to debug why REAPER doesn't get audio
 * 
 * This test goes through the full ASIO initialization sequence:
 * 1. Init()
 * 2. GetChannels()
 * 3. GetBufferSize()
 * 4. GetSampleRate()
 * 5. CanSampleRate()
 * 6. CreateBuffers()
 * 7. Start() <-- THIS IS WHAT WE'RE TESTING!
 * 8. Stop()
 * 9. DisposeBuffers()
 * 
 * Compile:
 *   i686-w64-mingw32-gcc -o test_asio_start.exe test_asio_start.c -lole32 -luuid
 * 
 * Run:
 *   WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "WineASIO|Test|OK|ERROR"
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

/* ASIO Structures */
typedef struct {
    long isInputType;      // 1 = input, 0 = output
    long channelNumber;    // Channel index
    void *audioBufferStart;  // Pointer to buffer
    void *audioBufferEnd;    // Pointer to buffer end
} BufferInformation;

typedef struct {
    void (*swapBuffers)(long, long);
    void (*sampleRateChanged)(double);
    long (*sendNotification)(long, long, void*, double*);
    void* (*swapBuffersWithTimeInfo)(void*, long, long);
} Callbacks;

/*
 * Thiscall wrappers for calling ASIO methods
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

/* Call a method with one pointer arg: void method(this, void *arg) */
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

/* Call GetBufferSize: long GetBufferSize(this, long *min, long *max, long *pref, long *gran) */
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

/* Call GetSampleRate: long GetSampleRate(this, double *rate) */
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

/* Call CanSampleRate: long CanSampleRate(this, double rate) */
static __inline__ LONG call_thiscall_can_samplerate(void *func, IWineASIO *pThis, double rate)
{
    LONG result;
    unsigned long long rate_bits = *(unsigned long long*)&rate;
    unsigned long rate_lo = (unsigned long)(rate_bits & 0xFFFFFFFF);
    unsigned long rate_hi = (unsigned long)(rate_bits >> 32);
    
    __asm__ __volatile__ (
        "pushl %3\n\t"
        "pushl %2\n\t"
        "movl %1, %%ecx\n\t"
        "call *%4\n\t"
        : "=a" (result)
        : "r" (pThis), "r" (rate_lo), "r" (rate_hi), "r" (func)
        : "ecx", "edx", "memory"
    );
    return result;
}

/* Call CreateBuffers: long CreateBuffers(this, BufferInformation *info, long num, long size, Callbacks *cb) */
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
    /* IUnknown - use stdcall */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *This);
    
    /* IASIO - use thiscall (these are raw function pointers) */
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

/* Dummy ASIO callbacks - remove CALLBACK attribute to match function pointer types */
static void dummy_swapBuffers(long index, long processNow)
{
    printf("   [CALLBACK] swapBuffers(index=%ld, processNow=%ld)\n", index, processNow);
}

static void dummy_sampleRateChanged(double sampleRate)
{
    printf("   [CALLBACK] sampleRateChanged(sampleRate=%f)\n", sampleRate);
}

static long dummy_sendNotification(long selector, long value, void *message, double *opt)
{
    printf("   [CALLBACK] sendNotification(selector=%ld, value=%ld)\n", selector, value);
    return 0;  /* Not supported */
}

static void* dummy_swapBuffersWithTimeInfo(void *timeInfo, long index, long processNow)
{
    printf("   [CALLBACK] swapBuffersWithTimeInfo(index=%ld, processNow=%ld)\n", index, processNow);
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
    BufferInformation bufferInfo[4];  /* 2 inputs + 2 outputs */
    Callbacks callbacks;
    
    printf("\n");
    printf("=======================================================\n");
    printf("WineASIO Start() Test - Full ASIO Initialization\n");
    printf("=======================================================\n\n");
    
    /* Initialize COM */
    printf("Step 1: Initialize COM\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("   ERROR: CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("   OK: COM initialized\n\n");
    
    /* Create WineASIO instance */
    printf("Step 2: Create WineASIO instance\n");
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown_Local, (void**)&pAsio);
    
    if (FAILED(hr) || !pAsio) {
        printf("   ERROR: CoCreateInstance failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("   OK: Instance created at %p\n\n", pAsio);
    
    /* Initialize ASIO */
    printf("Step 3: Call Init(NULL)\n");
    result = call_thiscall_init(pAsio->lpVtbl->Init, pAsio, NULL);
    if (!result) {
        printf("   ERROR: Init failed - is JACK running?\n\n");
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("   OK: Init succeeded (returned %ld)\n\n", result);
    
    /* Get channels */
    printf("Step 4: Call GetChannels()\n");
    result = call_thiscall_2_ptr(pAsio->lpVtbl->GetChannels, pAsio, &numInputs, &numOutputs);
    if (result != 0) {
        printf("   ERROR: GetChannels failed with code %ld\n\n", result);
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("   OK: Inputs=%ld, Outputs=%ld\n\n", numInputs, numOutputs);
    
    /* Get buffer size */
    printf("Step 5: Call GetBufferSize()\n");
    result = call_thiscall_4_ptr(pAsio->lpVtbl->GetBufferSize, pAsio, 
                                   &minSize, &maxSize, &prefSize, &granularity);
    if (result != 0) {
        printf("   ERROR: GetBufferSize failed with code %ld\n\n", result);
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("   OK: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n\n", 
           minSize, maxSize, prefSize, granularity);
    
    /* Get sample rate */
    printf("Step 6: Call GetSampleRate()\n");
    result = call_thiscall_get_samplerate(pAsio->lpVtbl->GetSampleRate, pAsio, &sampleRate);
    if (result != 0) {
        printf("   ERROR: GetSampleRate failed with code %ld\n\n", result);
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("   OK: Sample rate = %f Hz\n\n", sampleRate);
    
    /* Check if we can use this sample rate */
    printf("Step 7: Call CanSampleRate(%f)\n", sampleRate);
    result = call_thiscall_can_samplerate(pAsio->lpVtbl->CanSampleRate, pAsio, sampleRate);
    if (result != 0) {
        printf("   WARNING: CanSampleRate returned %ld (not supported?)\n\n", result);
    } else {
        printf("   OK: Sample rate is supported\n\n");
    }
    
    /* Setup callbacks */
    printf("Step 8: Setup callbacks\n");
    callbacks.swapBuffers = dummy_swapBuffers;
    callbacks.sampleRateChanged = dummy_sampleRateChanged;
    callbacks.sendNotification = dummy_sendNotification;
    callbacks.swapBuffersWithTimeInfo = dummy_swapBuffersWithTimeInfo;
    printf("   OK: Callbacks configured\n\n");
    
    /* Setup buffer information - request 2 inputs + 2 outputs */
    printf("Step 9: Setup BufferInformation (2 inputs + 2 outputs)\n");
    bufferInfo[0].isInputType = 1;  /* Input channel 0 */
    bufferInfo[0].channelNumber = 0;
    bufferInfo[0].audioBufferStart = NULL;
    bufferInfo[0].audioBufferEnd = NULL;
    
    bufferInfo[1].isInputType = 1;  /* Input channel 1 */
    bufferInfo[1].channelNumber = 1;
    bufferInfo[1].audioBufferStart = NULL;
    bufferInfo[1].audioBufferEnd = NULL;
    
    bufferInfo[2].isInputType = 0;  /* Output channel 0 */
    bufferInfo[2].channelNumber = 0;
    bufferInfo[2].audioBufferStart = NULL;
    bufferInfo[2].audioBufferEnd = NULL;
    
    bufferInfo[3].isInputType = 0;  /* Output channel 1 */
    bufferInfo[3].channelNumber = 1;
    bufferInfo[3].audioBufferStart = NULL;
    bufferInfo[3].audioBufferEnd = NULL;
    printf("   OK: BufferInformation configured\n\n");
    
    /* Create buffers */
    printf("Step 10: Call CreateBuffers(numChannels=4, bufferSize=%ld)\n", prefSize);
    result = call_thiscall_create_buffers(pAsio->lpVtbl->CreateBuffers, pAsio,
                                          bufferInfo, 4, prefSize, &callbacks);
    if (result != 0) {
        printf("   ERROR: CreateBuffers failed with code %ld\n\n", result);
        pAsio->lpVtbl->Release(pAsio);
        CoUninitialize();
        return 1;
    }
    printf("   OK: CreateBuffers succeeded\n");
    printf("   Buffer pointers:\n");
    printf("      Input 0:  start=%p, end=%p\n", bufferInfo[0].audioBufferStart, bufferInfo[0].audioBufferEnd);
    printf("      Input 1:  start=%p, end=%p\n", bufferInfo[1].audioBufferStart, bufferInfo[1].audioBufferEnd);
    printf("      Output 0: start=%p, end=%p\n", bufferInfo[2].audioBufferStart, bufferInfo[2].audioBufferEnd);
    printf("      Output 1: start=%p, end=%p\n", bufferInfo[3].audioBufferStart, bufferInfo[3].audioBufferEnd);
    printf("\n");
    
    /* THIS IS THE CRITICAL TEST: Call Start() */
    printf("Step 11: Call Start() - THIS IS THE KEY!\n");
    printf("   Calling Start()...\n");
    result = call_thiscall_0(pAsio->lpVtbl->Start, pAsio);
    if (result != 0) {
        printf("   ERROR: Start() failed with code %ld\n", result);
        printf("   This is likely why REAPER has no audio!\n\n");
    } else {
        printf("   OK: Start() succeeded!\n");
        printf("   Audio processing should now be running...\n\n");
        
        /* Let it run for a bit */
        printf("   Sleeping for 2 seconds to let audio run...\n");
        Sleep(2000);
        printf("   Done sleeping\n\n");
    }
    
    /* Stop */
    printf("Step 12: Call Stop()\n");
    result = call_thiscall_0(pAsio->lpVtbl->Stop, pAsio);
    if (result != 0) {
        printf("   WARNING: Stop() returned %ld\n\n", result);
    } else {
        printf("   OK: Stop() succeeded\n\n");
    }
    
    /* Dispose buffers */
    printf("Step 13: Call DisposeBuffers()\n");
    result = call_thiscall_0(pAsio->lpVtbl->DisposeBuffers, pAsio);
    if (result != 0) {
        printf("   WARNING: DisposeBuffers() returned %ld\n\n", result);
    } else {
        printf("   OK: DisposeBuffers() succeeded\n\n");
    }
    
    /* Release instance */
    printf("Step 14: Release WineASIO instance\n");
    pAsio->lpVtbl->Release(pAsio);
    printf("   OK: Released\n\n");
    
    /* Uninitialize COM */
    printf("Step 15: Uninitialize COM\n");
    CoUninitialize();
    printf("   OK: COM uninitialized\n\n");
    
    printf("=======================================================\n");
    printf("Test completed successfully!\n");
    printf("=======================================================\n\n");
    
    return 0;
}