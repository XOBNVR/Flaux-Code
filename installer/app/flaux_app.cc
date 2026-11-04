/* Flaux's Windows apps: real programs with their own window (not a web
 * browser, and not Electron). The window shows Flaux's pages with WebView2,
 * the part of Windows that draws web pages for apps (it comes with Windows
 * 10 and 11), through the small `webview` library (webview.h, MIT).
 *
 * One source, two programs:
 *
 *   FlauxStudio.exe   Flaux Studio. Starts Flaux (python\python.exe flaux
 *                     studio, hidden) and shows the Studio in its window.
 *                     Closing the window stops Flaux and any program still
 *                     running (they're all in a job object).
 *
 *   FlauxPlayer.exe   (built with -DFLAUX_PLAYER) Plays a game exported from
 *                     Flaux Studio on the web: the export is this program
 *                     with the game (one web page) added to the end of the
 *                     file, renamed to <name>.exe. See "The game" below.
 *
 * Without WebView2 (an old Windows 10 that never updated), the Studio opens
 * the old way (Edge's app window, or the web browser) and a game opens in
 * the web browser.
 *
 * For tests: FLAUX_APP_TEST=<file> makes the page call flauxDone(...) when
 * it's ready (the Studio) or when the program ends (a game); what it gives is
 * written to <file> (as JSON) and the app closes.
 */
#include "webview.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

/* ------------------------------------------------------------ helpers */

static std::wstring widen(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring exe_path() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    return std::wstring(buf.data(), n);
}

static std::wstring folder_of(const std::wstring &path) {
    size_t i = path.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : path.substr(0, i);
}

