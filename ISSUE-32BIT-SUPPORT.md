# Issue: 32-bit WineASIO Support for Wine 11 Custom Build

**Date**: 2025-01-20  
**Status**: ❌ BLOCKED - Requires Wine Architecture Decision  
**Branch**: `wine11-custom-build-32bit`  
**Priority**: HIGH (Garritan CFX Lite and other 32-bit VST plugins need this)

---

## Problem Summary

WineASIO 32-bit does not work with our Custom Wine 11 build (`/usr/local`), while 64-bit works perfectly.

### What Works ✅
- Wine 11 Custom Build with WoW64 (i386 + x86_64 PE support)
- WineASIO 64-bit for Custom Wine 11
- OpenGL 32-bit + 64-bit
- 32-bit Windows apps run (MIDI-OX, Yellow Rose demo, etc.)

### What Doesn't Work ❌
- WineASIO 32-bit registration fails
- 32-bit apps (Garritan CFX Lite) cannot see WineASIO driver
- Error: `STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)` when loading unix library

---

## Root Cause Analysis

### Wine 11 "New WoW64" Architecture

Our Custom Wine 11 build uses **"new WoW64" mode** (default in Wine 11):

```
Wine 11 Custom Build (new WoW64):
├── i386-windows/          ← 32-bit PE DLLs
├── x86_64-windows/        ← 64-bit PE DLLs
└── x86_64-unix/           ← 64-bit Unix libraries (shared by both!)
    └── (no i386-unix directory!)
```

**Key difference**: In "new WoW64", 32-bit PE DLLs use the **same 64-bit Unix libraries** as 64-bit PE DLLs.

Compare to wine-stable package ("old WoW64"):

```
Wine Stable Package (old WoW64):
├── i386-windows/          ← 32-bit PE DLLs
├── i386-unix/             ← 32-bit Unix libraries (separate!)
├── x86_64-windows/        ← 64-bit PE DLLs
└── x86_64-unix/           ← 64-bit Unix libraries
```

### Why WineASIO 32-bit Fails

1. **Build System Assumption**: Our `Makefile.wine11` builds separate 32-bit Unix libraries (`wineasio.so` as ELF32)
2. **Runtime Expectation**: Wine's "new WoW64" expects 32-bit PE DLLs to find a 64-bit Unix library
3. **Symbol Loading Issue**: `NtQueryVirtualMemory(MemoryWineUnixFuncs)` returns `STATUS_ENTRYPOINT_NOT_FOUND`
4. **Result**: 32-bit PE DLL loads, but cannot initialize the Unix side → crashes or shows error dialog

---

## Attempted Solutions (FAILED)

### Attempt 1: Add visibility attributes to Unix symbols
- **Goal**: Export `__wine_unix_call_funcs` explicitly
- **Change**: Added `__attribute__((visibility("default")))`
- **Result**: Symbol exported, but still `STATUS_ENTRYPOINT_NOT_FOUND`
- **Why it failed**: Architecture mismatch (ELF32 vs ELF64 expected)

### Attempt 2: Copy 64-bit Unix library for 32-bit use
- **Goal**: Use `wineasio64.so` (ELF64) for 32-bit PE DLL
- **Method**: `ln -s x86_64-unix/wineasio64.so i386-unix/wineasio.so`
- **Result**: Registration succeeded but app crashes with Page Fault
- **Why it failed**: ABI incompatibility between 32-bit PE and 64-bit Unix thunking

### Attempt 3: Modify symbol imports from ntdll
- **Goal**: Use Wine's `wine/unixlib.h` instead of custom declarations
- **Result**: Compile errors, linker issues, and eventual crashes
- **Why it failed**: mingw linker requires link-time resolution, but Wine provides runtime resolution

**All changes reverted** - repository is back to clean state.

---

## Current Installation State

