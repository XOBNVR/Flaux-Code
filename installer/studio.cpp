/* Flaux Studio.exe: Flaux Studio in a window of its own.
 *
 * A small Windows program (not Electron, and not a browser window). It
 * starts Flaux Studio's helper (python\pythonw.exe flaux studio --host),
 * which prints the address of the Studio's page, and shows that page in its
 * own window with WebView2, the web view that's built into Windows 10 and 11.
 * Closing the window stops the helper and any program that's still running.
 *
 * If WebView2 is missing (only on an old Windows 10 that never updated), it
 * starts `flaux studio` instead, which opens the Studio in Edge or Chrome.
 *
 * Built by installer/fetch-windows-parts.ps1, with the header from Microsoft's
 * WebView2 SDK (webview2_iids.py writes the interface ids it needs);
 * WebView2Loader.dll goes next to it.
 *
 *     "Flaux Studio.exe" [program.flx]
 *     "Flaux Studio.exe" --selftest result.txt
 *         the build's test: start, wait until the Studio is ready, run a
 *         little program through it, save what happened in result.txt (and a
 *         picture of the window in result.txt.png), then close.
 *         Exit code 0 = it all worked, 2 = no WebView2 on this computer.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include <vector>
#include "WebView2.h"
#include "webview2_iids.h"   /* made from WebView2.h by webview2_iids.py */

namespace {

/* ------------------------------------------------------------ COM handlers */

/* WebView2 reports back through small objects with an Invoke method:
   this makes one from a lambda. */
template <class I, class... A>
class Handler final : public I {
    std::function<HRESULT(A...)> fn_;
    LONG refs_ = 1;

public:
    explicit Handler(std::function<HRESULT(A...)> fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, flaux_iid<I>())) {
            *out = static_cast<I *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs_);
        if (!left) delete this;
        return (ULONG)left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn_(a...); }
};

/* Make a handler from a lambda. Whoever makes one hands it to WebView2 and
   then calls give() on it: WebView2 keeps its own reference while it needs it. */
template <class I, class... A, class F>
I *handler(F fn) { return new Handler<I, A...>(std::function<HRESULT(A...)>(fn)); }

template <class I>
void give(I *h) { h->Release(); }

/* Run some JavaScript in the page; `done` gets the result as JSON (or nothing). */
void run_script(ICoreWebView2 *view, const wchar_t *js, std::function<void(const std::wstring &)> done = nullptr) {
    auto *h = handler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
        [done](HRESULT result, LPCWSTR json) -> HRESULT {
            if (done) done(SUCCEEDED(result) && json ? std::wstring(json) : std::wstring());
            return S_OK;
        });
    view->ExecuteScript(js, h);
    give(h);
}

template <class T>
void release(T *&p) {
    if (p) { p->Release(); p = nullptr; }
}

/* ------------------------------------------------------------------ state */

const UINT WM_APP_URL = WM_APP + 1;          /* the helper said the page's address */
const UINT WM_APP_HELPER_GONE = WM_APP + 2;  /* the helper stopped */
const UINT_PTR TIMER_SELFTEST = 1;

HINSTANCE g_inst;
HWND g_win;
std::wstring g_app;           /* the folder Flaux Studio.exe is in */
std::wstring g_file;          /* a program to open (double-clicked .flx) */
std::wstring g_selftest;      /* --selftest result file */
std::wstring g_url, g_origin; /* the Studio page, and its http://127.0.0.1:port */
std::string g_helper_said;    /* what the helper printed (shown if it fails to start) */
HANDLE g_helper, g_helper_input, g_job;
ICoreWebView2Controller *g_ctrl;
ICoreWebView2 *g_view;
bool g_navigated, g_closing, g_dark, g_fullscreen, g_fell_back;
WINDOWPLACEMENT g_place = {sizeof(WINDOWPLACEMENT)};
DWORD g_selftest_start;
int g_selftest_stage;         /* 0 waiting for the Studio, 1 running a program, 2 done */
std::wstring g_selftest_last;

