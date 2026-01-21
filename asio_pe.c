/*
 * WineASIO PE Side - Windows ASIO Interface
 * Copyright (C) 2006 Robert Reif
 * Portions copyright (C) 2007-2013 Various contributors
 * Copyright (C) 2024 WineASIO contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#define COBJMACROS
#include <windows.h>

/* Earliest possible debug output - uses OutputDebugStringA which works before CRT init */
#define EARLY_DBG(msg) OutputDebugStringA("[WineASIO-EARLY] " msg "\n")
#include <objbase.h>
#include <olectl.h>
#include <mmsystem.h>
#include <winternl.h>

/* NtQueryVirtualMemory will be loaded dynamically to avoid stdcall decoration issues on 32-bit */
typedef NTSTATUS (WINAPI *NtQueryVirtualMemory_t)(HANDLE, LPCVOID, ULONG, PVOID, SIZE_T, SIZE_T*);
static NtQueryVirtualMemory_t pNtQueryVirtualMemory = NULL;

/* Wine unix call dispatcher - loaded dynamically to avoid import resolution issues on 32-bit WoW64 */
typedef NTSTATUS (WINAPI *wine_unix_call_dispatcher_t)(UINT64, unsigned int, void *);
static wine_unix_call_dispatcher_t p__wine_unix_call_dispatcher = NULL;

#include "unixlib.h"

/*
 * Thiscall wrapper macros for 32-bit ASIO compatibility
 * 
 * ASIO uses the thiscall calling convention on 32-bit Windows, where the
 * 'this' pointer is passed in ECX register instead of on the stack.
 * MSVC-compiled ASIO hosts (like REAPER, Cubase, etc.) expect this.
 * 
 * These wrappers convert thiscall to stdcall by:
 * 1. Popping the return address
 * 2. Pushing ECX (this pointer) onto the stack
 * 3. Pushing the return address back
 * 4. Jumping to the actual function
 *
 * For MinGW cross-compiler, we use naked functions with inline asm.
 * MinGW uses underscore prefix for C symbols on 32-bit Windows.
 */
#ifdef __i386__  /* thiscall wrappers are i386-specific */

/* MinGW uses different assembly syntax than Wine's winegcc */
#define THISCALL(func) __thiscall_ ## func

/* 
 * Define thiscall wrapper using naked function + inline asm
 * This works with MinGW cross-compiler
 * Note: MinGW adds underscore prefix to C symbols, so we use _func
 */
#define DEFINE_THISCALL_WRAPPER(func, args) \
    void __attribute__((naked)) __thiscall_ ## func(void) { \
        __asm__ __volatile__( \
            "popl %%eax\n\t" \
            "pushl %%ecx\n\t" \
            "pushl %%eax\n\t" \
            "jmp _" #func "\n\t" \
            ::: "memory" \
        ); \
    }

#else /* __i386__ */

/* On 64-bit, thiscall is the same as the standard calling convention */
#define THISCALL(func) func
#define DEFINE_THISCALL_WRAPPER(func,args) /* nothing */

#endif /* __i386__ */

/* NTSTATUS definitions for mingw if not available */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

/* Direct stderr debug output - always visible regardless of WINEDEBUG settings */
#define DBG_STDERR(fmt, ...) do { fprintf(stderr, "[WineASIO-DBG] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

/* Debug macros - replace Wine's debug system with simple printf for PE side */
#ifdef WINEASIO_DEBUG
#define TRACE(fmt, ...) do { fprintf(stderr, "wineasio:trace: " fmt, ##__VA_ARGS__); } while(0)
#define WARN(fmt, ...) do { fprintf(stderr, "wineasio:warn: " fmt, ##__VA_ARGS__); } while(0)
#define ERR(fmt, ...) do { fprintf(stderr, "wineasio:err: " fmt, ##__VA_ARGS__); } while(0)
#else
#define TRACE(fmt, ...) do { } while(0)
#define WARN(fmt, ...) do { } while(0)
#define ERR(fmt, ...) do { fprintf(stderr, "wineasio:err: " fmt, ##__VA_ARGS__); } while(0)
#endif

/* 
 * Wine Unix Call interface for mingw
 * These symbols are imported directly from ntdll.dll using dllimport
 */
typedef UINT64 unixlib_handle_t;

/* Our handle - set during initialization */
static unixlib_handle_t wineasio_unix_handle = 0;

/* Wrapper for unix calls - uses dynamically loaded dispatcher */
static inline NTSTATUS wine_unix_call(unixlib_handle_t handle, unsigned int code, void *args)
{
    if (!p__wine_unix_call_dispatcher) {
        ERR("Unix call dispatcher not available!\n");
        return STATUS_UNSUCCESSFUL;
    }
    return p__wine_unix_call_dispatcher(handle, code, args);
}

