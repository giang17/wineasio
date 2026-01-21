# WineASIO 32-bit Crash Analysis

## Executive Summary

WineASIO 32-bit crashes with a **NULL function pointer execution** (`page fault on execute access to 0x00000000`) when loaded by real-world ASIO hosts (REAPER, Garritan CFX Lite) in Wine WoW64 mode. The crash occurs **before DllMain is called**, despite extensive debug instrumentation.

**Critical Discovery**: The crash persists even after removing the "Wine builtin DLL" marker, indicating the root cause is **MinGW CRT initialization or TLS callbacks**, not Wine's builtin DLL handling.

---

## Timeline of Investigation

### Phase 1: Initial Crash Observation
- **Symptom**: Page fault at EIP=0x00000000 when hosts enumerate ASIO drivers
- **Environment**: Wine 11.0 WoW64 on Ubuntu 24.04 x86_64
- **Initial Theory**: Berkeley DB (libjack) issues → **DISPROVEN** (messages from JACK, not GTK/Wine)

### Phase 2: Builtin DLL Hypothesis
- **Observation**: PE file contains "Wine builtin DLL" marker at offset 0x40
- **Theory**: Wine's builtin DLL loader in WoW64 mode fails to initialize `__wine_unix_call_dispatcher`
- **Test**: Removed builtin marker, created "native" PE DLL
- **Result**: **Crash persists** - hypothesis partially incorrect

### Phase 3: Code Instrumentation
- Added `EARLY_DBG()` using `OutputDebugStringA` (works before CRT init)
- Added `DBG_STDERR()` for direct stderr output
- Instrumented `DllMain`, `init_wine_unix_call`, and entry points
- **Result**: **NO debug output appears before crash** → crash before DllMain

### Phase 4: Current Status
- Crash occurs during PE loader initialization, BEFORE any WineASIO code runs
- Most likely culprits: **TLS callbacks** or **MinGW CRT startup code**
- Stack trace shows NULL function pointer execution with suspicious "Wine" ASCII in return address

---

## Technical Details

### Crash Signature

```
Unhandled exception: page fault on execute access to 0x00000000 in wow64 32-bit code (0x00000000).
Register dump:
 EIP:00000000 ESP:005ff3f4 EBP:005ff43c EFLAGS:00010246
 EAX:00000000 EBX:00000001 ECX:00e34490 EDX:7f5b14d0
 ESI:005ff43c EDI:7982601e
```

### Stack Analysis

```
Stack at crash point (0x005ff43c):
0x005ff43c: 656e6957 0000000d
            ^-------^
            "Wine" in little-endian ASCII (0x57=W, 0x69=i, 0x6e=n, 0x65=e)
```

**EDI=7982601e** points inside `portaudio_x86` module range (79820000-79855000), suggesting the crash may be triggered by or related to prior DLL loads.

### PE Structure Findings

1. **TLS Directory Present**: Entry 9 at RVA 0x0000bc44 (size 0x18)
2. **TLS Callback Symbol**: `___mingw_TLScallback` at 0x00002e60
3. **Import Table**: Multiple imports from ntdll, advapi32, kernel32, ole32, etc.
4. **No Builtin Marker**: Verified via hexdump (DOS stub = standard "MZ" + stub code)

### What We Know Does NOT Cause the Crash

- ✅ **Wine's builtin DLL handling** - crash persists without builtin marker
- ✅ **JACK/Berkeley DB** - messages unrelated to Wine/WineASIO
- ✅ **WineASIO runtime code** - test_asio_extended.exe works perfectly
- ✅ **Basic WoW64 DLL loading** - minimal test DLL loads successfully
- ✅ **ASIO callback/buffer code** - CreateBuffers, Start, Stop all work in tests

---

## Hypotheses & Next Steps

### Hypothesis 1: TLS Callbacks Call NULL Function ⭐ MOST LIKELY

**Evidence**:
- TLS directory exists in PE
- `___mingw_TLScallback` symbol present
- TLS callbacks execute BEFORE DllMain
- Previous attempt to patch TLS may have been incomplete

**Test**:
```bash
# Examine TLS directory structure
i686-w64-mingw32-objdump -s -j .tls ~/.wine/drive_c/windows/syswow64/wineasio.dll

# Look for TLS callback array (should be array of function pointers terminated by NULL)
# The array address is in TLS directory at offset +12 (AddressOfCallBacks field)
```

**Fix Approach**:
1. Locate TLS callback array in PE
2. Patch array to contain only NULL terminator
3. Or: rebuild with `-nostartfiles` and custom CRT entry point (complex)

### Hypothesis 2: MinGW CRT Init Code Has NULL Pointer

