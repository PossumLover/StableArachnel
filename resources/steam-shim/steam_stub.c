/*
 * Arachnel's steam.exe stub.
 *
 * Steam emulators (Unsteam, Online Fix) locate the Steam client through
 * HKCU\Software\Valve\Steam\ActiveProcess: they read PID and check that such a process
 * exists. Under Proton nothing ever does - Proton writes a placeholder PID (0xfffe) that
 * belongs to no process - so the emulator reports "Unable to find steam process, make
 * sure Steam is running" and refuses to initialise.
 *
 * This stub is that missing process. It writes its own PID into ActiveProcess along with
 * the paths the API expects, then stays alive doing nothing at all until Arachnel stops
 * it at the end of the session. No window, no output, no files touched.
 *
 * Rebuild with:
 *   x86_64-w64-mingw32-gcc -O2 -s -mwindows -o steam.exe steam_stub.c -ladvapi32
 */

#include <windows.h>

static void setString(HKEY key, const char* name, const char* value)
{
    RegSetValueExA(key, name, 0, REG_SZ, (const BYTE*)value, (DWORD)(lstrlenA(value) + 1));
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;

    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, NULL,
                        0, KEY_SET_VALUE, NULL, &key, NULL)
        == ERROR_SUCCESS) {
        const DWORD pid = GetCurrentProcessId();
        RegSetValueExA(key, "PID", 0, REG_DWORD, (const BYTE*)&pid, sizeof(pid));
        setString(key, "SteamClientDll", "C:\\Program Files (x86)\\Steam\\steamclient.dll");
        setString(key, "SteamClientDll64", "C:\\Program Files (x86)\\Steam\\steamclient64.dll");
        setString(key, "SteamPath", "C:\\Program Files (x86)\\Steam");
        const DWORD universe = 1; /* k_EUniversePublic */
        RegSetValueExA(key, "Universe", 0, REG_DWORD, (const BYTE*)&universe, sizeof(universe));
        RegCloseKey(key);
    }

    /* Arachnel terminates this process when the game exits. */
    for (;;)
        Sleep(60000);

    return 0;
}