/* GUID to string helper - replacement for wine_dbgstr_guid */
static const char *debugstr_guid(const GUID *guid)
{
    static char buf[64];
    if (!guid) return "(null)";
    snprintf(buf, sizeof(buf), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             (unsigned long)guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
             guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return buf;
}
#define wine_dbgstr_guid debugstr_guid

/* Initialize Wine unix call interface - must be called from DllMain */
static BOOL init_wine_unix_call(HINSTANCE hInstDLL)
{
    NTSTATUS status;
    HMODULE hNtdll;
    wine_unix_call_dispatcher_t *dispatcher_ptr;
    
    DBG_STDERR("init_wine_unix_call: starting initialization");
    
    /* Load ntdll functions dynamically to avoid import resolution issues on 32-bit WoW64 */
    hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) {
        ERR("Failed to get ntdll.dll handle\n");
        DBG_STDERR("init_wine_unix_call: FAILED - no ntdll.dll handle");
        return FALSE;
    }
    DBG_STDERR("init_wine_unix_call: got ntdll handle %p", hNtdll);
    
    /* Load __wine_unix_call_dispatcher - it's exported as DATA (a pointer to the dispatcher)
     * GetProcAddress returns the address of the exported variable, which contains the function pointer */
    dispatcher_ptr = (wine_unix_call_dispatcher_t *)GetProcAddress(hNtdll, "__wine_unix_call_dispatcher");
    if (!dispatcher_ptr) {
        ERR("Failed to get __wine_unix_call_dispatcher from ntdll - not running under Wine?\n");
        DBG_STDERR("init_wine_unix_call: FAILED - __wine_unix_call_dispatcher not found");
        return FALSE;
    }
    DBG_STDERR("init_wine_unix_call: dispatcher_ptr = %p", dispatcher_ptr);
    
    /* Dereference to get the actual function pointer */
    p__wine_unix_call_dispatcher = *dispatcher_ptr;
    if (!p__wine_unix_call_dispatcher) {
        ERR("Wine unix call dispatcher is NULL - unix side not loaded?\n");
        DBG_STDERR("init_wine_unix_call: FAILED - dispatcher is NULL");
        return FALSE;
    }
    DBG_STDERR("init_wine_unix_call: p__wine_unix_call_dispatcher = %p", p__wine_unix_call_dispatcher);
    
    /* Load NtQueryVirtualMemory dynamically to avoid stdcall decoration issues on 32-bit */
    pNtQueryVirtualMemory = (NtQueryVirtualMemory_t)GetProcAddress(hNtdll, "NtQueryVirtualMemory");
    if (!pNtQueryVirtualMemory) {
        ERR("Failed to get NtQueryVirtualMemory from ntdll\n");
        DBG_STDERR("init_wine_unix_call: FAILED - NtQueryVirtualMemory not found");
        return FALSE;
    }
    DBG_STDERR("init_wine_unix_call: pNtQueryVirtualMemory = %p", pNtQueryVirtualMemory);
    
    /* Get our unix library handle using NtQueryVirtualMemory with MemoryWineUnixFuncs = 1000 
     * Wine's loader should have already loaded the unix .so for this builtin DLL */
    status = pNtQueryVirtualMemory(GetCurrentProcess(), hInstDLL, 
                                   1000 /* MemoryWineUnixFuncs */, 
                                   &wineasio_unix_handle, 
                                   sizeof(wineasio_unix_handle), NULL);
    if (status) {
        ERR("Failed to get unix library handle, status 0x%lx\n", (unsigned long)status);
        DBG_STDERR("init_wine_unix_call: FAILED - NtQueryVirtualMemory returned 0x%lx", (unsigned long)status);
#ifdef _WIN64
        ERR("Make sure wineasio64.so is in the Wine unix library path\n");
#else
        ERR("Make sure wineasio.so is in the Wine unix library path\n");
#endif
        return FALSE;
    }
    
    DBG_STDERR("init_wine_unix_call: SUCCESS - unix handle = 0x%llx", (unsigned long long)wineasio_unix_handle);
    TRACE("Wine unix call interface initialized, handle=%llx\n", 
          (unsigned long long)wineasio_unix_handle);
    return TRUE;
}

#define WINEASIO_VERSION 13  /* 1.3 */

/* ASIO type definitions */
typedef struct {
    LONG hi;
    LONG lo;
} ASIOSamples;

typedef struct {
    LONG hi;
    LONG lo;
} ASIOTimeStamp;

typedef struct {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samples;
    ASIOTimeStamp tcTimeCode;
    LONG flags;
    char future[64];
} ASIOTimeCode;

typedef struct {
    LONG isInput;
    LONG channelNum;
    void *buffers[2];
} ASIOBufferInfo;

typedef struct {
    LONG channel;
    LONG isInput;
    LONG isActive;
    LONG channelGroup;
    LONG type;
    char name[32];
} ASIOChannelInfo;

typedef struct {
    ASIOTimeCode timeCode;
    ASIOSamples timeInfo;
    ASIOTimeStamp systemTime;
    double sampleRate;
    LONG flags;
    char reserved[12];
} ASIOTime;

typedef struct {
    void (*bufferSwitch)(LONG bufferIndex, LONG directProcess);
    void (*sampleRateDidChange)(double sRate);
    LONG (*asioMessage)(LONG selector, LONG value, void *message, double *opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime *params, LONG bufferIndex, LONG directProcess);
} ASIOCallbacks;

/* Forward declarations */
struct IWineASIO;
typedef struct IWineASIO IWineASIO;
typedef IWineASIO *LPWINEASIO;

/* ASIO interface - uses COM-like structure */
typedef struct IWineASIOVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(LPWINEASIO iface, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(LPWINEASIO iface);
    ULONG (STDMETHODCALLTYPE *Release)(LPWINEASIO iface);
    /* IASIO */
    LONG (STDMETHODCALLTYPE *Init)(LPWINEASIO iface, void *sysRef);
    void (STDMETHODCALLTYPE *GetDriverName)(LPWINEASIO iface, char *name);
    LONG (STDMETHODCALLTYPE *GetDriverVersion)(LPWINEASIO iface);
    void (STDMETHODCALLTYPE *GetErrorMessage)(LPWINEASIO iface, char *string);
    LONG (STDMETHODCALLTYPE *Start)(LPWINEASIO iface);
    LONG (STDMETHODCALLTYPE *Stop)(LPWINEASIO iface);
    LONG (STDMETHODCALLTYPE *GetChannels)(LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels);
    LONG (STDMETHODCALLTYPE *GetLatencies)(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency);
    LONG (STDMETHODCALLTYPE *GetBufferSize)(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
    LONG (STDMETHODCALLTYPE *CanSampleRate)(LPWINEASIO iface, double sampleRate);
    LONG (STDMETHODCALLTYPE *GetSampleRate)(LPWINEASIO iface, double *currentRate);
    LONG (STDMETHODCALLTYPE *SetSampleRate)(LPWINEASIO iface, double sampleRate);
    LONG (STDMETHODCALLTYPE *GetClockSources)(LPWINEASIO iface, void *clocks, LONG *numSources);
    LONG (STDMETHODCALLTYPE *SetClockSource)(LPWINEASIO iface, LONG reference);
    LONG (STDMETHODCALLTYPE *GetSamplePosition)(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp);
    LONG (STDMETHODCALLTYPE *GetChannelInfo)(LPWINEASIO iface, ASIOChannelInfo *info);
    LONG (STDMETHODCALLTYPE *CreateBuffers)(LPWINEASIO iface, ASIOBufferInfo *bufferInfos, LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks);
    LONG (STDMETHODCALLTYPE *DisposeBuffers)(LPWINEASIO iface);
    LONG (STDMETHODCALLTYPE *ControlPanel)(LPWINEASIO iface);
    LONG (STDMETHODCALLTYPE *Future)(LPWINEASIO iface, LONG selector, void *params);
    LONG (STDMETHODCALLTYPE *OutputReady)(LPWINEASIO iface);
} IWineASIOVtbl;

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
    LONG ref;
    
    /* Unix handle */
    asio_handle handle;
    
    /* Host callbacks */
    ASIOCallbacks *callbacks;
    BOOL time_info_mode;
    BOOL can_time_code;
    
    /* State */
    LONG num_inputs;
    LONG num_outputs;
    double sample_rate;
    LONG buffer_size;
    
    /* Callback thread */
    HANDLE callback_thread;
    BOOL stop_callback_thread;
    ASIOTime host_time;
    
    /* Configuration */
    struct asio_config config;
};

