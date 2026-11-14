/*
 * Forwarding proxy DLL for WineD3D on Windows XP x64.
 * Forwards standard GDI entrypoints to gdi32.dll and implements
 * D3DKMT* stubs from ucrtbase_xp.c.
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return TRUE;
}