**Evidence**:
- No debug output before crash = crash during CRT startup
- MinGW's CRT (_start, __mingw_init) runs before DllMain
- WoW64 may interact poorly with MinGW's Win32 API assumptions

**Test**:
```bash
# Disassemble PE entry point and trace CRT initialization
i686-w64-mingw32-objdump -d ~/.wine/drive_c/windows/syswow64/wineasio.dll | grep -A50 "<_DllMainCRTStartup>"
```

**Fix Approach**:
1. Build with minimal CRT: `-nostartfiles -nostdlib`
2. Implement minimal DLL entry point manually (complex, needs CRT functions)
3. Or: use MSVC toolchain instead of MinGW (major refactor)

### Hypothesis 3: Import Thunk Resolution Issue

**Evidence**:
- Import table shows many ntdll/kernel32 imports
- 32-bit WoW64 may resolve imports differently
- NULL could be unresolved import

**Test**:
```bash
# Check if all imports resolve
wine dumpbin /imports ~/.wine/drive_c/windows/syswow64/wineasio.dll

# Or use winedump
winedump -j import ~/.wine/drive_c/windows/syswow64/wineasio.dll
```

**Fix Approach**:
1. Delay-load problematic imports
2. Use GetProcAddress for all Wine-specific functions (already doing this)

### Hypothesis 4: Exception Handler Registration

**Evidence**:
- MinGW registers SEH handlers during startup
- SEH registration could call NULL handler

**Test**:
```bash
# Check for .pdata or exception info
i686-w64-mingw32-objdump -p ~/.wine/drive_c/windows/syswow64/wineasio.dll | grep -i exception
```

---

## Comparison: Working vs. Crashing Scenarios

| Scenario | Works? | Notes |
|----------|--------|-------|
| test_asio_extended.exe (custom host) | ✅ YES | Full ASIO functionality, 32-bit WoW64 |
| Minimal test DLL (MinGW) | ✅ YES | Simple DllMain, no imports, 32-bit WoW64 |
| REAPER 32-bit enumerating WineASIO | ❌ CRASH | Before DllMain, NULL execute |
| CFX Lite enumerating WineASIO | ❌ CRASH | Same crash signature |
| WineASIO 64-bit | ✅ YES | Works in native 64-bit Wine |

**Key Difference**: Real hosts load many DLLs before WineASIO (portaudio_x86, ILWASAPI2ASIO). Possible DLL load order or initialization ordering issue?

---

## Recommended Actions (Priority Order)

### 🔴 PRIORITY 1: Examine TLS Callbacks
```bash
cd /tmp
# Extract TLS directory details
python3 << 'EOF'
import struct

dll_path = "/home/gng/.wine/drive_c/windows/syswow64/wineasio.dll"
with open(dll_path, 'rb') as f:
    # Read DOS header
    f.seek(0x3C)
    pe_offset = struct.unpack('<I', f.read(4))[0]
    
    # Read PE header
    f.seek(pe_offset + 4 + 20 + 92)  # Offset to Data Directory[9] (TLS)
    tls_rva = struct.unpack('<I', f.read(4))[0]
    tls_size = struct.unpack('<I', f.read(4))[0]
    
    print(f"TLS Directory RVA: 0x{tls_rva:08x}, Size: 0x{tls_size:08x}")
    
    # TLS directory structure (32-bit):
    # +0x00: StartAddressOfRawData (VA)
    # +0x04: EndAddressOfRawData (VA)
    # +0x08: AddressOfIndex (VA)
    # +0x0C: AddressOfCallBacks (VA) ← IMPORTANT
    # +0x10: SizeOfZeroFill
    # +0x14: Characteristics
    
    # Read TLS directory
    f.seek(tls_rva)
    tls_data = f.read(0x18)
    start_va, end_va, index_va, callbacks_va, zero_fill, chars = struct.unpack('<IIIIII', tls_data)
    
    print(f"TLS Callbacks VA: 0x{callbacks_va:08x}")
    print(f"This is a VIRTUAL ADDRESS (not RVA)")
    print(f"Need to convert VA to file offset and examine callback array")
EOF
```

### 🟡 PRIORITY 2: Verbose winedbg Trace
```bash
# Get full backtrace with all threads and memory map
WINEDEBUG=+seh,+loaddll,+relay wine winedbg --gdb '/home/gng/.wine/drive_c/Program Files/Garritan/CFX Lite/CFX Lite.exe' 2>&1 | tee crash_full_debug.log

# When it crashes, inside winedbg:
# bt all       - backtrace all threads
# info registers
# x/20x $esp   - examine stack
# info dll     - list all loaded modules
```