/* {48D0C522-BFCC-45cc-8B84-17F25F33E6E8} */
static const GUID CLSID_WineASIO = {
    0x48d0c522, 0xbfcc, 0x45cc, { 0x8b, 0x84, 0x17, 0xf2, 0x5f, 0x33, 0xe6, 0xe8 }
};

#define UNIX_CALL(func, params) wine_unix_call(wineasio_unix_handle, unix_##func, params)

/* Read configuration from registry */
static void read_config(IWineASIO *This)
{
    HKEY hkey;
    DWORD type, size, value;
    char str_value[256];
    
    /* Set defaults */
    This->config.num_inputs = 16;
    This->config.num_outputs = 16;
    This->config.preferred_bufsize = 1024;
    This->config.fixed_bufsize = FALSE;
    This->config.autoconnect = TRUE;
    strcpy(This->config.client_name, "WineASIO");
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\WineASIO", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        size = sizeof(value);
        if (RegQueryValueExA(hkey, "Number of inputs", NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
            This->config.num_inputs = value;
        
        size = sizeof(value);
        if (RegQueryValueExA(hkey, "Number of outputs", NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
            This->config.num_outputs = value;
        
        size = sizeof(value);
        if (RegQueryValueExA(hkey, "Preferred buffersize", NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
            This->config.preferred_bufsize = value;
        
        size = sizeof(value);
        if (RegQueryValueExA(hkey, "Fixed buffersize", NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
            This->config.fixed_bufsize = value ? TRUE : FALSE;
        
        size = sizeof(value);
        if (RegQueryValueExA(hkey, "Connect to hardware", NULL, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
            This->config.autoconnect = value ? TRUE : FALSE;
        
        size = sizeof(str_value);
        if (RegQueryValueExA(hkey, "Client name", NULL, &type, (BYTE*)str_value, &size) == ERROR_SUCCESS && type == REG_SZ)
            strncpy(This->config.client_name, str_value, 63);
        
        RegCloseKey(hkey);
    }
    
    TRACE("Config: inputs=%d outputs=%d bufsize=%d fixed=%d autoconnect=%d name=%s\n",
          This->config.num_inputs, This->config.num_outputs, This->config.preferred_bufsize,
          This->config.fixed_bufsize, This->config.autoconnect, This->config.client_name);
}

/* Callback polling thread - polls Unix side for buffer switches */
static DWORD WINAPI callback_thread_proc(LPVOID arg)
{
    IWineASIO *This = (IWineASIO *)arg;
    struct asio_get_callback_params params;
    int loop_count = 0;
    
    DBG_STDERR(">>> Callback thread started, This=%p, handle=%llu", This, (unsigned long long)This->handle);
    TRACE("Callback thread started\n");
    
    while (!This->stop_callback_thread) {
        params.handle = This->handle;
        
        if (loop_count < 5) {
            DBG_STDERR("    Callback loop %d: calling UNIX_CALL(asio_get_callback)", loop_count);
        }
        
        UNIX_CALL(asio_get_callback, &params);
        
        if (loop_count < 5) {
            DBG_STDERR("    Callback loop %d: UNIX_CALL returned, result=%d, buffer_switch_ready=%d", 
                       loop_count, params.result, params.buffer_switch_ready);
            loop_count++;
        }
        
        if (params.result == ASE_OK && params.buffer_switch_ready && This->callbacks) {
            /* Handle sample rate change */
            if (params.sample_rate_changed) {
                TRACE("Sample rate changed to %f\n", params.new_sample_rate);
                This->sample_rate = params.new_sample_rate;
                This->callbacks->sampleRateDidChange(params.new_sample_rate);
            }
            
            /* Handle reset request */
            if (params.reset_request) {
                TRACE("Reset requested\n");
                This->callbacks->asioMessage(1 /* kAsioSelectorSupported */, 3 /* kAsioResetRequest */, NULL, NULL);
                This->callbacks->asioMessage(3 /* kAsioResetRequest */, 0, NULL, NULL);
            }
            
            /* Handle latency change */
            if (params.latency_changed) {
                TRACE("Latency changed\n");
                This->callbacks->asioMessage(1, 6 /* kAsioLatenciesChanged */, NULL, NULL);
                This->callbacks->asioMessage(6, 0, NULL, NULL);
            }
            
            /* Buffer switch */
            if (This->time_info_mode) {
                /* Use time info mode */
                DBG_STDERR("    Calling bufferSwitchTimeInfo (time_info_mode=1), buffer_index=%d", params.buffer_index);
                This->host_time.timeInfo.hi = (LONG)(params.time_info.sample_position >> 32);
                This->host_time.timeInfo.lo = (LONG)(params.time_info.sample_position & 0xFFFFFFFF);
                This->host_time.systemTime.hi = (LONG)(params.time_info.system_time >> 32);
                This->host_time.systemTime.lo = (LONG)(params.time_info.system_time & 0xFFFFFFFF);
                This->host_time.sampleRate = params.time_info.sample_rate;
                This->host_time.flags = params.time_info.flags;
                
                DBG_STDERR("    bufferSwitchTimeInfo callback=%p", This->callbacks->bufferSwitchTimeInfo);
                This->callbacks->bufferSwitchTimeInfo(&This->host_time, params.buffer_index, params.direct_process);
                DBG_STDERR("    bufferSwitchTimeInfo returned OK");
            } else {
                /* Use simple buffer switch */
                DBG_STDERR("    Calling bufferSwitch (time_info_mode=0), buffer_index=%d, callbacks=%p", 
                           params.buffer_index, This->callbacks);
                DBG_STDERR("    bufferSwitch callback=%p", This->callbacks->bufferSwitch);
                This->callbacks->bufferSwitch(params.buffer_index, params.direct_process);
                DBG_STDERR("    bufferSwitch returned OK");
            }
        }
        
        /* Small sleep to avoid busy waiting - 1ms */
        Sleep(1);
    }
    
    DBG_STDERR(">>> Callback thread stopping");
    TRACE("Callback thread stopped\n");
    return 0;
}

/* IUnknown methods */
static HRESULT STDMETHODCALLTYPE QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject)
{
    TRACE("iface=%p riid=%s\n", iface, wine_dbgstr_guid(riid));
    
    if (!ppvObject)
        return E_POINTER;
    
    if (IsEqualGUID(riid, &IID_IUnknown)) {
        *ppvObject = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE AddRef(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("iface=%p ref=%u\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE Release(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    ULONG ref = InterlockedDecrement(&This->ref);
    
    TRACE("iface=%p ref=%u\n", iface, ref);
    
    if (ref == 0) {
        /* Stop callback thread */
        if (This->callback_thread) {
            This->stop_callback_thread = TRUE;
            WaitForSingleObject(This->callback_thread, 5000);
            CloseHandle(This->callback_thread);
        }
        
        /* Close Unix side */
        if (This->handle) {
            struct asio_exit_params params = { .handle = This->handle };
            UNIX_CALL(asio_exit, &params);
        }
        
        HeapFree(GetProcessHeap(), 0, This);
    }
    
    return ref;
}

/* IASIO methods */
LONG STDMETHODCALLTYPE Init(LPWINEASIO iface, void *sysRef)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_init_params params;
    
    DBG_STDERR(">>> Init(iface=%p, sysRef=%p)", iface, sysRef);
    WARN(">>> CALLED: Init(iface=%p, sysRef=%p)\n", iface, sysRef);
    TRACE("iface=%p sysRef=%p\n", iface, sysRef);
    
    /* Read config from registry */
    read_config(This);
    
    /* Initialize Unix side */
    memset(&params, 0, sizeof(params));
    params.config = This->config;
    
    UNIX_CALL(asio_init, &params);
    
    if (params.result != ASE_OK) {
        DBG_STDERR("<<< Init FAILED: Unix init returned %d", params.result);
        WARN("Unix init failed: %d\n", params.result);
        return 0;  /* ASIO Init returns 0 on failure */
    }
    
    This->handle = params.handle;
    This->num_inputs = params.input_channels;
    This->num_outputs = params.output_channels;
    This->sample_rate = params.sample_rate;
    
    TRACE("Initialized: handle=%llu inputs=%d outputs=%d rate=%f\n",
          (unsigned long long)This->handle, This->num_inputs, This->num_outputs, This->sample_rate);
    
    DBG_STDERR("<<< Init returning SUCCESS (1), handle=%llu, inputs=%d, outputs=%d, rate=%f",
               (unsigned long long)This->handle, This->num_inputs, This->num_outputs, This->sample_rate);
    WARN("<<< RETURNING from Init: SUCCESS (1)\n");
    return 1;  /* Success */
}

void STDMETHODCALLTYPE GetDriverName(LPWINEASIO iface, char *name)
{
    WARN(">>> CALLED: GetDriverName(iface=%p, name=%p)\n", iface, name);
    TRACE("iface=%p name=%p\n", iface, name);
    strcpy(name, "WineASIO");
    WARN("<<< RETURNING from GetDriverName\n");
}

LONG STDMETHODCALLTYPE GetDriverVersion(LPWINEASIO iface)
{
    WARN(">>> CALLED: GetDriverVersion(iface=%p)\n", iface);
    TRACE("iface=%p\n", iface);
    WARN("<<< RETURNING from GetDriverVersion: %d\n", WINEASIO_VERSION);
    return WINEASIO_VERSION;
}

void STDMETHODCALLTYPE GetErrorMessage(LPWINEASIO iface, char *string)
{
    WARN(">>> CALLED: GetErrorMessage(iface=%p, string=%p)\n", iface, string);
    TRACE("iface=%p string=%p\n", iface, string);
    strcpy(string, "No error");
    WARN("<<< RETURNING from GetErrorMessage\n");
}

LONG STDMETHODCALLTYPE Start(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_start_params params = { .handle = This->handle };
    
    DBG_STDERR(">>> Start(iface=%p)", iface);
    WARN(">>> CALLED: Start(iface=%p)\n", iface);
    TRACE("iface=%p\n", iface);
    
    UNIX_CALL(asio_start, &params);
    
    if (params.result != ASE_OK) {
        WARN("Start failed: %d\n", params.result);
        return params.result;
    }
    
    /* Start callback polling thread */
    This->stop_callback_thread = FALSE;
    This->callback_thread = CreateThread(NULL, 0, callback_thread_proc, This, 0, NULL);
    
    /* Prime the first buffer */
    if (This->callbacks) {
        DBG_STDERR("    Priming first buffer, time_info_mode=%d", This->time_info_mode);
        if (This->time_info_mode) {
            DBG_STDERR("    Calling bufferSwitchTimeInfo(%p, 0, TRUE)", This->callbacks->bufferSwitchTimeInfo);
            memset(&This->host_time, 0, sizeof(This->host_time));
            This->host_time.sampleRate = This->sample_rate;
            This->host_time.flags = 0x7;
            This->callbacks->bufferSwitchTimeInfo(&This->host_time, 0, TRUE);
        } else {
            DBG_STDERR("    Calling bufferSwitch(%p, 0, TRUE)", This->callbacks->bufferSwitch);
            This->callbacks->bufferSwitch(0, TRUE);
        }
        DBG_STDERR("    Callback returned OK");
    }
    
    return ASE_OK;
}

LONG STDMETHODCALLTYPE Stop(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_stop_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: Stop(iface=%p)\n", iface);
    TRACE("iface=%p\n", iface);
    
    /* Stop callback thread */
    if (This->callback_thread) {
        This->stop_callback_thread = TRUE;
        WaitForSingleObject(This->callback_thread, 5000);
        CloseHandle(This->callback_thread);
        This->callback_thread = NULL;
    }
    
    UNIX_CALL(asio_stop, &params);
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetChannels(LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_channels_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: GetChannels(iface=%p, numInputChannels=%p, numOutputChannels=%p)\n", iface, numInputChannels, numOutputChannels);
    TRACE("iface=%p\n", iface);
    
    if (!numInputChannels || !numOutputChannels)
        return ASE_InvalidParameter;
    
    UNIX_CALL(asio_get_channels, &params);
    
    *numInputChannels = params.num_inputs;
    *numOutputChannels = params.num_outputs;
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_latencies_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: GetLatencies(iface=%p, inputLatency=%p, outputLatency=%p)\n", iface, inputLatency, outputLatency);
    TRACE("iface=%p\n", iface);
    
    if (!inputLatency || !outputLatency)
        return ASE_InvalidParameter;
    
    UNIX_CALL(asio_get_latencies, &params);
    
    *inputLatency = params.input_latency;
    *outputLatency = params.output_latency;
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_buffer_size_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: GetBufferSize(iface=%p, minSize=%p, maxSize=%p, preferredSize=%p, granularity=%p)\n", iface, minSize, maxSize, preferredSize, granularity);
    TRACE("iface=%p\n", iface);
    
    UNIX_CALL(asio_get_buffer_size, &params);
    
    if (minSize) *minSize = params.min_size;
    if (maxSize) *maxSize = params.max_size;
    if (preferredSize) *preferredSize = params.preferred_size;
    if (granularity) *granularity = params.granularity;
    
    return params.result;
}

LONG STDMETHODCALLTYPE CanSampleRate(LPWINEASIO iface, double sampleRate)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_can_sample_rate_params params = { .handle = This->handle, .sample_rate = sampleRate };
    
    WARN(">>> CALLED: CanSampleRate(iface=%p, rate=%f)\n", iface, sampleRate);
    TRACE("iface=%p rate=%f\n", iface, sampleRate);
    
    UNIX_CALL(asio_can_sample_rate, &params);
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetSampleRate(LPWINEASIO iface, double *currentRate)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_sample_rate_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: GetSampleRate(iface=%p, currentRate=%p)\n", iface, currentRate);
    TRACE("iface=%p\n", iface);
    
    if (!currentRate)
        return ASE_InvalidParameter;
    
    UNIX_CALL(asio_get_sample_rate, &params);
    
    *currentRate = params.sample_rate;
    This->sample_rate = params.sample_rate;
    
    return params.result;
}

LONG STDMETHODCALLTYPE SetSampleRate(LPWINEASIO iface, double sampleRate)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_set_sample_rate_params params = { .handle = This->handle, .sample_rate = sampleRate };
    
    WARN(">>> CALLED: SetSampleRate(iface=%p, rate=%f)\n", iface, sampleRate);
    TRACE("iface=%p rate=%f\n", iface, sampleRate);
    
    UNIX_CALL(asio_set_sample_rate, &params);
    
    if (params.result == ASE_OK)
        This->sample_rate = sampleRate;
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetClockSources(LPWINEASIO iface, void *clocks, LONG *numSources)
{
    WARN(">>> CALLED: GetClockSources(iface=%p, clocks=%p, numSources=%p)\n", iface, clocks, numSources);
    TRACE("iface=%p\n", iface);
    
    /* We only have one clock source - JACK */
    if (numSources)
        *numSources = 0;
    
    WARN("<<< RETURNING from GetClockSources: ASE_OK\n");
    return ASE_OK;
}

LONG STDMETHODCALLTYPE SetClockSource(LPWINEASIO iface, LONG reference)
{
    WARN(">>> CALLED: SetClockSource(iface=%p, reference=%d)\n", iface, reference);
    TRACE("iface=%p ref=%d\n", iface, reference);
    
    /* Only one clock source, ignore */
    WARN("<<< RETURNING from SetClockSource: ASE_OK\n");
    return ASE_OK;
}

LONG STDMETHODCALLTYPE GetSamplePosition(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_sample_position_params params = { .handle = This->handle };
    
    WARN(">>> CALLED: GetSamplePosition(iface=%p, sPos=%p, tStamp=%p)\n", iface, sPos, tStamp);
    
    if (!sPos || !tStamp)
        return ASE_InvalidParameter;
    
    UNIX_CALL(asio_get_sample_position, &params);
    
    sPos->hi = (LONG)(params.sample_position >> 32);
    sPos->lo = (LONG)(params.sample_position & 0xFFFFFFFF);
    tStamp->hi = (LONG)(params.system_time >> 32);
    tStamp->lo = (LONG)(params.system_time & 0xFFFFFFFF);
    
    return params.result;
}

LONG STDMETHODCALLTYPE GetChannelInfo(LPWINEASIO iface, ASIOChannelInfo *info)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_get_channel_info_params params;
    
    WARN(">>> CALLED: GetChannelInfo(iface=%p, info=%p)\n", iface, info);
    TRACE("iface=%p channel=%d isInput=%d\n", iface, info->channel, info->isInput);
    
    if (!info)
        return ASE_InvalidParameter;
    
    memset(&params, 0, sizeof(params));
    params.handle = This->handle;
    params.info.channel = info->channel;
    params.info.is_input = info->isInput;
    
    UNIX_CALL(asio_get_channel_info, &params);
    
    info->isActive = params.info.is_active;
    info->channelGroup = params.info.channel_group;
    info->type = params.info.sample_type;
    strncpy(info->name, params.info.name, 31);
    info->name[31] = 0;
    
    return params.result;
}

LONG STDMETHODCALLTYPE CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfos, LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_create_buffers_params params;
    struct asio_buffer_info *unix_infos;
    int i;
    
    DBG_STDERR(">>> CreateBuffers(iface=%p, bufferInfos=%p, numChannels=%d, bufferSize=%d, callbacks=%p)", iface, bufferInfos, numChannels, bufferSize, callbacks);
    if (callbacks) {
        DBG_STDERR("    callbacks->bufferSwitch=%p", callbacks->bufferSwitch);
        DBG_STDERR("    callbacks->sampleRateDidChange=%p", callbacks->sampleRateDidChange);
        DBG_STDERR("    callbacks->asioMessage=%p", callbacks->asioMessage);
        DBG_STDERR("    callbacks->bufferSwitchTimeInfo=%p", callbacks->bufferSwitchTimeInfo);
    }
    WARN(">>> CALLED: CreateBuffers(iface=%p, bufferInfos=%p, numChannels=%d, bufferSize=%d, callbacks=%p)\n", iface, bufferInfos, numChannels, bufferSize, callbacks);
    TRACE("iface=%p numChannels=%d bufferSize=%d\n", iface, numChannels, bufferSize);
    
    if (!bufferInfos || !callbacks || numChannels <= 0)
        return ASE_InvalidParameter;
    
    DBG_STDERR("    CreateBuffers step 1: storing callbacks");
    /* Store callbacks */
    This->callbacks = callbacks;
    This->buffer_size = bufferSize;
    
    DBG_STDERR("    CreateBuffers step 2: checking time info support");
    /* Check for time info support */
    This->time_info_mode = FALSE;
    This->can_time_code = FALSE;
    if (callbacks->asioMessage) {
        DBG_STDERR("    CreateBuffers step 2a: calling asioMessage(1, 14) for time info...");
        if (callbacks->asioMessage(1 /* kAsioSelectorSupported */, 14 /* kAsioSupportsTimeInfo */, NULL, NULL) == 1)
            This->time_info_mode = TRUE;
        DBG_STDERR("    CreateBuffers step 2b: calling asioMessage(1, 15) for time code...");
        if (callbacks->asioMessage(1, 15 /* kAsioSupportsTimeCode */, NULL, NULL) == 1)
            This->can_time_code = TRUE;
    }
    DBG_STDERR("    CreateBuffers step 2 done: time_info_mode=%d, can_time_code=%d", This->time_info_mode, This->can_time_code);
    
    TRACE("time_info_mode=%d can_time_code=%d\n", This->time_info_mode, This->can_time_code);
    
    DBG_STDERR("    CreateBuffers step 3: allocating unix_infos for %d channels", (int)numChannels);
    /* Prepare Unix call */
    unix_infos = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, numChannels * sizeof(*unix_infos));
    if (!unix_infos)
        return ASE_NoMemory;
    
    DBG_STDERR("    CreateBuffers step 4: copying buffer info");
    for (i = 0; i < numChannels; i++) {
        unix_infos[i].is_input = bufferInfos[i].isInput;
        unix_infos[i].channel_num = bufferInfos[i].channelNum;
    }
    
    DBG_STDERR("    CreateBuffers step 5: preparing UNIX_CALL params");
    memset(&params, 0, sizeof(params));
    params.handle = This->handle;
    params.num_channels = numChannels;
    params.buffer_size = bufferSize;
    params.buffer_infos = unix_infos;
    
    DBG_STDERR("    CreateBuffers step 6: calling UNIX_CALL(asio_create_buffers)...");
    UNIX_CALL(asio_create_buffers, &params);
    DBG_STDERR("    CreateBuffers step 7: UNIX_CALL returned, result=%d", (int)params.result);
    
    /* Copy buffer pointers back */
    if (params.result == ASE_OK) {
        for (i = 0; i < numChannels; i++) {
            bufferInfos[i].buffers[0] = (void *)(UINT_PTR)unix_infos[i].buffer_ptr[0];
            bufferInfos[i].buffers[1] = (void *)(UINT_PTR)unix_infos[i].buffer_ptr[1];
        }
    }
    
    HeapFree(GetProcessHeap(), 0, unix_infos);
    
    DBG_STDERR("<<< CreateBuffers returning %ld (ASE_OK=%d)", (long)params.result, ASE_OK);
    return params.result;
}

LONG STDMETHODCALLTYPE DisposeBuffers(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_dispose_buffers_params params = { .handle = This->handle };
    
    TRACE("iface=%p\n", iface);
    
    UNIX_CALL(asio_dispose_buffers, &params);
    
    This->callbacks = NULL;
    
    return params.result;
}

LONG STDMETHODCALLTYPE ControlPanel(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_control_panel_params params = { .handle = This->handle };
    
    TRACE("iface=%p\n", iface);
    
    UNIX_CALL(asio_control_panel, &params);
    
    return params.result;
}

LONG STDMETHODCALLTYPE Future(LPWINEASIO iface, LONG selector, void *opt)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_future_params params;
    
    TRACE("iface=%p selector=%d\n", iface, selector);
    
    memset(&params, 0, sizeof(params));
    params.handle = This->handle;
    params.selector = selector;
    params.opt = (UINT64)(UINT_PTR)opt;
    
    UNIX_CALL(asio_future, &params);
    
    return params.result;
}

LONG STDMETHODCALLTYPE OutputReady(LPWINEASIO iface)
{
    IWineASIO *This = (IWineASIO *)iface;
    struct asio_output_ready_params params = { .handle = This->handle };
    
    UNIX_CALL(asio_output_ready, &params);
    
    return params.result;
}

/*
 * Define thiscall wrappers for all ASIO methods (32-bit only)
 * IUnknown methods (QueryInterface, AddRef, Release) use stdcall, not thiscall
 * ASIO methods use thiscall on 32-bit Windows
 */
DEFINE_THISCALL_WRAPPER(Init, 8)
DEFINE_THISCALL_WRAPPER(GetDriverName, 8)
DEFINE_THISCALL_WRAPPER(GetDriverVersion, 4)
DEFINE_THISCALL_WRAPPER(GetErrorMessage, 8)
DEFINE_THISCALL_WRAPPER(Start, 4)
DEFINE_THISCALL_WRAPPER(Stop, 4)
DEFINE_THISCALL_WRAPPER(GetChannels, 12)
DEFINE_THISCALL_WRAPPER(GetLatencies, 12)
DEFINE_THISCALL_WRAPPER(GetBufferSize, 20)
DEFINE_THISCALL_WRAPPER(CanSampleRate, 12)
DEFINE_THISCALL_WRAPPER(GetSampleRate, 8)
DEFINE_THISCALL_WRAPPER(SetSampleRate, 12)
DEFINE_THISCALL_WRAPPER(GetClockSources, 12)
DEFINE_THISCALL_WRAPPER(SetClockSource, 8)
DEFINE_THISCALL_WRAPPER(GetSamplePosition, 12)
DEFINE_THISCALL_WRAPPER(GetChannelInfo, 8)
DEFINE_THISCALL_WRAPPER(CreateBuffers, 20)
DEFINE_THISCALL_WRAPPER(DisposeBuffers, 4)
DEFINE_THISCALL_WRAPPER(ControlPanel, 4)
DEFINE_THISCALL_WRAPPER(Future, 12)
DEFINE_THISCALL_WRAPPER(OutputReady, 4)

/* VTable - uses thiscall wrappers for ASIO methods on 32-bit */
static const IWineASIOVtbl WineASIO_Vtbl = {
    /* IUnknown methods - use stdcall (no wrapper needed) */
    QueryInterface,
    AddRef,
    Release,
    /* ASIO methods - use thiscall wrappers on 32-bit */
    (void*)THISCALL(Init),
    (void*)THISCALL(GetDriverName),
    (void*)THISCALL(GetDriverVersion),
    (void*)THISCALL(GetErrorMessage),
    (void*)THISCALL(Start),
    (void*)THISCALL(Stop),
    (void*)THISCALL(GetChannels),
    (void*)THISCALL(GetLatencies),
    (void*)THISCALL(GetBufferSize),
    (void*)THISCALL(CanSampleRate),
    (void*)THISCALL(GetSampleRate),
    (void*)THISCALL(SetSampleRate),
    (void*)THISCALL(GetClockSources),
    (void*)THISCALL(SetClockSource),
    (void*)THISCALL(GetSamplePosition),
    (void*)THISCALL(GetChannelInfo),
    (void*)THISCALL(CreateBuffers),
    (void*)THISCALL(DisposeBuffers),
    (void*)THISCALL(ControlPanel),
    (void*)THISCALL(Future),
    (void*)THISCALL(OutputReady)
};

/* Create WineASIO instance */
HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj)
{
    IWineASIO *pAsio;
    
    TRACE("riid=%s ppobj=%p\n", wine_dbgstr_guid(riid), ppobj);
    
    if (!ppobj)
        return E_POINTER;
    
    *ppobj = NULL;
    
    pAsio = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pAsio));
    if (!pAsio)
        return E_OUTOFMEMORY;
    
    pAsio->lpVtbl = &WineASIO_Vtbl;
    pAsio->ref = 1;
    
    /* DEBUG: Dump vtable function pointers */
    TRACE("=== VTABLE DUMP (pAsio=%p, lpVtbl=%p) ===\n", pAsio, pAsio->lpVtbl);
    TRACE("vtable[0x00] QueryInterface   = %p\n", (void*)WineASIO_Vtbl.QueryInterface);
    TRACE("vtable[0x04] AddRef            = %p\n", (void*)WineASIO_Vtbl.AddRef);
    TRACE("vtable[0x08] Release           = %p\n", (void*)WineASIO_Vtbl.Release);
    TRACE("vtable[0x0C] Init              = %p\n", (void*)WineASIO_Vtbl.Init);
    TRACE("vtable[0x10] GetDriverName     = %p\n", (void*)WineASIO_Vtbl.GetDriverName);
    TRACE("vtable[0x14] GetDriverVersion  = %p\n", (void*)WineASIO_Vtbl.GetDriverVersion);
    TRACE("vtable[0x18] GetErrorMessage   = %p\n", (void*)WineASIO_Vtbl.GetErrorMessage);
    TRACE("vtable[0x1C] Start             = %p\n", (void*)WineASIO_Vtbl.Start);
    TRACE("vtable[0x20] Stop              = %p\n", (void*)WineASIO_Vtbl.Stop);
    TRACE("vtable[0x24] GetChannels       = %p\n", (void*)WineASIO_Vtbl.GetChannels);
    TRACE("vtable[0x28] GetLatencies      = %p\n", (void*)WineASIO_Vtbl.GetLatencies);
    TRACE("vtable[0x2C] GetBufferSize     = %p\n", (void*)WineASIO_Vtbl.GetBufferSize);
    TRACE("vtable[0x30] CanSampleRate     = %p\n", (void*)WineASIO_Vtbl.CanSampleRate);
    TRACE("vtable[0x34] GetSampleRate     = %p\n", (void*)WineASIO_Vtbl.GetSampleRate);
    TRACE("vtable[0x38] SetSampleRate     = %p\n", (void*)WineASIO_Vtbl.SetSampleRate);
    TRACE("vtable[0x3C] GetClockSources   = %p\n", (void*)WineASIO_Vtbl.GetClockSources);
    TRACE("vtable[0x40] SetClockSource    = %p\n", (void*)WineASIO_Vtbl.SetClockSource);
    TRACE("vtable[0x44] GetSamplePosition = %p\n", (void*)WineASIO_Vtbl.GetSamplePosition);
    TRACE("vtable[0x48] GetChannelInfo    = %p\n", (void*)WineASIO_Vtbl.GetChannelInfo);
    TRACE("vtable[0x4C] CreateBuffers     = %p\n", (void*)WineASIO_Vtbl.CreateBuffers);
    TRACE("vtable[0x50] DisposeBuffers    = %p\n", (void*)WineASIO_Vtbl.DisposeBuffers);
    TRACE("vtable[0x54] ControlPanel      = %p\n", (void*)WineASIO_Vtbl.ControlPanel);
    TRACE("vtable[0x58] Future            = %p\n", (void*)WineASIO_Vtbl.Future);
    TRACE("vtable[0x5C] OutputReady       = %p\n", (void*)WineASIO_Vtbl.OutputReady);
    TRACE("=== END VTABLE DUMP ===\n");
    
    *ppobj = pAsio;
    return S_OK;
}

/*
 * ClassFactory
 */
typedef struct {
    const IClassFactoryVtbl *lpVtbl;
    LONG ref;
} IClassFactoryImpl;

static HRESULT WINAPI CF_QueryInterface(LPCLASSFACTORY iface, REFIID riid, LPVOID *ppobj)
{
    if (ppobj == NULL)
        return E_POINTER;
    *ppobj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI CF_AddRef(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI CF_Release(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    return InterlockedDecrement(&This->ref);
}

static HRESULT WINAPI CF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj)
{
    if (pOuter)
        return CLASS_E_NOAGGREGATION;
    
    if (ppobj == NULL)
        return E_INVALIDARG;
    
    *ppobj = NULL;
    return WineASIOCreateInstance(riid, ppobj);
}

static HRESULT WINAPI CF_LockServer(LPCLASSFACTORY iface, BOOL dolock)
{
    return S_OK;
}

static const IClassFactoryVtbl CF_Vtbl = {
    CF_QueryInterface,
    CF_AddRef,
    CF_Release,
    CF_CreateInstance,
    CF_LockServer
};

static IClassFactoryImpl WINEASIO_CF = { &CF_Vtbl, 1 };

/*
 * DLL exports
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    DBG_STDERR(">>> DllGetClassObject called");
    TRACE("rclsid=%s riid=%s ppv=%p\n", wine_dbgstr_guid(rclsid), wine_dbgstr_guid(riid), ppv);
    
    if (ppv == NULL)
        return E_INVALIDARG;
    
    *ppv = NULL;
    
    if (!IsEqualIID(riid, &IID_IClassFactory) && !IsEqualIID(riid, &IID_IUnknown))
        return E_NOINTERFACE;
    
    if (IsEqualGUID(rclsid, &CLSID_WineASIO)) {
        CF_AddRef((IClassFactory *)&WINEASIO_CF);
        *ppv = &WINEASIO_CF;
        return S_OK;
    }
    
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

/*
 * DllRegisterServer - Register the COM class and ASIO driver
 */
HRESULT WINAPI DllRegisterServer(void)
{
    HKEY hkey, hsubkey;
    LONG rc;
    char clsid_str[] = "{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}";
    char module_path[MAX_PATH];
    
#ifdef _WIN64
    const char *dll_name = "wineasio64.dll";
#else
    const char *dll_name = "wineasio.dll";
#endif
    
    TRACE("Registering WineASIO\n");
    
    /* Register COM class under HKEY_CLASSES_ROOT\CLSID\{...} */
    rc = RegCreateKeyExA(HKEY_CLASSES_ROOT, 
                         "CLSID\\{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}",
                         0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS) {
        ERR("Failed to create CLSID key: %ld\n", rc);
        return SELFREG_E_CLASS;
    }
    
    /* Set default value to class name */
    RegSetValueExA(hkey, NULL, 0, REG_SZ, (const BYTE*)"WineASIO Driver", 16);
    
    /* Create InprocServer32 subkey */
    rc = RegCreateKeyExA(hkey, "InprocServer32", 0, NULL, 0, KEY_WRITE, NULL, &hsubkey, NULL);
    if (rc == ERROR_SUCCESS) {
        /* Set default value to DLL path */
        GetSystemDirectoryA(module_path, MAX_PATH);
        strcat(module_path, "\\");
        strcat(module_path, dll_name);
        RegSetValueExA(hsubkey, NULL, 0, REG_SZ, (const BYTE*)module_path, strlen(module_path) + 1);
        
        /* Set threading model */
        RegSetValueExA(hsubkey, "ThreadingModel", 0, REG_SZ, (const BYTE*)"Apartment", 10);
        RegCloseKey(hsubkey);
    }
    RegCloseKey(hkey);
    
    /* Register ASIO driver under HKEY_LOCAL_MACHINE\Software\ASIO\WineASIO */
    rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE, "Software\\ASIO\\WineASIO",
                         0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
    if (rc == ERROR_SUCCESS) {
        RegSetValueExA(hkey, "CLSID", 0, REG_SZ, (const BYTE*)clsid_str, strlen(clsid_str) + 1);
        RegSetValueExA(hkey, "Description", 0, REG_SZ, (const BYTE*)"WineASIO Driver", 16);
        RegCloseKey(hkey);
    }
    
    TRACE("WineASIO registered successfully\n");
    return S_OK;
}

/*
 * DllUnregisterServer - Unregister the COM class and ASIO driver
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    TRACE("Unregistering WineASIO\n");
    
    /* Delete ASIO driver key */
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\ASIO\\WineASIO");
    
    /* Delete COM class registration */
    RegDeleteKeyA(HKEY_CLASSES_ROOT, "CLSID\\{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}\\InprocServer32");
    RegDeleteKeyA(HKEY_CLASSES_ROOT, "CLSID\\{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}");
    
    TRACE("WineASIO unregistered\n");
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    /* Very first thing - output debug before anything else */
    EARLY_DBG("DllMain entered");
    
    TRACE("hInstDLL=%p fdwReason=%lx lpvReserved=%p\n", hInstDLL, fdwReason, lpvReserved);
    DBG_STDERR("DllMain: hInstDLL=%p fdwReason=%lx", hInstDLL, fdwReason);
    
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        EARLY_DBG("DllMain: DLL_PROCESS_ATTACH");
        DBG_STDERR("DllMain: DLL_PROCESS_ATTACH - calling DisableThreadLibraryCalls");
        DisableThreadLibraryCalls(hInstDLL);
        DBG_STDERR("DllMain: calling init_wine_unix_call");
        if (!init_wine_unix_call(hInstDLL)) {
            ERR("Failed to load Unix library\n");
            DBG_STDERR("DllMain: init_wine_unix_call FAILED");
            return FALSE;
        }
        DBG_STDERR("DllMain: init_wine_unix_call succeeded");
        break;
    case DLL_PROCESS_DETACH:
        EARLY_DBG("DllMain: DLL_PROCESS_DETACH");
        break;
    }
    EARLY_DBG("DllMain returning TRUE");
    return TRUE;
}