std::wstring widen(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool exists(const std::wstring &path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

void message(const std::wstring &text, UINT icon = MB_ICONERROR) {
    MessageBoxW(g_win, text.c_str(), L"Flaux Studio", MB_OK | icon);
}

/* ---------------------------------------------------------------- selftest */

void selftest_done(int code, const std::wstring &what) {
    std::string text = narrow(what) + "\n";
    HANDLE f = CreateFileW(g_selftest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n;
        WriteFile(f, text.data(), (DWORD)text.size(), &n, nullptr);
        CloseHandle(f);
    }
    KillTimer(g_win, TIMER_SELFTEST);
    g_closing = true;
    if (g_win) DestroyWindow(g_win);
    PostQuitMessage(code);
}

/* -------------------------------------------------------------- the helper */

/* Read what the helper prints: the line with the page's address, or (if it
   can't start) the problem. */
DWORD WINAPI read_helper(LPVOID pipe) {
    char buf[4096];
    DWORD n;
    std::string line;
    bool told = false;
    while (ReadFile((HANDLE)pipe, buf, sizeof buf, &n, nullptr) && n) {
        for (DWORD i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                line += buf[i];
                continue;
            }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t at = line.find("FLAUX-STUDIO-URL ");
            if (!told && at != std::string::npos) {
                told = true;
                PostMessageW(g_win, WM_APP_URL, 0, (LPARAM) new std::wstring(widen(line.substr(at + 17))));
            } else if (!told && g_helper_said.size() < 6000) {
                g_helper_said += line + "\n";
            }
            line.clear();
        }
    }
    if (!told) g_helper_said += line;
    CloseHandle((HANDLE)pipe);
    PostMessageW(g_win, WM_APP_HELPER_GONE, told, 0);
    return 0;
}

bool start_process(const std::wstring &args, bool helper) {
    std::wstring python = g_app + L"\\python\\pythonw.exe";
    std::wstring cmd = L"\"" + python + L"\" \"" + g_app + L"\\flaux\" studio" + args;
    if (!g_file.empty()) cmd += L" \"" + g_file + L"\"";
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (!helper)
        return CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, g_app.c_str(), &si, &pi) &&
               CloseHandle(pi.hThread) && CloseHandle(pi.hProcess);

    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE out_r, out_w, in_r, in_w;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return false;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, g_app.c_str(), &si, &pi);
    CloseHandle(out_w);
    CloseHandle(in_r);
    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(in_w);
        return false;
    }
    /* everything the helper starts (the programs you run) stops when we do */
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (g_job) {
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(g_job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_helper = pi.hProcess;
    g_helper_input = in_w;   /* closing it tells the helper the window has closed */
    HANDLE t = CreateThread(nullptr, 0, read_helper, out_r, 0, nullptr);
    if (t) CloseHandle(t);
    return true;
}

void stop_helper() {
    if (g_helper_input) { CloseHandle(g_helper_input); g_helper_input = nullptr; }
    if (g_helper) {
        if (WaitForSingleObject(g_helper, 3000) == WAIT_TIMEOUT && g_job) TerminateJobObject(g_job, 0);
        CloseHandle(g_helper);
        g_helper = nullptr;
    }
}

/* No WebView2: use `flaux studio`, which opens the Studio in Edge or Chrome. */
void fall_back() {
    if (g_fell_back) return;
    g_fell_back = true;
    if (!g_selftest.empty()) {
        selftest_done(2, L"no-webview2");
        return;
    }
    if (g_job) TerminateJobObject(g_job, 0);
    if (!start_process(L"", false))
        message(L"Flaux Studio couldn't start. Try installing Flaux again with FlauxSetup.exe.");
    g_closing = true;
    DestroyWindow(g_win);
}

/* ---------------------------------------------------------------- window */

void fit_view() {
    if (!g_ctrl) return;
    RECT r;
    GetClientRect(g_win, &r);
    g_ctrl->put_Bounds(r);
}

void set_fullscreen(bool on) {
    if (on == g_fullscreen) return;
    g_fullscreen = on;
    if (on) {
        GetWindowPlacement(g_win, &g_place);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(g_win, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(g_win, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_win, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(g_win, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(g_win, &g_place);
        SetWindowPos(g_win, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

void open_outside(const wchar_t *uri) {
    std::wstring u = uri ? uri : L"";
    if (u.rfind(L"http://", 0) == 0 || u.rfind(L"https://", 0) == 0 || u.rfind(L"mailto:", 0) == 0)
        ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool ours(const wchar_t *uri) {
    std::wstring u = uri ? uri : L"";
    return !g_origin.empty() && u.compare(0, g_origin.size(), g_origin) == 0 &&
           (u.size() == g_origin.size() || u[g_origin.size()] == L'/');
}

void navigate() {
    if (g_view && !g_url.empty() && !g_navigated) {
        g_navigated = true;
        g_view->Navigate(g_url.c_str());
    }
}

void setup_view() {
    ICoreWebView2Settings *settings = nullptr;
    if (SUCCEEDED(g_view->get_Settings(&settings)) && settings) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsZoomControlEnabled(TRUE);
        /* F5 runs your program here, so the browser's own keys (F5 reload,
           Ctrl+P print, ...) are off */
        ICoreWebView2Settings3 *s3 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings3, (void **)&s3)) && s3) {
            s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            s3->Release();
        }
        settings->Release();
    }
    /* the page's colour before it has loaded, so the window doesn't flash white at night */
    ICoreWebView2Controller2 *c2 = nullptr;
    if (SUCCEEDED(g_ctrl->QueryInterface(IID_ICoreWebView2Controller2, (void **)&c2)) && c2) {
        COREWEBVIEW2_COLOR bg = g_dark ? COREWEBVIEW2_COLOR{255, 0x12, 0x14, 0x1c} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
        c2->put_DefaultBackgroundColor(bg);
        c2->Release();
    }
    EventRegistrationToken token;
    auto *on_DocumentTitleChanged =
        handler<ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2 *, IUnknown *>(
            [](ICoreWebView2 *view, IUnknown *) -> HRESULT {
                LPWSTR title = nullptr;
                if (SUCCEEDED(view->get_DocumentTitle(&title)) && title) {
                    SetWindowTextW(g_win, *title ? title : L"Flaux Studio");
                    CoTaskMemFree(title);
                }
                return S_OK;
            });
    g_view->add_DocumentTitleChanged(on_DocumentTitleChanged, &token);
    give(on_DocumentTitleChanged);
    /* links to other websites open in the normal web browser */
    auto *on_NewWindowRequested =
        handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *>(
            [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    open_outside(uri);
                    CoTaskMemFree(uri);
                }
                args->put_Handled(TRUE);
                return S_OK;
            });
    g_view->add_NewWindowRequested(on_NewWindowRequested, &token);
    give(on_NewWindowRequested);
    auto *on_NavigationStarting =
        handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *>(
            [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    if (!ours(uri)) {
                        args->put_Cancel(TRUE);
                        open_outside(uri);
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            });
    g_view->add_NavigationStarting(on_NavigationStarting, &token);
    give(on_NavigationStarting);
    /* a game shown fullscreen in the page fills the screen */
    auto *on_ContainsFullScreenElementChanged =
        handler<ICoreWebView2ContainsFullScreenElementChangedEventHandler, ICoreWebView2 *, IUnknown *>(
            [](ICoreWebView2 *view, IUnknown *) -> HRESULT {
                BOOL full = FALSE;
                view->get_ContainsFullScreenElement(&full);
                set_fullscreen(full != FALSE);
                return S_OK;
            });
    g_view->add_ContainsFullScreenElementChanged(on_ContainsFullScreenElementChanged, &token);
    give(on_ContainsFullScreenElementChanged);
    auto *on_ProcessFailed =
        handler<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *>(
            [](ICoreWebView2 *view, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                COREWEBVIEW2_PROCESS_FAILED_KIND kind;
                if (FAILED(args->get_ProcessFailedKind(&kind))) return S_OK;
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                    if (!g_selftest.empty()) {
                        selftest_done(1, L"the web view stopped");
                        return S_OK;
                    }
                    message(L"Flaux Studio's window stopped working. Open Flaux Studio again (your saved "
                            L"programs are safe).");
                    g_closing = true;
                    DestroyWindow(g_win);
                } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                           kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                    view->Reload();
                }
                return S_OK;
            });
    g_view->add_ProcessFailed(on_ProcessFailed, &token);
    give(on_ProcessFailed);
}

bool start_webview() {
    HMODULE loader = LoadLibraryW((g_app + L"\\WebView2Loader.dll").c_str());
    if (!loader) return false;
    typedef HRESULT(STDAPICALLTYPE * CreateFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                               ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
    CreateFn create = (CreateFn)(void *)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!create) return false;
    /* WebView2 keeps its files (cache, the page's saved settings) here */
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    std::wstring data = (n && n < MAX_PATH ? std::wstring(local) : g_app) + L"\\Flaux\\WebView2";

    auto *env_done = handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                             ICoreWebView2Environment *>([](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
        if (FAILED(result) || !env) {
            fall_back();
            return S_OK;
        }
        auto *ctrl_done = handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                  ICoreWebView2Controller *>([](HRESULT result, ICoreWebView2Controller *ctrl) -> HRESULT {
            if (FAILED(result) || !ctrl || !g_win) {
                fall_back();
                return S_OK;
            }
            g_ctrl = ctrl;
            g_ctrl->AddRef();
            if (FAILED(g_ctrl->get_CoreWebView2(&g_view)) || !g_view) {
                fall_back();
                return S_OK;
            }
            setup_view();
            fit_view();
            g_ctrl->put_IsVisible(TRUE);
            navigate();
            return S_OK;
        });
        HRESULT hr = env->CreateCoreWebView2Controller(g_win, ctrl_done);
        ctrl_done->Release();
        if (FAILED(hr)) fall_back();
        return S_OK;
    });
    HRESULT hr = create(nullptr, data.c_str(), nullptr, env_done);
    env_done->Release();
    return SUCCEEDED(hr);
}

/* The selftest: wait for "No problems found", run a program, check what it said. */
void selftest_done_with_picture(const std::wstring &said) {
    IStream *png = nullptr;
    std::wstring pic = g_selftest + L".png";
    if (FAILED(SHCreateStreamOnFileEx(pic.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, nullptr,
                                      &png)) || !png) {
        selftest_done(0, said);
        return;
    }
    auto *h = handler<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>([png, said](HRESULT) -> HRESULT {
        png->Release();
        selftest_done(0, said);
        return S_OK;
    });
    HRESULT hr = g_view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, png, h);
    give(h);
    if (FAILED(hr)) {
        png->Release();
        selftest_done(0, said);
    }
}

void selftest_tick() {
    if (GetTickCount() - g_selftest_start > 120000) {
        selftest_done(1, L"timeout (stage " + std::to_wstring(g_selftest_stage) + L"): " + g_selftest_last);
        return;
    }
    if (!g_view || !g_navigated || g_selftest_stage > 1) return;
    if (g_selftest_stage == 0) {
        run_script(g_view, L"(document.getElementById('status') || {}).textContent || ''", [](const std::wstring &json) {
            if (g_selftest_stage != 0 || json.empty()) return;
            g_selftest_last = json;
            if (json.find(L"No problems") == std::wstring::npos && json.find(L"Ready") == std::wstring::npos) return;
            g_selftest_stage = 1;
            run_script(g_view, L"code.value = 'say.same \"Hello \"\\nsay \"from Flaux Studio.exe\"\\n';"
                               L"state.lastChecked = null; render(); document.getElementById('btn-run').click();");
        });
        return;
    }
    run_script(g_view, L"(document.getElementById('console') || {}).textContent || ''", [](const std::wstring &json) {
        if (g_selftest_stage != 1 || json.empty()) return;
        g_selftest_last = json;
        if (json.find(L"Hello from Flaux Studio.exe") == std::wstring::npos ||
            json.find(L"Finished") == std::wstring::npos) return;
        g_selftest_stage = 2;
        wchar_t title[256] = L"";
        GetWindowTextW(g_win, title, 256);
        selftest_done_with_picture(L"ok\nwindow: " + std::wstring(title) + L"\noutput: " + json);
    });
}

/* Before closing: warn about changes that aren't saved (the page knows). */
void ask_to_close() {
    if (!g_view || g_closing) {
        g_closing = true;
        DestroyWindow(g_win);
        return;
    }
    run_script(g_view, L"(window.state && state.dirty) ? 1 : 0", [](const std::wstring &json) {
        if (g_closing || !g_win) return;
        if (json == L"1" &&
            MessageBoxW(g_win, L"Your program has changes that aren't saved.\n\nClose Flaux Studio anyway?",
                        L"Flaux Studio", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        g_closing = true;
        DestroyWindow(g_win);
    });
}

UINT dpi_of(HWND w) {
    typedef UINT(WINAPI * DpiFn)(HWND);
    static DpiFn fn = (DpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && w) return fn(w);
    HDC dc = GetDC(nullptr);
    UINT dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        fit_view();
        return 0;
    case WM_SETFOCUS:
        if (g_ctrl) g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_MOVE:
        if (g_ctrl) g_ctrl->NotifyParentWindowPositionChanged();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        UINT dpi = dpi_of(w);
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = MulDiv(640, dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(440, dpi, 96);
        return 0;
    }
    case WM_PAINT: {   /* until the Studio's page shows */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        HBRUSH bg = CreateSolidBrush(g_dark ? RGB(0x12, 0x14, 0x1c) : RGB(255, 255, 255));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        if (!g_navigated) {
            HFONT font = CreateFontW(-MulDiv(18, dpi_of(w), 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_dark ? RGB(0xb8, 0xbc, 0xd0) : RGB(0x5a, 0x5f, 0x75));
            DrawTextW(dc, L"Starting Flaux Studio\u2026", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old);
            DeleteObject(font);
        }
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_APP_URL: {
        std::wstring *url = (std::wstring *)lp;
        g_url = *url;
        delete url;
        size_t slash = g_url.find(L'/', g_url.find(L"://") + 3);
        g_origin = g_url.substr(0, slash);
        navigate();
        return 0;
    }
    case WM_APP_HELPER_GONE:
        if (g_closing) return 0;
        if (!g_selftest.empty()) {
            selftest_done(1, L"the helper stopped: " + widen(g_helper_said));
            return 0;
        }
        message(wp ? L"Flaux Studio's helper stopped. Close this window and open Flaux Studio again."
                   : L"Flaux Studio couldn't start:\n\n" + widen(g_helper_said) +
                         L"\n\nTry installing Flaux again with FlauxSetup.exe.");
        if (!wp) {
            g_closing = true;
            DestroyWindow(w);
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_SELFTEST) selftest_tick();
        return 0;
    case WM_CLOSE:
        ask_to_close();
        return 0;
    case WM_DESTROY:
        KillTimer(w, TIMER_SELFTEST);
        if (g_ctrl) g_ctrl->Close();
        release(g_view);
        release(g_ctrl);
        g_win = nullptr;
        stop_helper();
        if (g_selftest.empty()) PostQuitMessage(0);   /* the selftest posts its own result */
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

bool system_is_dark() {
    DWORD light = 1, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    /* sharp text on high-DPI screens (the manifest asks too) */
    typedef BOOL(WINAPI * AwareFn)(HANDLE);
    AwareFn aware = (AwareFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (aware) aware((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);

    wchar_t exe[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    g_app.assign(exe, n);
    g_app = g_app.substr(0, g_app.find_last_of(L'\\'));

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--selftest" && i + 1 < argc) g_selftest = argv[++i];
        else if (!a.empty() && a[0] != L'-' && g_file.empty()) g_file = a;
    }
    if (argv) LocalFree(argv);

    if (!exists(g_app + L"\\python\\pythonw.exe") || !exists(g_app + L"\\flaux")) {
        MessageBoxW(nullptr, L"Flaux Studio.exe needs the other Flaux files next to it (the python and flauxc "
                             L"folders). Unzip the whole Flaux Studio folder, or install Flaux with FlauxSetup.exe.",
                    L"Flaux Studio", MB_OK | MB_ICONERROR);
        return 1;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_dark = system_is_dark();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"FlauxStudio";
    RegisterClassExW(&wc);

    /* 1300 x 850 (at normal size), but never bigger than the screen */
    UINT dpi = dpi_of(nullptr);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = MulDiv(1300, dpi, 96), wh = MulDiv(850, dpi, 96);
    int maxw = (work.right - work.left) * 95 / 100, maxh = (work.bottom - work.top) * 95 / 100;
    if (ww > maxw) ww = maxw;
    if (wh > maxh) wh = maxh;
    g_win = CreateWindowExW(0, wc.lpszClassName, L"Flaux Studio", WS_OVERLAPPEDWINDOW,
                            work.left + ((work.right - work.left) - ww) / 2, work.top + ((work.bottom - work.top) - wh) / 2,
                            ww, wh, nullptr, nullptr, inst, nullptr);
    if (!g_win) return 1;
    if (g_dark) {
        BOOL on = TRUE;   /* a dark title bar to match */
        DwmSetWindowAttribute(g_win, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof on);
    }
    ShowWindow(g_win, g_selftest.empty() ? show : SW_SHOWNORMAL);
    UpdateWindow(g_win);
    g_selftest_start = GetTickCount();

    if (!start_process(L" --host", true)) {
        message(L"Flaux Studio couldn't start its helper. Try installing Flaux again with FlauxSetup.exe.");
        return 1;
    }
    if (!start_webview()) fall_back();
    if (!g_selftest.empty() && !g_fell_back) SetTimer(g_win, TIMER_SELFTEST, 500, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    stop_helper();
    if (g_job) CloseHandle(g_job);
    CoUninitialize();
    return (int)m.wParam;
}
