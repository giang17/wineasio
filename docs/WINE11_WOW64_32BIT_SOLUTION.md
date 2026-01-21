# Wine 11 WoW64 32-bit WineASIO - SOLVED!

**Date**: 2026-01-21  
**Status**: ✅ Working

## The Problem

32-bit Windows ASIO applications (REAPER 32-bit, Garritan CFX Lite, etc.) were crashing or producing no audio when using WineASIO on Wine 11 with WoW64.

### Symptoms
- JACK client was created but no audio ports appeared in Patchance/qjackctl
- Crash on `CreateBuffers()` or `Start()`
- Warning messages: `audio_buffer=(nil)`
- No `[WineASIO-Unix]` debug messages visible

## Root Cause Discovery

### The Key Insight

**In Wine 11 WoW64, 32-bit PE DLLs use 64-bit Unix libraries!**

This is completely different from the traditional Wine architecture where 32-bit PE DLLs used 32-bit Unix libraries.

```
Wine 11 WoW64 Architecture:
┌─────────────────────────────────────────────────────────────┐
│ 32-bit PE DLL (wineasio.dll)                                │
│   - Runs in emulated 32-bit address space                   │
│   - Addresses: 0x00000000 - 0x7FFFFFFF                      │
└─────────────────────┬───────────────────────────────────────┘
                      │ __wine_unix_call()
                      │ (WoW64 thunking layer)
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ 64-bit Unix .so (wineasio.so in x86_64-unix/)               │
│   - Runs in native 64-bit address space                     │
│   - Full 64-bit address range                               │
│   - Communicates with JACK/PipeWire                         │
└─────────────────────────────────────────────────────────────┘
```

### What We Were Doing Wrong

1. **Building the Unix library as 32-bit** (`-m32`) - WRONG!
2. **Installing to `i386-unix/`** - WRONG!
3. **Allocating audio buffers on Unix side** - Address space mismatch!

### The Correct Approach

1. **Build Unix library as 64-bit** (`-m64`)
2. **Install to `x86_64-unix/`**
3. **Allocate audio buffers on PE side** (accessible from Windows apps)

## The Solution

### Step 1: Fix the Build System (Makefile.wine11)

```makefile
# 32-bit PE DLL - built with -m32
$(MINGW32) -m32 -o wineasio.dll ...

# Unix .so for 32-bit PE - MUST BE 64-bit!
$(GCC) -m64 -o wineasio.so ...

# Installation paths
install32:
    cp wineasio.dll $(WINE_PREFIX)/i386-windows/
    cp wineasio.so $(WINE_PREFIX)/x86_64-unix/  # NOT i386-unix!
```

### Step 2: PE-Side Buffer Allocation (asio_pe.c)

Audio buffers must be allocated on the PE (Windows) side because:
- 32-bit PE pointers (e.g., `0x00F85760`) are invalid in 64-bit Unix space
- 64-bit Unix pointers don't fit in 32-bit variables
- Windows applications need direct access to the buffers

```c
// In CreateBuffers():
// Allocate buffer memory on PE side
DWORD buffer_size = bufferSize * sizeof(float);
BYTE *buffer_block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 
                               numChannels * 2 * buffer_size);

// Pass PE pointers to Unix side as UINT64
for (int i = 0; i < numChannels; i++) {
    unix_infos[i].pe_buffer[0] = (UINT64)(buffer_block + offset);
    unix_infos[i].pe_buffer[1] = (UINT64)(buffer_block + offset + buffer_size);
}
```

### Step 3: Use PE Buffers in JACK Callback (asio_unix.c)

The JACK process callback must use the PE-allocated buffers:

```c
static int jack_process_callback(jack_nframes_t nframes, void *arg) {
    // For inputs: copy from JACK to PE buffer
    for (int i = 0; i < num_inputs; i++) {
        float *jack_buf = jack_port_get_buffer(input_ports[i], nframes);
        float *pe_buf = (float *)(ULONG_PTR)channels[i].pe_buffer[buffer_index];
        memcpy(pe_buf, jack_buf, nframes * sizeof(float));
    }
    
    // For outputs: copy from PE buffer to JACK
    for (int i = 0; i < num_outputs; i++) {
        float *jack_buf = jack_port_get_buffer(output_ports[i], nframes);
        float *pe_buf = (float *)(ULONG_PTR)channels[i].pe_buffer[buffer_index];
        memcpy(jack_buf, pe_buf, nframes * sizeof(float));
    }
}
```

## Verification

### Check Unix Library is 64-bit
```bash
file /usr/local/lib/wine/x86_64-unix/wineasio.so
# Should say: ELF 64-bit LSB shared object, x86-64
```

### Check PE DLL is 32-bit
```bash
file /usr/local/lib/wine/i386-windows/wineasio.dll
# Should say: PE32 executable
```

### Check Debug Output
When running a test, you should see Unix-side debug messages:
```
[WineASIO-Unix] *** Library loaded (constructor called) ***
[WineASIO-Unix] >>> asio_init() called
[WineASIO-Unix] Step 2: Opening JACK client 'WineASIO'...
```

And PE buffer usage in the JACK callback:
```
wineasio:trace: Copying input 0, pe_buffer[0]=0x1355cc0
wineasio:trace: Copying output 0, pe_buffer[0]=0x13564c0
```

If you see `audio_buffer=(nil)` warnings, the fix is not installed correctly.

## File Locations Summary

| Component | Build Flag | Install Location |
|-----------|-----------|------------------|
| `wineasio.dll` (32-bit PE) | `-m32` | `i386-windows/` |
| `wineasio.so` (for 32-bit PE) | **`-m64`** | **`x86_64-unix/`** |
| `wineasio64.dll` (64-bit PE) | `-m64` | `x86_64-windows/` |
| `wineasio64.so` (for 64-bit PE) | `-m64` | `x86_64-unix/` |

**Note**: There is NO `i386-unix/` directory in Wine 11 WoW64 builds!

## Tested Applications

| Application | Architecture | Status |
|-------------|--------------|--------|
| REAPER 32-bit | 32-bit PE | ✅ Working |
| Garritan CFX Lite | 32-bit PE | ✅ Working |
| test_asio_interactive.exe | 32-bit PE | ✅ Working |
| REAPER 64-bit | 64-bit PE | ✅ Working (was already working) |

## Quick Build & Install

```bash
cd /path/to/wineasio-fork

# Clean and build
make -f Makefile.wine11 clean
make -f Makefile.wine11 all

# Install (requires sudo)
sudo make -f Makefile.wine11 install

# Register in Wine
wine regsvr32 wineasio.dll
wine64 regsvr32 wineasio64.dll
```

## Lessons Learned

1. **Wine 11 WoW64 is fundamentally different** - Don't assume 32-bit PE uses 32-bit Unix
2. **Address spaces matter** - PE and Unix sides have incompatible pointer sizes
3. **Allocate shared memory on the right side** - Windows apps need PE-side buffers
4. **Debug output is essential** - Without `[WineASIO-Unix]` messages, you know the .so isn't loading
5. **Check file types** - Use `file` command to verify architectures

## References

- Wine PE/Unix Split: https://wiki.winehq.org/Dll_Separation
- WineASIO GitHub: https://github.com/wineasio/wineasio
- JACK Audio API: https://jackaudio.org/api/