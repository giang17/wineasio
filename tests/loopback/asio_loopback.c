/* asio_loopback.c - a minimal ASIO host that measures the round trip of an
 * ASIO driver through a JACK-internal loop out_1 -> in_1.
 *
 * Loads WineASIO through COM, creates one input and one output buffer
 * (channel 0), starts, and after arming sends single impulses (lb_common.h).
 * In addition:
 *   - GetLatencies before and after the rewiring (what a DAW would compensate),
 *   - samplePosition jumps between two bufferSwitch calls
 *     (= swallowed cycles; a jump other than the buffer size is a gap),
 *   - LB_BURN_US=<us>: simulated processing load of the host per bufferSwitch
 *     (busy wait), to check the behaviour with a tight or overrun period;
 *     with LB_BURN_EVERY=<n> only in every n-th call (a load peak instead of
 *     a constant load),
 *   - systemTime against timeGetTime() (the SDK requires systemTime to be
 *     derived from timeGetTime() on Windows).
 *
 * Usage: wine asio_loopback.exe <arm-file as a Windows path> [measurements]
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -o asio_loopback64.exe asio_loopback.c -lole32 -luuid -lwinmm
 *        i686-w64-mingw32-gcc   ... (32-bit: the ASIO methods are thiscall there)
 * Driven by run.sh. */

#include <windows.h>
#include <ole2.h>
#include <stdlib.h>
#include "lb_common.h"

typedef long ASIOBool;
typedef long ASIOError;
typedef long ASIOSampleType;
typedef struct { unsigned long hi, lo; } ASIOSamples;
typedef struct { unsigned long hi, lo; } ASIOTimeStamp;

typedef struct { ASIOBool isInput; long channelNum; void *buffers[2]; } ASIOBufferInfo;
typedef struct { long channel; ASIOBool isInput; ASIOBool isActive; long channelGroup;
                 ASIOSampleType type; char name[32]; } ASIOChannelInfo;
typedef struct { double speed; ASIOTimeStamp systemTime; ASIOSamples samplePosition;
                 double sampleRate; unsigned long flags; char reserved[12]; } AsioTimeInfo;
typedef struct { double speed; ASIOSamples timeCodeSamples; unsigned long flags;
                 char future[64]; } ASIOTimeCode;
typedef struct { long reserved[4]; AsioTimeInfo timeInfo; ASIOTimeCode timeCode; } ASIOTime;
typedef struct {
    void (*bufferSwitch)(long index, ASIOBool directProcess);
    void (*sampleRateDidChange)(double rate);
    long (*asioMessage)(long selector, long value, void *message, double *opt);
    ASIOTime *(*bufferSwitchTimeInfo)(ASIOTime *params, long index, ASIOBool directProcess);
} ASIOCallbacks;

#define ASIOSTFloat32LSB       19
#define kAsioSelectorSupported 1
#define kAsioEngineVersion     2
#define kAsioResetRequest      3
#define kAsioBufferSizeChange  4
#define kAsioResyncRequest     5
#define kAsioLatenciesChanged  6
#define kAsioSupportsTimeInfo  7

/* 32-bit: IASIO is a C++ interface, its methods are thiscall; only the
 * IUnknown methods are stdcall. 64-bit has a single convention. */
#ifdef __i386__
#define ASIOMETHOD __attribute__((thiscall))
#else
#define ASIOMETHOD
#endif

typedef struct IASIO IASIO;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IASIO *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IASIO *);
    ULONG (STDMETHODCALLTYPE *Release)(IASIO *);
    ASIOBool (ASIOMETHOD *Init)(IASIO *, void *sysRef);
    void (ASIOMETHOD *GetDriverName)(IASIO *, char *name);
    long (ASIOMETHOD *GetDriverVersion)(IASIO *);
    void (ASIOMETHOD *GetErrorMessage)(IASIO *, char *string);
    ASIOError (ASIOMETHOD *Start)(IASIO *);
    ASIOError (ASIOMETHOD *Stop)(IASIO *);
    ASIOError (ASIOMETHOD *GetChannels)(IASIO *, long *in, long *out);
    ASIOError (ASIOMETHOD *GetLatencies)(IASIO *, long *in, long *out);
    ASIOError (ASIOMETHOD *GetBufferSize)(IASIO *, long *min, long *max, long *pref, long *gran);
    ASIOError (ASIOMETHOD *CanSampleRate)(IASIO *, double rate);
    ASIOError (ASIOMETHOD *GetSampleRate)(IASIO *, double *rate);
    ASIOError (ASIOMETHOD *SetSampleRate)(IASIO *, double rate);
    ASIOError (ASIOMETHOD *GetClockSources)(IASIO *, void *clocks, long *num);
    ASIOError (ASIOMETHOD *SetClockSource)(IASIO *, long ref);
    ASIOError (ASIOMETHOD *GetSamplePosition)(IASIO *, ASIOSamples *pos, ASIOTimeStamp *ts);
    ASIOError (ASIOMETHOD *GetChannelInfo)(IASIO *, ASIOChannelInfo *info);
    ASIOError (ASIOMETHOD *CreateBuffers)(IASIO *, ASIOBufferInfo *infos, long num,
                                                 long size, ASIOCallbacks *cb);
    ASIOError (ASIOMETHOD *DisposeBuffers)(IASIO *);
    ASIOError (ASIOMETHOD *ControlPanel)(IASIO *);
    ASIOError (ASIOMETHOD *Future)(IASIO *, long selector, void *opt);
    ASIOError (ASIOMETHOD *OutputReady)(IASIO *);
} IASIOVtbl;
struct IASIO { const IASIOVtbl *lpVtbl; };

