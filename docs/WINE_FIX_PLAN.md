# Plan: Wine 11 WoW64 Unix-Loader Fix

## Status: ✅ SOLVED

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
- 2026-01-21: **FIXED** - Re-enabled `--builtin`, created proper test

---

**CONCLUSION**: The fix was simple - re-enable `--builtin` and use correct calling convention in tests. No Wine patches needed!