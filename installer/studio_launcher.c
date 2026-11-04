/* FlauxStudio.exe: opens Flaux Studio, the app (flauxc\studio\app.py, a
 * window made with Tk), with the Python that comes with the installer:
 *     {install folder}\python\pythonw.exe {install folder}\flaux studio [file.flx]
 * It's what the Start menu, the Desktop icon and double-clicking a .flx file
 * open, and it carries the Flaux icon. It doesn't wait: Flaux Studio has its
 * own window (and its own taskbar button). */
#include <windows.h>
#include <wchar.h>

static wchar_t command[32768];

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show) {
    (void)inst; (void)prev; (void)show;
    wchar_t app[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, app, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    wchar_t *slash = wcsrchr(app, L'\\');      /* ...\Flaux\FlauxStudio.exe -> ...\Flaux */
    if (!slash) return 1;
    *slash = 0;
    _snwprintf(command, 32767, L"\"%ls\\python\\pythonw.exe\" \"%ls\\flaux\" studio %ls", app, app, args ? args : L"");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, app, &si, &pi)) {
        MessageBoxW(NULL, L"Flaux Studio couldn't start. Try installing Flaux again with FlauxSetup.exe.",
                    L"Flaux Studio", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