/* WineASIO {48D0C522-BFCC-45CC-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = { 0x48d0c522, 0xbfcc, 0x45cc,
                                     { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 } };
static const GUID IID_IUnknown_L = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };

static ASIOBufferInfo bufinfo[2];   /* [0] = input 0, [1] = output 0 */
static long long last_pos = -1;
static long timeinfo_calls, plain_calls, gaps, gap_cycles, resets, latency_changes;
static IASIO *g_asio;
static long burn_us, burn_every, burn_calls;
static LARGE_INTEGER qpf;

static void burn(void)
{
    LARGE_INTEGER t0, t;
    if (burn_us <= 0) return;
    if (burn_every > 1 && ++burn_calls % burn_every) return;
    QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t); while ((t.QuadPart - t0.QuadPart) * 1000000 / qpf.QuadPart < burn_us);
}
static double systime_min = 1e300, systime_max = -1e300;
static long systime_n;

/* systemTime (ns) against timeGetTime() (ms), counted only after arming. */
static void note_systime(unsigned long hi, unsigned long lo)
{
    long long ns = ((long long)hi << 32) | (unsigned long long)lo;
    double off = ns / 1e6 - (double)timeGetTime();
    if (!lb_armed) return;
    if (off < systime_min) systime_min = off;
    if (off > systime_max) systime_max = off;
    systime_n++;
}

static void cb_switch(long index, ASIOBool direct)
{
    ASIOSamples pos;
    ASIOTimeStamp ts;
    (void)direct;
    plain_calls++;
    if (g_asio->lpVtbl->GetSamplePosition(g_asio, &pos, &ts) == 0) note_systime(ts.hi, ts.lo);
    burn();
    lb_process(bufinfo[0].buffers[index], bufinfo[1].buffers[index], lb_bufsize);
}

static ASIOTime *cb_switch_ti(ASIOTime *t, long index, ASIOBool direct)
{
    long long pos = ((long long)t->timeInfo.samplePosition.hi << 32)
                    | (unsigned long long)t->timeInfo.samplePosition.lo;
    (void)direct;
    if (last_pos >= 0 && pos - last_pos != lb_bufsize)
    {
        gaps++;
        if (pos - last_pos > lb_bufsize) gap_cycles += (pos - last_pos) / lb_bufsize - 1;
    }
    last_pos = pos;
    timeinfo_calls++;
    note_systime(t->timeInfo.systemTime.hi, t->timeInfo.systemTime.lo);
    burn();
    lb_process(bufinfo[0].buffers[index], bufinfo[1].buffers[index], lb_bufsize);
    return t;
}

static void cb_rate(double rate) { printf("INFO sampleRateDidChange %.0f\n", rate); }

static long cb_message(long selector, long value, void *message, double *opt)
{
    (void)message; (void)opt;
    switch (selector)
    {
    case kAsioSelectorSupported:
        return value == kAsioEngineVersion || value == kAsioResetRequest
            || value == kAsioBufferSizeChange || value == kAsioResyncRequest
            || value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo;
    case kAsioEngineVersion:    return 2;
    case kAsioSupportsTimeInfo: return 1;
    case kAsioResetRequest:     resets++; return 1;
    case kAsioLatenciesChanged: latency_changes++; return 1;
    case kAsioBufferSizeChange:
    case kAsioResyncRequest:    return 1;
    }
    return 0;
}

static ASIOCallbacks callbacks = { cb_switch, cb_rate, cb_message, cb_switch_ti };