static std::wstring name_of(const std::wstring &path) {   /* "C:\games\My Game.exe" -> "My Game" */
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

static bool file_exists(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void problem(const std::wstring &text) {
    MessageBoxW(nullptr, text.c_str(), L"Flaux", MB_OK | MB_ICONERROR);
}

/* Text in quotes for a command line (Windows' rules for backslashes before quotes). */
static std::wstring quoted(const std::wstring &arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { slashes++; continue; }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

/* The page calls the app's functions with its arguments as a JSON list, like
   ["My Game",640,400]. Only text, numbers and true/false are used. */
struct Arg {
    bool is_text = false;
    std::string text;
    double number = 0;
};

static std::vector<Arg> json_args(const std::string &s) {
    std::vector<Arg> args;
    size_t i = s.find('[');
    if (i == std::string::npos) return args;
    i++;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
        if (i >= s.size() || s[i] == ']') break;
        Arg a;
        if (s[i] == '"') {
            a.is_text = true;
            for (i++; i < s.size() && s[i] != '"'; i++) {
                if (s[i] != '\\' || i + 1 >= s.size()) { a.text += s[i]; continue; }
                char e = s[++i];
                if (e == 'n') a.text += '\n';
                else if (e == 't') a.text += '\t';
                else if (e == 'r') a.text += '\r';
                else if (e == 'b') a.text += '\b';
                else if (e == 'f') a.text += '\f';
                else if (e == 'u' && i + 4 < s.size()) {
                    unsigned cp = (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        unsigned lo = (unsigned)strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                    std::wstring w;   /* (UTF-16 -> UTF-8) */
                    if (cp >= 0x10000) { w += (wchar_t)(0xD800 + ((cp - 0x10000) >> 10)); w += (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
                    else w += (wchar_t)cp;
                    a.text += narrow(w);
                } else a.text += e;
            }
            i++;
        } else {
            size_t start = i;
            while (i < s.size() && s[i] != ',' && s[i] != ']') i++;
            std::string word = s.substr(start, i - start);
            a.text = word;
            a.number = word == "true" ? 1 : word == "false" ? 0 : strtod(word.c_str(), nullptr);
        }
        args.push_back(a);
    }
    return args;
}

/* ------------------------------------------------------- the window */

/* Fullscreen: the window without its frame, over the whole screen (and back). */
struct Fullscreen {
    bool on = false;
    LONG style = 0;
    WINDOWPLACEMENT place{};
};

static void set_fullscreen(HWND window, Fullscreen &f, bool on) {
    if (!window || on == f.on) return;
    if (on) {
        f.style = GetWindowLongW(window, GWL_STYLE);
        f.place.length = sizeof f.place;
        GetWindowPlacement(window, &f.place);
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(window, GWL_STYLE, f.style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(window, GWL_STYLE, f.style);
        SetWindowPlacement(window, &f.place);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    f.on = on;
}

/* Put the window in the middle of the screen it's on. */
static void center(HWND window) {
    RECT r;
    if (!window || !GetWindowRect(window, &r)) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
    RECT &a = mi.rcWork;
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = a.left + ((a.right - a.left) - w) / 2, y = a.top + ((a.bottom - a.top) - h) / 2;
    SetWindowPos(window, nullptr, x > a.left ? x : a.left, y > a.top ? y : a.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

/* In every page: fullscreen in the page (a game's Fullscreen button, F11) makes
   the whole window fullscreen, and Esc (which leaves it in the page) undoes that. */
static const char *PAGE_JS = R"JS(
window.flauxApp = true;
document.addEventListener("fullscreenchange", () => {
  if (window.flauxFullscreen) window.flauxFullscreen(!!document.fullscreenElement);
});
)JS";

/* For tests (FLAUX_APP_TEST): the Studio says it's ready once its page is there. */
static const char *STUDIO_TEST_JS = R"JS(
if (!window.flauxTestStarted) {
  window.flauxTestStarted = true;
  const t = setInterval(() => {
    const status = document.getElementById("status"), code = document.getElementById("code");
    if (status && code && window.flauxDone) {
      clearInterval(t);
      window.flauxDone(0, document.title + " | " + status.textContent);
    }
  }, 300);
}
)JS";

static std::wstring test_file() {
    wchar_t buf[4096];
    DWORD n = GetEnvironmentVariableW(L"FLAUX_APP_TEST", buf, 4096);
    return n && n < 4096 ? std::wstring(buf, n) : L"";
}

/* The functions the pages can call: fullscreen, the window's title and size, and (tests) flauxDone. */
static void add_app_functions(webview::webview &w, Fullscreen &full, const std::wstring &test) {
    w.bind("flauxFullscreen", [&w, &full](const std::string &req) -> std::string {
        auto args = json_args(req);
        set_fullscreen((HWND)w.window().value(), full, !args.empty() && args[0].number != 0);
        return "";
    });
    /* flauxWindow(title, width, height): the game's own name and size (in screen pixels) */
    w.bind("flauxWindow", [&w, &full](const std::string &req) -> std::string {
        auto args = json_args(req);
        if (!args.empty() && !args[0].text.empty()) w.set_title(args[0].text);
        if (args.size() >= 3 && args[1].number >= 50 && args[2].number >= 50 && !full.on) {
            w.set_size((int)args[1].number, (int)args[2].number, WEBVIEW_HINT_NONE);
            center((HWND)w.window().value());
        }
        return "";
    });
    if (!test.empty()) {
        w.bind("flauxDone", [&w, test](const std::string &req) -> std::string {
            FILE *f = _wfopen(test.c_str(), L"wb");
            if (f) { fwrite(req.data(), 1, req.size(), f); fclose(f); }
            w.terminate();
            return "";
        });
    }
}

static void set_icon_everywhere(HWND window) {   /* the Flaux icon in the title bar and on the taskbar */
    HICON big = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON small = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (big) SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (small) SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

#ifdef FLAUX_PLAYER
/* =============================================================== a game
 *
 * The game: this program's file, then the game's web page (game.html from
 * Flaux Studio's Export), then 8 bytes saying how long the page is (little
 * endian) and the 8 letters FLAUXGM1. */

static const char MAGIC[8] = { 'F', 'L', 'A', 'U', 'X', 'G', 'M', '1' };

static bool read_game(const std::wstring &exe, std::string &page) {
    FILE *f = _wfopen(exe.c_str(), L"rb");
    if (!f) return false;
    bool ok = false;
    unsigned char tail[16];
    if (_fseeki64(f, -16, SEEK_END) == 0 && fread(tail, 1, 16, f) == 16 && memcmp(tail + 8, MAGIC, 8) == 0) {
        uint64_t n = 0;
        for (int i = 7; i >= 0; i--) n = n << 8 | tail[i];
        long long end = _ftelli64(f) - 16;
        if (n > 0 && (long long)n <= end && _fseeki64(f, end - (long long)n, SEEK_SET) == 0) {
            page.resize((size_t)n);
            ok = fread(&page[0], 1, (size_t)n, f) == (size_t)n;
        }
    }
    fclose(f);
    return ok;
}

static int play() {
    std::wstring exe = exe_path(), name = name_of(exe), test = test_file();
    std::string page;
    if (!read_game(exe, page)) {
        problem(L"This is Flaux's game player, without a game in it.\n\n"
                L"To make a game you can share, press Export in Flaux Studio: it gives you a .zip with the game's .exe.");
        return 1;
    }
    /* The page goes in a file the window can open (a page this big can't be handed over directly). */
    wchar_t local[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) GetTempPathW(MAX_PATH, local);
    std::wstring folder = std::wstring(local) + L"\\Flaux\\games\\" + name;
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    std::wstring file = folder + L"\\game.html";
    FILE *f = _wfopen(file.c_str(), L"wb");
    if (!f || fwrite(page.data(), 1, page.size(), f) != page.size()) {
        if (f) fclose(f);
        problem(L"The game couldn't start: there's no room to unpack it in " + folder);
        return 1;
    }
    fclose(f);
    wchar_t url[4096];
    DWORD url_len = 4096;
    if (FAILED(UrlCreateFromPathW(file.c_str(), url, &url_len, 0))) return 1;

    try {
        webview::webview w(false, nullptr);
        HWND window = (HWND)w.window().value();
        set_icon_everywhere(window);
        Fullscreen full;
        w.set_title(narrow(name));
        w.set_size(900, 640, WEBVIEW_HINT_NONE);
        center(window);
        add_app_functions(w, full, test);
        w.init(PAGE_JS);
        w.navigate(narrow(url));
        w.run();
    } catch (const webview::exception &) {
        /* no WebView2 here: the game still works in the web browser */
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return play(); }

#else
/* ========================================================= Flaux Studio */

static const char *STARTING_PAGE = R"HTML(<!DOCTYPE html><html><body style="margin:0;height:100vh;display:grid;
place-items:center;background:#14161f;color:#9a9db0;font:16px system-ui,'Segoe UI',sans-serif">Starting Flaux Studio...</body></html>)HTML";

/* Flaux Studio the old way: Flaux opens it in Edge's app window, or the web browser. */
static int old_way(const std::wstring &app, const std::wstring &args) {
    std::wstring pythonw = app + L"\\python\\pythonw.exe";
    std::wstring cmd = quoted(pythonw) + L" " + quoted(app + L"\\flaux") + L" studio" + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(pythonw.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, app.c_str(), &si, &pi)) {
        problem(L"Flaux Studio couldn't start. Try installing Flaux again with FlauxSetup.exe.");
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

static int studio() {
    std::wstring app = folder_of(exe_path()), test = test_file();
    std::wstring python = app + L"\\python\\python.exe";
    if (!file_exists(python) || !file_exists(app + L"\\flaux")) {
        problem(L"Flaux Studio couldn't find Flaux next to it, in " + app +
                L".\n\nTry installing Flaux again with FlauxSetup.exe.");
        return 1;
    }
    /* a program to open (double-clicking a .flx file) */
    std::wstring args;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) args += L" " + quoted(argv[i]);
    if (argv) LocalFree(argv);

    webview::webview *w = nullptr;
    try {
        w = new webview::webview(false, nullptr);
    } catch (const webview::exception &) {
        return old_way(app, args);    /* no WebView2 on this computer */
    }
    HWND window = (HWND)w->window().value();
    set_icon_everywhere(window);
    Fullscreen full;
    w->set_title("Flaux Studio");
    w->set_size(1300, 850, WEBVIEW_HINT_NONE);
    center(window);
    add_app_functions(*w, full, test);
    w->init(PAGE_JS);
    if (!test.empty()) w->init(STUDIO_TEST_JS);
    w->set_html(STARTING_PAGE);

    /* Flaux itself, hidden. It (and every program it runs) is in a job that ends
       when this app does, so closing the window stops everything. */
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE out_read = nullptr, out_write = nullptr;
    CreatePipe(&out_read, &out_write, &sa, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    HANDLE nothing = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nothing;
    si.hStdOutput = out_write;
    si.hStdError = out_write;
    /* --keep-running: the app says when to stop (the window can be minimized for hours) */
    std::wstring cmd = quoted(python) + L" " + quoted(app + L"\\flaux") + L" studio --no-window --keep-running" + args;
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(python.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, app.c_str(), &si, &pi)) {
        problem(L"Flaux Studio couldn't start Flaux. Try installing Flaux again with FlauxSetup.exe.");
        return 1;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(out_write);
    CloseHandle(nothing);

    /* Flaux says "Flaux Studio is running at <address>": then the window shows it. */
    std::thread reader([w, out_read]() {
        std::string seen;
        bool found = false;
        char buf[4096];
        DWORD n;
        while (ReadFile(out_read, buf, sizeof buf, &n, nullptr) && n > 0) {
            if (found) continue;            /* (keep reading, so Flaux never waits to write) */
            seen.append(buf, n);
            size_t at = seen.find("running at ");
            size_t eol = at == std::string::npos ? std::string::npos : seen.find('\n', at);
            if (eol != std::string::npos) {
                std::string url = seen.substr(at + 11, eol - at - 11);
                while (!url.empty() && (url.back() == '\r' || url.back() == ' ')) url.pop_back();
                found = true;
                w->dispatch([w, url]() { w->navigate(url); });
            }
        }
        if (!found) {                       /* Flaux stopped before it was ready: show why */
            std::string text;
            for (char c : seen) {
                if (c == '<') text += "&lt;";
                else if (c == '&') text += "&amp;";
                else text += c;
            }
            w->dispatch([w, text]() {
                w->set_html("<body style=\"background:#14161f;color:#e8e9f0;font:15px system-ui;padding:24px\">"
                            "<h2>Flaux Studio couldn't start</h2><p>Try installing Flaux again with FlauxSetup.exe. "
                            "What Flaux said:</p><pre style=\"white-space:pre-wrap;color:#ff8a8a\">" + text + "</pre></body>");
            });
        }
    });
    reader.detach();

    w->run();
    CloseHandle(job);                   /* stops Flaux and everything it started */
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return studio(); }
#endif