```bash
# Custom Wine 11 (/usr/local)
/usr/local/lib/wine/x86_64-windows/wineasio64.dll  ✅ Working (64-bit)
/usr/local/lib/wine/x86_64-unix/wineasio64.so      ✅ Working (64-bit)

# Missing:
/usr/local/lib/wine/i386-windows/wineasio.dll      ❌ Not installed
/usr/local/lib/wine/i386-unix/wineasio.so          ❌ Directory exists but empty
```

---

## Available Solutions

### Option A: Rebuild Custom Wine in "Old WoW64" Mode ⭐ RECOMMENDED

**Pros**:
- Standard Wine 11 approach (used by wine-stable packages)
- WineASIO can build normally with separate 32-bit Unix libraries
- Well-tested architecture

**Cons**:
- Requires full Wine rebuild (60-90 minutes)
- Uses more disk space (separate 32-bit libraries)
- "Old WoW64" may be deprecated in future Wine versions

**Steps**:
```bash
cd ~/docker-workspace/sources/wine-11.0-build
make clean
../wine-11.0/configure --enable-archs=i386,x86_64 --with-mingw --disable-new-wow64
make -j$(nproc)
sudo make install
```

### Option B: Adapt WineASIO for "New WoW64" 

**Pros**:
- Future-proof (new WoW64 is Wine's direction)
- Smaller disk footprint

**Cons**:
- Requires deep understanding of Wine internals
- Needs custom thunking layer for 32→64 bit calls
- High complexity, uncertain success
- May break on Wine updates

**Requirements**:
- Study Wine's internal WoW64 thunking mechanism
- Implement proper entry points for 32-bit PE calling 64-bit Unix
- Possibly requires changes to both PE and Unix sides

### Option C: Use wine-stable Package for 32-bit Apps

**Pros**:
- No changes needed - works out of the box
- wine-stable is in "old WoW64" mode with full 32-bit support

**Cons**:
- Mixed Wine versions (custom for 64-bit, package for 32-bit)
- Confusing for users
- Different behavior between 32-bit and 64-bit apps

---

## Testing Results

### Before Fix Attempts:
```
✅ CFX Lite 64-bit: Works with WineASIO (if such version exists)
❌ CFX Lite 32-bit: "Last audio devices listing failed" - no ASIO driver found
```

### After Fix Attempts:
```
❌ CFX Lite 64-bit: Broken (WineASIO not found)
❌ CFX Lite 32-bit: Crashes with page fault (0x0022f43c)
```

### After Rollback:
```
✅ CFX Lite 64-bit: Restored and working
❌ CFX Lite 32-bit: Still no ASIO driver (original problem)
```

---

## Recommended Action Plan

1. **Immediate** (Today):
   - ✅ Rollback all changes (DONE)
   - ✅ Restore working 64-bit WineASIO (DONE)
   - ✅ Document issue (this file)

2. **Short-term** (This week):
   - Research Wine 11 "new WoW64" architecture documentation
   - Check Wine GitLab for similar issues from other projects
   - Decide: Option A (old WoW64 rebuild) vs Option B (new WoW64 adaptation)

3. **Medium-term** (Next week):
   - If Option A: Schedule Wine rebuild with `--disable-new-wow64`
   - If Option B: Create proof-of-concept for 32-bit thunking

---

## Useful References

- Wine 11.0 Release Notes: WoW64 section
- Wine GitLab: `/dlls/bcrypt/` (example of Unix library architecture)
- Wine Headers: `/include/wine/unixlib.h`
- WineASIO Original: `/home/gng/docker-workspace/wineasio-1.3.0/`

---

## Contact & Discussion

- Main Thread: [Zed AI Chat - 32 bit krb5 multidev file conflict]
- Branch: `wine11-custom-build-32bit`
- Previous working build: wineasio-1.3.0 (Jan 14, 2025)

---

## Notes

**Important**: Do NOT attempt to "fix" 32-bit support without first deciding on the Wine architecture strategy. The failed attempts show that mixing architectures leads to crashes and instability.

The 64-bit version works perfectly - focus there for production use while researching the proper 32-bit solution.