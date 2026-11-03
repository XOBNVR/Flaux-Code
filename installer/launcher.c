/* flaux.exe: the `flaux` command on Windows.
 *
 * It runs Flaux using the Python that comes with the installer:
 *     {install folder}\python\python.exe {install folder}\flaux <what you typed>
 *
 * A real .exe (not a .cmd file) means batch files can just say
 * `flaux mygame.flx` and carry on afterwards (a .cmd would need `call`).
 */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static wchar_t command[32768];

int wmain(void) {
    wchar_t app[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, app, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    for (int up = 0; up < 2; up++) {          /* ...\Flaux\bin\flaux.exe -> ...\Flaux */
        wchar_t *slash = wcsrchr(app, L'\\');
        if (!slash) return 1;
        *slash = 0;
    }

    /* everything typed after "flaux" */
    wchar_t *rest = GetCommandLineW();
    if (*rest == L'"') {
        rest++;
        while (*rest && *rest != L'"') rest++;
        if (*rest) rest++;
    } else {
        while (*rest && *rest != L' ' && *rest != L'\t') rest++;
    }

    _snwprintf(command, 32767, L"\"%ls\\python\\python.exe\" \"%ls\\flaux\"%ls", app, app, rest);
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetConsoleCtrlHandler(NULL, TRUE);        /* let Ctrl+C go to the program, not to us */

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"Flaux couldn't start. Try reinstalling it with FlauxSetup.exe.\n");
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
