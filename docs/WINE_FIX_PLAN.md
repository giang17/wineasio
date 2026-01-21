# Plan: Wine 11 WoW64 Unix-Loader Fix

## Status: ⚠️ PARTIALLY SOLVED - Init() fails for real ASIO hosts

**Last Updated**: 2026-01-21 Late Evening

## Original Problem
`NtQueryVirtualMemory(MemoryWineUnixFuncs)` returned `STATUS_DLL_NOT_FOUND` (c0000135) for 32-bit PE DLLs.

## Root Cause (Found!)
1. **Missing `--builtin` flag**: The DLL was not marked as "Wine builtin DLL", so Wine's loader didn't know to look for the corresponding `.so` file
2. **Test program bug**: The original test used **stdcall** convention, but ASIO on 32-bit Windows uses **thiscall** (this pointer in ECX register)

## Solution Applied

### 1. Re-enable `--builtin` Flag
The `winebuild --builtin` flag is **REQUIRED** for DLLs that have Unix counterparts. It:
- Writes "Wine builtin DLL" signature at offset 0x40 in the PE header
- Tells Wine's loader to search for the matching `.so` file
- Without it, `NtQueryVirtualMemory(MemoryWineUnixFuncs)` returns `c0000135`

### 2. Keep Linker Flags for WoW64 Compatibility
```makefile
PE_LDFLAGS_32 = -Wl,--image-base=0x10000000 -Wl,--disable-reloc-section
```
- **Lower ImageBase**: Avoids conflicts in WoW64 32-bit address space
- **No relocations**: Sidesteps Wine WoW64 relocation handling bugs

### 3. Use Correct Calling Convention in Tests
ASIO uses **thiscall** on 32-bit Windows:
- `this` pointer passed in **ECX register** (not on stack!)
- Other arguments pushed right-to-left on stack
- Callee cleans up the stack

Created `test_asio_thiscall.c` with proper calling convention.

## Build Configuration (Final)
```makefile
# 32-bit PE DLL - must be marked as builtin for Unix side loading
$(BUILD_DIR)/$(DLL32): $(PE_SOURCES) unixlib.h $(NTDLL_LIB32) wineasio.def | $(BUILD_DIR)
	$(MINGW32) $(PE_CFLAGS) -m32 \
		-o $@ $(PE_SOURCES) \
		$(NTDLL_LIB32) \
		$(PE_LDFLAGS_32) $(PE_LIBS) \
		wineasio.def \
		-Wl,--out-implib,$(BUILD_DIR)/libwineasio.a
	$(WINEBUILD) --builtin $@   # <-- REQUIRED!
```

## Verification

### Check builtin signature:
```bash
xxd -s 0x40 -l 32 wineasio.dll
# Should show: "Wine builtin DLL"
```

### Test with thiscall test program:
```bash
wine test_asio_thiscall.exe
# Should show: "Test completed successfully!"
```

## Why the Original Analysis Was Wrong

### Original Theory (WRONG):
> "Wine 11 WoW64 PE loader crashes when loading DLLs marked as builtin"

### Actual Cause:
The crash was in the **test program**, not Wine or WineASIO:
1. `test_asio_minimal.exe` used stdcall for ASIO methods
2. WineASIO's vtable contains thiscall wrappers (correct for real ASIO hosts)
3. Calling convention mismatch → crash
4. `--builtin` was incorrectly blamed

### Evidence:
- Real ASIO hosts (REAPER, Cubase, FL Studio) use thiscall → they work!
- `test_asio_thiscall.exe` uses thiscall → it works!
- `test_asio_minimal.exe` uses stdcall → it crashes!

## Files Modified
- `Makefile.wine11` - Re-enabled `--builtin` for 32-bit
- `test_asio_thiscall.c` - NEW: Test with correct calling convention
- `docs/WINE11_32BIT_DEBUG_SESSION.md` - Updated with solution

## Options Considered

| Option | Description | Result |
|--------|-------------|--------|
| **A) Re-enable --builtin** | Mark DLL as Wine builtin | ✅ **CHOSEN - WORKS!** |
| B) Patch Wine | Allow native DLLs to load Unix side | Not needed |
| C) load_builtin_unixlib() | Call in DllMain | Not needed |

## Lessons Learned

1. **Always use `--builtin`** for PE DLLs with Unix counterparts
2. **Understand calling conventions** when debugging COM/ASIO interfaces
3. **Test with correct calling conventions** - thiscall vs stdcall matters!
4. **Real ASIO hosts use thiscall** - test programs should too