### 🟡 PRIORITY 3: Binary Patch TLS Callbacks to NULL
```bash
# If we find TLS callback array, patch it:
cd /tmp
cp ~/.wine/drive_c/windows/syswow64/wineasio.dll wineasio_notls.dll

# Use hexedit or python to patch the callback array address in TLS directory
# Set AddressOfCallBacks (offset 0x0C in TLS directory) to 0x00000000
python3 << 'EOF'
import struct

# Patch TLS directory to disable callbacks
dll_path = "/tmp/wineasio_notls.dll"
with open(dll_path, 'r+b') as f:
    # Find TLS directory at RVA 0xbc44 (from objdump output)
    f.seek(0xbc44 + 0x0C)  # Offset to AddressOfCallBacks field
    f.write(struct.pack('<I', 0))  # Set to NULL
    print("Patched TLS callbacks to NULL")
EOF

# Test patched DLL
cp /tmp/wineasio_notls.dll ~/.wine/drive_c/windows/syswow64/wineasio.dll
wine '/home/gng/.wine/drive_c/Program Files/Garritan/CFX Lite/CFX Lite.exe'
```

### 🟢 PRIORITY 4: Alternative Build Approach
```bash
# Try building without TLS altogether
cd ~/wineasio-fork

# Option A: Disable TLS in linker
i686-w64-mingw32-gcc -shared -o wineasio_notls.dll *.o \
    -Wl,--no-insert-timestamp \
    -Wl,--disable-stdcall-fixup \
    -Wl,--no-seh \
    -static-libgcc \
    -lole32 -loleaut32 -luuid -ladvapi32

# Option B: Build as simple "native" DLL without builtin machinery
# (Don't use winebuild at all, just produce PE)
```

### 🟢 PRIORITY 5: Minimal Repro
Create absolute minimal WineASIO stub to isolate the crash:
```c
// minimal_wineasio.c
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        MessageBoxA(NULL, "Minimal WineASIO loaded!", "Debug", MB_OK);
    }
    return TRUE;
}

// Export DllGetClassObject so hosts recognize it as COM DLL
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void) {
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void) {
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void) {
    return S_OK;
}
```

Compile and test:
```bash
i686-w64-mingw32-gcc -shared -o minimal_wineasio.dll minimal_wineasio.c \
    -lole32 -luuid -Wl,--out-implib,libminimal.a

cp minimal_wineasio.dll ~/.wine/drive_c/windows/syswow64/wineasio.dll
wine '/home/gng/.wine/drive_c/Program Files/Garritan/CFX Lite/CFX Lite.exe'
```

If this works → problem is in WineASIO's imports/initialization
If this crashes → problem is systemic (Wine WoW64 + specific DLL name/location?)

---

## Build Environment

```bash
# Current working Makefile
cd ~/wineasio-fork
cat Makefile.wine11

# Known issues:
# - Uses winegcc for Unix side (produces .so) ✅
# - Uses i686-w64-mingw32-gcc for PE side ✅
# - PE side links against MinGW CRT (potential issue)
# - TLS callbacks from MinGW CRT (likely issue)
```

---

## Related Files

- `asio_pe.c` - PE side (Windows), has extensive DBG_STDERR instrumentation
- `asio_unix.c` - Unix side (JACK), not involved in crash
- `Makefile.wine11` - Current build system
- `/tmp/wineasio_native.dll` - Non-builtin test version
- `~/.wine/drive_c/windows/syswow64/wineasio.dll` - Currently installed (non-builtin)

---

## Wine Bug Tracker

**WineHQ Bug #59283**: Original report incorrectly attributed crash to Berkeley DB. 
Should update with findings:
- Crash is in WineASIO DLL loading (before DllMain)
- Not related to Berkeley DB or JACK
- TLS callbacks or MinGW CRT initialization most likely cause
- Affects only 32-bit WoW64 mode
- 64-bit native works fine

---

## Questions for Wine Developers

1. Does Wine's WoW64 PE loader correctly handle TLS callbacks from MinGW-built DLLs?
2. Are there known issues with MinGW CRT initialization in WoW64 mode?
3. Is there a recommended way to build "hybrid" PE+Unix DLLs for Wine 11.0 without builtin marker?
4. Would using MSVC instead of MinGW for PE side avoid these issues?

---

## Success Criteria

✅ **Minimum Goal**: WineASIO 32-bit loads without crashing when host enumerates ASIO drivers

✅ **Full Goal**: WineASIO 32-bit works with full functionality (Init, CreateBuffers, Start, Stop, callbacks)

**Current Status**: 0% - crashes before any WineASIO code runs

---

## Last Updated

2026-01-21 by AI assistant (Claude Sonnet 4.5)

**Next Session TODO**: Execute PRIORITY 1 (TLS callback analysis) and PRIORITY 3 (binary TLS patch)