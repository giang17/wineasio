# Plan: Wine 11 WoW64 Unix-Loader Fix

## Problem
`NtQueryVirtualMemory(MemoryWineUnixFuncs)` gibt `STATUS_DLL_NOT_FOUND` (c0000135) für 32-bit PE DLLs.

## Root Cause (gefunden!)
In `dlls/ntdll/unix/virtual.c:get_builtin_unix_funcs()`:
- Wine sucht in `builtin_modules` Liste nach dem PE-Modul
- Wenn Modul nicht gefunden ODER `unix_path` nicht gesetzt → STATUS_DLL_NOT_FOUND
- Unsere DLL ist nicht in dieser Liste weil sie als "native" geladen wird

## Relevante Dateien
```
/home/gng/docker-workspace/sources/wine-11.0/dlls/ntdll/unix/virtual.c
  - get_builtin_unix_funcs() Zeile 837
  - builtin_modules Liste

/home/gng/docker-workspace/sources/wine-11.0/dlls/ntdll/unix/loader.c  
  - Wo werden Module zu builtin_modules hinzugefügt?
```

## Nächste Schritte

### 1. Verstehe builtin_modules Registration
```bash
grep -rn "builtin_modules" wine-11.0/dlls/ntdll/
```
- Wann wird ein Modul hinzugefügt?
- Warum wird unsere DLL nicht hinzugefügt?

### 2. Debug mit WINEDEBUG
```bash
WINEDEBUG=+module,+loaddll wine ...
```
- Wird wineasio.dll als builtin oder native geladen?
- Wird unix_path gesetzt?

### 3. Mögliche Fixes

**Option A: DLL muss als builtin markiert sein**
- Reaktiviere `winebuild --builtin` für 32-bit
- Aber das crashte vorher... warum?

**Option B: Wine patchen**
- Erlaube native DLLs, Unix-Seite zu laden
- Prüfe ob .so existiert, auch wenn DLL nicht builtin ist

**Option C: load_builtin_unixlib() aufrufen**
- In DllMain explizit die Unix-Lib registrieren
- `load_builtin_unixlib(hInstDLL, "wineasio.so")`

### 4. Test-Strategie
1. Patch in Wine einbauen
2. Wine neu kompilieren: `make -j8`
3. Test mit REAPER 32-bit
4. Wenn OK → Patch upstream vorschlagen

## Zeitschätzung
- Analyse: 30 min
- Patch entwickeln: 1-2 Stunden  
- Test & Debug: 1 Stunde