int main(int argc, char **argv)
{
    IASIO *asio = NULL;
    char name[64] = "";
    long nin, nout, bmin, bmax, bpref, bgran, lin, lout;
    double rate = 0;
    ASIOChannelInfo ci;
    HRESULT hr;
    int waited, k;

    if (argc < 2) { fprintf(stderr, "Usage: %s <arm-file> [measurements]\n", argv[0]); return 2; }
    if (argc > 2) lb_nmeas = atoi(argv[2]);
    if (lb_nmeas < 1 || lb_nmeas > LB_MAX_MEAS) lb_nmeas = 50;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("LB_BURN_US")) burn_us = atol(getenv("LB_BURN_US"));
    if (getenv("LB_BURN_EVERY")) burn_every = atol(getenv("LB_BURN_EVERY"));
    QueryPerformanceFrequency(&qpf);

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown_L, (void **)&asio);
    if (FAILED(hr) || !asio) { printf("ERROR CoCreateInstance 0x%08lx\n", hr); return 1; }
    g_asio = asio;
    if (!asio->lpVtbl->Init(asio, NULL)) { printf("ERROR Init\n"); return 1; }
    asio->lpVtbl->GetDriverName(asio, name);
    asio->lpVtbl->GetChannels(asio, &nin, &nout);
    asio->lpVtbl->GetBufferSize(asio, &bmin, &bmax, &bpref, &bgran);
    asio->lpVtbl->GetSampleRate(asio, &rate);
    if (burn_us) printf("INFO burn_us=%ld every=%ld\n", burn_us, burn_every > 1 ? burn_every : 1);
    printf("INFO driver=%s version=%ld in=%ld out=%ld buf=min%ld/max%ld/pref%ld/gran%ld rate=%.0f\n",
           name, asio->lpVtbl->GetDriverVersion(asio), nin, nout, bmin, bmax, bpref, bgran, rate);
    lb_bufsize = bpref;

    bufinfo[0].isInput = 1; bufinfo[0].channelNum = 0;
    bufinfo[1].isInput = 0; bufinfo[1].channelNum = 0;
    if (asio->lpVtbl->CreateBuffers(asio, bufinfo, 2, lb_bufsize, &callbacks) != 0)
    { printf("ERROR CreateBuffers\n"); return 1; }
    for (k = 0; k < 2; k++)
    {
        memset(&ci, 0, sizeof(ci));
        ci.channel = 0; ci.isInput = k == 0;
        asio->lpVtbl->GetChannelInfo(asio, &ci);
        printf("INFO channel %s type=%ld name=%s\n", k == 0 ? "in" : "out", ci.type, ci.name);
        if (ci.type != ASIOSTFloat32LSB) { printf("ERROR sample type %ld != Float32LSB\n", ci.type); return 1; }
    }
    lin = lout = -1;
    asio->lpVtbl->GetLatencies(asio, &lin, &lout);
    printf("GETLAT before_start in=%ld out=%ld\n", lin, lout);

    if (asio->lpVtbl->Start(asio) != 0) { printf("ERROR Start\n"); return 1; }
    Sleep(200);   /* wait for the autoconnect and the first cycles */
    lin = lout = -1;
    asio->lpVtbl->GetLatencies(asio, &lin, &lout);
    printf("GETLAT hardware_wired in=%ld out=%ld\n", lin, lout);
    printf("READY\n");

    for (waited = 0; GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES; waited += 50)
    {
        if (waited > 30000) { printf("NOT_ARMED\n"); break; }
        Sleep(50);
    }
    if (waited <= 30000)
    {
        lin = lout = -1;
        asio->lpVtbl->GetLatencies(asio, &lin, &lout);
        printf("GETLAT loop_wired in=%ld out=%ld\n", lin, lout);
        lb_armed = 1;
        for (waited = 0; !lb_done && waited < 20000; waited += 20) Sleep(20);
    }

    asio->lpVtbl->Stop(asio);
    Sleep(50);
    lb_report("wineasio", rate);
    printf("CYCLES timeinfo_calls=%ld plain_calls=%ld gaps=%ld gap_cycles=%ld resets=%ld latency_changes=%ld\n",
           timeinfo_calls, plain_calls, gaps, gap_cycles, resets, latency_changes);
    if (systime_n)
        printf("SYSTIME n=%ld offset_ms_min=%.3f offset_ms_max=%.3f (systemTime - timeGetTime)\n",
               systime_n, systime_min, systime_max);
    asio->lpVtbl->DisposeBuffers(asio);
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    return lb_meas ? 0 : 1;
}
