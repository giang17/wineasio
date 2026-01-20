/*
 * WineASIO Settings Launcher
 * 
 * A minimal Windows executable that launches the native Linux
 * wineasio-settings GUI from within Wine applications (like FL Studio).
 * 
 * Compile with mingw:
 *   i686-w64-mingw32-gcc -o wineasio-settings.exe wineasio-settings-launcher.c -mwindows
 *   x86_64-w64-mingw32-gcc -o wineasio-settings64.exe wineasio-settings-launcher.c -mwindows
 * 
 * Copyright (C) 2025
 * License: GPL v2+
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

/* Try to launch the native Linux wineasio-settings via various methods */
static BOOL launch_native_settings(void)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[512];
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    /* Method 1: Use start.exe /unix to launch native tool */
    /* This is the recommended Wine method for launching Unix executables */
    lstrcpyA(cmd, "start.exe /unix /usr/bin/wineasio-settings");
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }
    
    /* Method 2: Try /usr/local/bin path */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    lstrcpyA(cmd, "start.exe /unix /usr/local/bin/wineasio-settings");
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }
    
    /* Method 3: Try with winepath (in case start.exe doesn't work) */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    lstrcpyA(cmd, "cmd.exe /c start /unix wineasio-settings");
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }
    
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    if (!launch_native_settings()) {
        MessageBoxA(NULL, 
            "Could not launch WineASIO Settings.\n\n"
            "Please make sure wineasio-settings is installed:\n"
            "  /usr/bin/wineasio-settings\n"
            "  or /usr/local/bin/wineasio-settings\n\n"
            "You can also run it manually from the Linux command line.",
            "WineASIO Settings",
            MB_OK | MB_ICONWARNING);
        return 1;
    }
    
    return 0;
}