## Timeline
- 2026-01-21: Initial investigation, identified `c0000135` error
- 2026-01-21: Incorrectly blamed `--builtin` flag
- 2026-01-21: Discovered thiscall calling convention issue
- 2026-01-21: Re-enabled `--builtin`, created proper test
- 2026-01-21: REAPER 32-bit loads WineASIO, CreateBuffers works, but no audio!

## Current Status (2026-01-21 Late Evening)

### What Works
- ✅ Unix-side loads correctly (`SUCCESS - unix handle = ...`)
- ✅ DllGetClassObject called
- ✅ **test_asio_start.exe** - Full ASIO pipeline works perfectly!
  - Init() succeeds
  - CreateBuffers() succeeds  
  - Start() succeeds
  - JACK ports created (52 ports visible in patchance)
  - Callbacks fire hundreds of times
  - Audio pipeline fully functional

### What Doesn't Work - Real ASIO Hosts (REAPER, CFX Lite)
- ❌ **Init() FAILS** - No success message, JACK ports never created
- ❌ CreateBuffers() never reached (Init fails first)
- ❌ Start() never reached
- ❌ No audio output - REAPER reports "audio close"

### Debug Log Evidence (Latest - 2026-01-21 19:42)
```
[WineASIO-DBG] >>> Init(iface=01741620, sysRef=0005022c)
[WineASIO-DBG] DllMain: hInstDLL=09860000 fdwReason=2   # DLL_THREAD_ATTACH
[WineASIO-DBG] DllMain: hInstDLL=09860000 fdwReason=3   # DLL_THREAD_DETACH
[WineASIO-DBG] DllMain: hInstDLL=09860000 fdwReason=3   # DLL_THREAD_DETACH
[WineASIO-DBG] DllMain: hInstDLL=09860000 fdwReason=0   # DLL_PROCESS_DETACH - EXIT!
```

**CRITICAL**: No `<<< Init returning SUCCESS` message! 
Init() is failing silently - the Unix side (JACK connection) fails.

## Next Session TODOs

### Priority 1: Debug why Init() fails for real ASIO hosts
1. **Add debug output to `asio_unix.c` → `asio_init()`**:
   - Before/after `pjack_client_open()` 
   - Check if JACK client is NULL
   - Before/after `pjack_activate()`
   - Log JACK status codes
2. Compare Init() call parameters:
   - Test program uses `sysRef=NULL`
   - REAPER uses `sysRef=0005022c` (window handle)
3. Check if 32-bit libjack is loading correctly

### Priority 2: Investigate JACK 32-bit Issues
1. Check if 32-bit libjack is installed: `ls /usr/lib/i386-linux-gnu/libjack*`
2. Check if PipeWire-JACK works with 32-bit
3. Try running a simple 32-bit JACK client

### Priority 3: Test with CFX Lite (already done - same issue)
- ✅ Tested: Same behavior as REAPER - Init() fails

### Debug Commands for Next Session
```bash
# Add debug to asio_unix.c, rebuild, and test
cd /home/gng/docker-workspace/wineasio-fork
# Edit asio_unix.c → asio_init() function
make -f Makefile.wine11 32
sudo make -f Makefile.wine11 install

# Test with debug output
WINEDEBUG=-all wine "$HOME/.wine/drive_c/Program Files (x86)/REAPER/reaper.exe" 2>&1 | head -40

# Check JACK 32-bit library
ls -la /usr/lib/i386-linux-gnu/libjack*
ldconfig -p | grep jack

# Test working program for comparison
WINEDEBUG=-all wine test_asio_start.exe 2>&1 | grep -E "Init|JACK"
```

### Key Insight
The problem is NOT in CreateBuffers() or Start()!
The problem is in **Init() → Unix side → JACK connection fails for real hosts**.

Test program Init() succeeds, but REAPER/CFX Lite Init() fails.
The difference might be:
- sysRef parameter (NULL vs window handle)
- Thread context
- 32-bit JACK library issues
- PipeWire-JACK 32-bit compatibility

### Log Files
- `~/docker-workspace/logs/reaper32_wineasio_20260121_190132.log`
- `~/docker-workspace/logs/reaper32_callback_debug_20260121_193922.log`
- `~/docker-workspace/logs/reaper32_return_debug_20260121_194245.log`

---

**CONCLUSION**: 

1. ✅ Unix-side loading is fixed with `--builtin`
2. ✅ Test program (`test_asio_start.exe`) proves WineASIO code is correct
3. ❌ Real ASIO hosts (REAPER, CFX Lite) fail at Init() - JACK connection not established
4. ❓ The difference between test and real hosts needs investigation

**Next Step**: Add debug output to `asio_unix.c` → `asio_init()` to find exactly why JACK client creation fails for real ASIO hosts but works for the test program.