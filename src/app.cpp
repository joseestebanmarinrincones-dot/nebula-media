#include "common.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <functional>

namespace nebula {

namespace {

constexpr int kFilterCount = 4;
constexpr int kMenuItemOpen = 40001;

constexpr int kMenuItemReveal = 40002;
constexpr int kMenuItemCopyPath = 40003;
constexpr int kMenuItemCopyDrive = 40004;
constexpr int kMenuItemDelete = 40005;
constexpr int kMenuItemProperties = 40006;
constexpr int kMenuItemRefresh = 40007;
constexpr int kMenuItemEditAudio = 40008;
constexpr int kMenuItemPlay = 40009;

constexpr int kFilterWidths[kFilterCount] = {54, 80, 64, 60};

bool InRect(const Gdiplus::RectF& r, float x, float y) {
    return x >= r.X && y >= r.Y && x < r.GetRight() && y < r.GetBottom();
}

Gdiplus::RectF InflateRect(const Gdiplus::RectF& r, float amount) {
    return Gdiplus::RectF(r.X - amount, r.Y - amount, r.Width + amount * 2,
                          r.Height + amount * 2);
}

void AddZone(Layout& layout, int id, const Gdiplus::RectF& rect) {
    layout.zones.push_back(Zone{id, rect});
}

wchar_t FoldChar(wchar_t c) {
    c = static_cast<wchar_t>(std::towlower(c));
    switch (c) {
        case 0x00E1: return L'a';
        case 0x00E9: return L'e';
        case 0x00ED: return L'i';
        case 0x00F3: return L'o';
        case 0x00FA: return L'u';
        case 0x00FC: return L'u';
        case 0x00F1: return L'n';
        case 0x00E0: return L'a';
        case 0x00E8: return L'e';
        case 0x00EC: return L'i';
        case 0x00F2: return L'o';
        case 0x00F9: return L'u';
        default: return c;
    }
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (FoldChar(haystack[i + j]) != FoldChar(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool MatchesItem(const MediaItem& item, Filter filter, const std::wstring& query) {
    if (!query.empty() && !ContainsNoCase(item.name, query)) return false;
    if (item.isDir) return true;
    switch (filter) {
        case Filter::Images: return item.kind == ItemKind::Image;
        case Filter::Videos: return item.kind == ItemKind::Video;
        case Filter::Audio: return item.kind == ItemKind::Audio;
        case Filter::All:
        default: return true;
    }
}

bool HasAttribute(DWORD attributes, DWORD flag) {
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & flag) != 0;
}

std::wstring DefaultLibraryPath() {
    std::wstring pictures = KnownFolderPath(FOLDERID_Pictures);
    if (DirExists(pictures)) return pictures;
    std::wstring desktop = KnownFolderPath(FOLDERID_Desktop);
    if (DirExists(desktop)) return desktop;
    std::wstring profile = KnownFolderPath(FOLDERID_Profile);
    if (DirExists(profile)) return profile;
    return L"C:\\";
}

}  // namespace

App::~App() {
    if (thumbs_) {
        thumbs_->Stop();
        thumbs_.reset();
    }
    if (player_) {
        player_->Close();
        player_.reset();
    }
    if (memDC_) {
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
}

float App::S(float value) const {
    return value * static_cast<float>(dpi_) / 96.0f;
}

int App::ShadowMargin() const {
    return maximized_ ? 0 : static_cast<int>(S(22) + 0.5f);
}

ViewState& App::View() {
    return tab_ == Tab::Drive ? drive_ : library_;
}

const ViewState& App::View() const {
    return tab_ == Tab::Drive ? drive_ : library_;
}

Filter& App::CurrentFilter() {
    return View().filter;
}

std::vector<int> App::VisibleIndices(const ViewState& view) const {
    std::vector<int> result;
    result.reserve(view.items.size());
    for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
        if (MatchesItem(view.items[i], view.filter, view.search)) result.push_back(i);
    }
    return result;
}

void App::LoadQuickFolders() {
    quick_.clear();
    struct Entry {
        const wchar_t* label;
        const wchar_t* glyph;
        REFKNOWNFOLDERID id;
    };
    const Entry entries[] = {
        {L"Escritorio", L"\uE7F4", FOLDERID_Desktop},
        {L"Imágenes", L"\uE8B9", FOLDERID_Pictures},
        {L"Vídeos", L"\uE714", FOLDERID_Videos},
        {L"Música", L"\uE8D6", FOLDERID_Music},
        {L"Descargas", L"\uE896", FOLDERID_Downloads},
        {L"Documentos", L"\uE8A5", FOLDERID_Documents},
    };
    for (const auto& entry : entries) {
        std::wstring path = KnownFolderPath(entry.id);
        if (!DirExists(path)) continue;
        bool duplicate = false;
        for (const auto& existing : quick_) {
            if (SamePath(existing.path, path)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) quick_.push_back(QuickFolder{entry.label, entry.glyph, path});
    }
}

void App::LoadSettings() {
    opacity_ = static_cast<int>(RegReadDword(L"Opacity", 232));
    if (opacity_ < 100) opacity_ = 100;
    if (opacity_ > 255) opacity_ = 255;
    pin_ = RegReadDword(L"Pin", 1) != 0;
    zoom_ = static_cast<int>(RegReadDword(L"Zoom", 100));
    zoom_ = std::clamp(((zoom_ + 5) / 10) * 10, 80, 160);
    library_.path = RegReadStr(L"Library", L"");
    if (!DirExists(library_.path)) library_.path = DefaultLibraryPath();
    driveRoot_ = RegReadStr(L"DriveRoot", L"");
    if (!driveRoot_.empty() && !DirExists(driveRoot_)) driveRoot_.clear();
    if (tab_ == Tab::Drive) tab_ = Tab::Library;
}

bool App::Create(HINSTANCE instance, bool showWindow) {
    instance_ = instance;
    SetCurrentProcessExplicitAppUserModelID(L"Nebula.Media.Manager");

    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    if (dpi_ <= 0) dpi_ = 96;

    LoadSettings();
    LoadQuickFolders();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &App::StaticWndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"NebulaMediaWindow";
    if (!RegisterClassExW(&wc)) return false;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (work.right <= work.left || work.bottom <= work.top) {
        work = RECT{0, 0, 1920, 1080};
    }

    int defaultPanelW = static_cast<int>(S(1200));
    int defaultPanelH = static_cast<int>(S(780));
    int maxPanelW = static_cast<int>((work.right - work.left) * 0.94f) - 2 * ShadowMargin();
    int maxPanelH = static_cast<int>((work.bottom - work.top) * 0.94f) - 2 * ShadowMargin();
    if (defaultPanelW > maxPanelW) defaultPanelW = maxPanelW;
    if (defaultPanelH > maxPanelH) defaultPanelH = maxPanelH;

    int windowW = defaultPanelW + 2 * ShadowMargin();
    int windowH = defaultPanelH + 2 * ShadowMargin();
    int x = work.left + ((work.right - work.left) - windowW) / 2;
    int y = work.top + ((work.bottom - work.top) - windowH) / 2;

    const DWORD savedW = RegReadDword(L"WinW", 0);
    const DWORD savedH = RegReadDword(L"WinH", 0);
    const int minWindowW = static_cast<int>(S(920)) + 2 * ShadowMargin();
    const int minWindowH = static_cast<int>(S(610)) + 2 * ShadowMargin();
    if (savedW >= static_cast<DWORD>(minWindowW) && savedH >= static_cast<DWORD>(minWindowH)) {
        windowW = static_cast<int>(savedW);
        windowH = static_cast<int>(savedH);
    }
    if (RegReadDword(L"HasPos", 0)) {
        int savedX = static_cast<int>(RegReadDword(L"WinX", 0));
        int savedY = static_cast<int>(RegReadDword(L"WinY", 0));
        RECT savedRect{savedX, savedY, savedX + windowW, savedY + windowH};
        if (MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL)) {
            x = savedX;
            y = savedY;
        }
    }

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName,
                            L"Nébula Media", WS_POPUP, x, y, windowW, windowH, nullptr,
                            nullptr, instance_, this);
    if (!hwnd_) return false;

    DragAcceptFiles(hwnd_, TRUE);
    DWORD windowDpi = GetDpiForWindow(hwnd_);
    if (windowDpi) dpi_ = static_cast<int>(windowDpi);

    library_.history.clear();
    library_.hist = -1;
    if (!DirExists(library_.path)) library_.path = DefaultLibraryPath();
    library_.history.push_back(library_.path);
    library_.hist = 0;
    ScanFolder(library_);

    if (driveRoot_.empty()) {
        std::wstring mount;
        std::wstring detected = DetectGoogleDrive(mount);
        if (!detected.empty()) {
            driveRoot_ = detected;
            driveMount_ = mount;
        }
    }
    if (!driveRoot_.empty() && DirExists(driveRoot_)) {
        drive_.path = driveRoot_;
        drive_.history.clear();
        drive_.history.push_back(drive_.path);
        drive_.hist = 0;
        ScanFolder(drive_);
        DeriveDriveMount();
    }

    thumbs_ = std::make_unique<ThumbnailLoader>(hwnd_);
    thumbs_->Start();

    ready_ = true;
    Render();
    if (showWindow) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        ApplyTopmost();
        SetTimer(hwnd_, 1, 500, nullptr);
        SetTimer(hwnd_, 4, 4000, nullptr);
        UpdateUninstallVersion();
        if (RegReadDword(L"Maximized", 0)) {
            ToggleMaximize();
        }
        Render();
    }
    return true;
}

void App::StartUpdateCheck() {
    if (updateCheckStarted_ || !hwnd_) return;
    updateCheckStarted_ = true;
    const HWND target = hwnd_;
    const std::wstring current = NEBULA_VERSION_W;
    std::thread([target, current]() {
        auto* result = new UpdateResult(CheckForUpdatesBlocking(current));
        if (!PostMessageW(target, WM_APP_UPDATE, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void App::UpdateUninstallVersion() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NebulaMedia",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    const std::wstring version = NEBULA_VERSION_W;
    RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(version.c_str()),
                   static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

void App::PrepareSnapshot(const std::wstring& mode, const std::wstring& argument) {
    if (mode == L"drive") {
        SwitchTab(Tab::Drive);
    } else if (mode == L"settings") {
        settingsOpen_ = true;
    } else if (mode == L"audio" && !argument.empty()) {
        OpenAudioEditor(argument);
        if (editorOpen_) {
            const int64_t frames = ClipFrames(audio_);
            if (frames > 8) {
                selectionStart_ = frames / 4;
                selectionEnd_ = frames * 3 / 4;
                playhead_ = selectionStart_;
                hasSelection_ = true;
            }
        }
    } else if (mode == L"search" && !argument.empty()) {
        View().search = argument;
        searchFocused_ = true;
        SearchChanged(true);
    } else if (mode == L"deep" && !argument.empty()) {
        View().search = argument;
        View().recursive = true;
        searchFocused_ = true;
        RunDeepSearch(View());
    } else if (mode == L"scroll") {
        View().scroll = static_cast<int>(S(320));
    } else if (mode == L"update") {
        updateReady_ = true;
        updateVersion_ = L"1.0.99";
    }
    Render();
}

bool App::SaveSnapshot(const std::wstring& path) {
    if (!bits_ || bufferW_ <= 0 || bufferH_ <= 0) return false;

    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes);
    if (encoderBytes == 0) return false;
    std::vector<BYTE> storage(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return false;
    }
    CLSID png{};
    bool found = false;
    for (UINT i = 0; i < encoderCount; ++i) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            png = encoders[i].Clsid;
            found = true;
            break;
        }
    }
    if (!found) return false;

    Gdiplus::Bitmap bitmap(bufferW_, bufferH_, bufferW_ * 4,
                           PixelFormat32bppPARGB, static_cast<BYTE*>(bits_));
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;
    return bitmap.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

int App::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app) return app->WndProc(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!hwnd_) hwnd_ = hwnd;

    switch (message) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            return TRUE;
        case WM_CREATE:
            hwnd_ = hwnd;
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            EnsureBuffer(LOWORD(lParam), HIWORD(lParam));
            if (ready_) Render();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            if (ready_) Render();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnMouseMove(pt, (wParam & MK_LBUTTON) != 0);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnLButtonDown(pt, (wParam & MK_CONTROL) != 0, (wParam & MK_SHIFT) != 0);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnLButtonUp(pt);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            OnLButtonDblClk(pt);
            return 0;
        }
        case WM_RBUTTONUP: {
            if (editorOpen_ || settingsOpen_) return 0;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HitInfo hit = HitTest(pt);
            if (hit.card >= 0) {
                if (!IsSelected(View().items[hit.card].path)) {
                    SelectCard(hit.card, false, false);
                }
                POINT screen{pt.x, pt.y};
                ClientToScreen(hwnd_, &screen);
                ShowContextMenu(hit.card);
                (void)screen;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (editorOpen_ || settingsOpen_) return 0;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &pt);
            Layout layout = BuildLayout();
            if (InRect(layout.content, static_cast<float>(pt.x),
                       static_cast<float>(pt.y))) {
                if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
                    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    if (delta != 0) SetZoom(zoom_ + (delta > 0 ? 10 : -10));
                } else {
                    OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
                }
            }
            return 0;
        }
        case WM_CHAR:
            OnChar(static_cast<wchar_t>(wParam));
            return 0;
        case WM_KEYDOWN: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            OnKeyDown(wParam, ctrl, shift, alt);
            return 0;
        }
        case WM_TIMER:
            OnTimer(wParam);
            return 0;
        case WM_APP_THUMB: {
            if (!ready_ || IsIconic(hwnd_)) return 0;
            const uint64_t now = GetTickCount64();
            if (now - lastThumbPaint_ >= 40) {
                lastThumbPaint_ = now;
                Render();
            }
            return 0;
        }
        case WM_APP_UPDATE: {
            auto* result = reinterpret_cast<UpdateResult*>(lParam);
            if (result) {
                if (result->downloaded && !result->path.empty()) {
                    updateReady_ = true;
                    updateVersion_ = result->version;
                    updatePath_ = result->path;
                    SetToast(L"Actualización " + updateVersion_ +
                             L" lista · pulsa Actualizar.");
                }
                delete result;
                Render();
            }
            return 0;
        }
        case WM_DROPFILES:
            OnDroppedFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &pt);
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int margin = ShadowMargin();
            if (pt.x < margin || pt.y < margin ||
                pt.x >= client.right - margin || pt.y >= client.bottom - margin) {
                return HTTRANSPARENT;
            }
            break;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) != HTCLIENT) break;
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            HitInfo hit = HitTest(pt);
            if (hit.resizeEdge) {
                LPWSTR cursor = IDC_ARROW;
                const bool left = (hit.resizeEdge & 1) != 0;
                const bool right = (hit.resizeEdge & 2) != 0;
                const bool top = (hit.resizeEdge & 4) != 0;
                const bool bottom = (hit.resizeEdge & 8) != 0;
                if ((left && top) || (right && bottom)) {
                    cursor = IDC_SIZENWSE;
                } else if ((right && top) || (left && bottom)) {
                    cursor = IDC_SIZENESW;
                } else if (left || right) {
                    cursor = IDC_SIZEWE;
                } else {
                    cursor = IDC_SIZENS;
                }
                SetCursor(LoadCursorW(nullptr, cursor));
                return TRUE;
            }
            if (hit.id == zid::Waveform) {
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (hit.id == zid::Search) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
            if (IsClickable(hit.id) || hit.card >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MAXIMIZE) {
                if (!maximized_) ToggleMaximize();
                return 0;
            }
            if ((wParam & 0xFFF0) == SC_RESTORE) {
                if (!IsIconic(hwnd_) && maximized_) {
                    ToggleMaximize();
                    return 0;
                }
                break;
            }
            break;
        case WM_ACTIVATE:
            if (pin_ && !draggingOut_) ApplyTopmost();
            break;
        case WM_KILLFOCUS:
            if (searchFocused_) {
                searchFocused_ = false;
                Render();
            }
            break;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (!maximized_ && suggested) {
                SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (ready_) Render();
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (ready_) Render();
            return 0;
        case WM_CAPTURECHANGED:
            if (movingWindow_ || resizingWindow_ || waveDragging_) {
                movingWindow_ = false;
                resizingWindow_ = false;
                waveDragging_ = false;
                Render();
            }
            return 0;
        case WM_CLOSE:
            if (editorOpen_ && !ConfirmCloseEditor()) return 0;
            SaveSettings();
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            KillTimer(hwnd_, 2);
            KillTimer(hwnd_, 3);
            KillTimer(hwnd_, 4);
            if (thumbs_) thumbs_->Stop();
            if (player_) player_->Close();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void App::ApplyTopmost() {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, pin_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::SetPin(bool pin) {
    pin_ = pin;
    RegWriteDword(L"Pin", pin_ ? 1 : 0);
    ApplyTopmost();
    Render();
}

void App::SaveWindowPlacement() {
    if (!hwnd_) return;
    RECT rect{};
    if (maximized_ && restoreRect_.right > restoreRect_.left) {
        rect = restoreRect_;
    } else if (IsIconic(hwnd_)) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(hwnd_, &placement)) return;
        rect = placement.rcNormalPosition;
    } else {
        GetWindowRect(hwnd_, &rect);
    }
    RegWriteDword(L"HasPos", 1);
    RegWriteDword(L"WinX", static_cast<DWORD>(rect.left));
    RegWriteDword(L"WinY", static_cast<DWORD>(rect.top));
    RegWriteDword(L"WinW", static_cast<DWORD>(rect.right - rect.left));
    RegWriteDword(L"WinH", static_cast<DWORD>(rect.bottom - rect.top));
    RegWriteDword(L"Maximized", maximized_ ? 1 : 0);
}

void App::SaveSettings() {
    SaveWindowPlacement();
    RegWriteDword(L"Opacity", static_cast<DWORD>(opacity_));
    RegWriteDword(L"Pin", pin_ ? 1 : 0);
    RegWriteDword(L"Zoom", static_cast<DWORD>(zoom_));
    RegWriteStr(L"Library", library_.path);
    RegWriteStr(L"DriveRoot", driveRoot_);
}

void App::SetZoom(int zoomPercent) {
    zoomPercent = std::clamp(zoomPercent, 80, 160);
    if (zoomPercent == zoom_) return;
    zoom_ = zoomPercent;
    RegWriteDword(L"Zoom", static_cast<DWORD>(zoom_));
    View().scroll = 0;
    hoverCard_ = -1;
    Render();
}

void App::InstallUpdateNow() {
    if (!updateReady_) return;
    if (!FileExists(updatePath_)) {
        updateReady_ = false;
        SetToast(L"El archivo de actualización ya no está disponible.", true);
        return;
    }

    const std::wstring message =
        L"Actualización " + updateVersion_ +
        L" descargada.\n\n¿Quieres instalarla ahora?\n"
        L"La aplicación se cerrará y se reiniciará actualizada.";
    if (MessageBoxW(hwnd_, message.c_str(), L"Nébula Media",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1) != IDYES) {
        return;
    }
    if (editorOpen_ && !ConfirmCloseEditor()) return;

    SaveSettings();
    std::wstring error;
    if (!LaunchSelfUpdate(updatePath_, error, true)) {
        MessageBoxW(hwnd_,
                    (L"No se pudo iniciar la actualización.\n\n" + error).c_str(),
                    L"Nébula Media", MB_OK | MB_ICONWARNING);
        return;
    }
    DestroyWindow(hwnd_);
}

void App::ToggleMaximize() {
    if (!hwnd_) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    if (!maximized_) {
        if (!GetWindowRect(hwnd_, &restoreRect_)) return;
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
            return;
        }
        maximized_ = true;
        SetWindowPos(hwnd_, nullptr, info.rcWork.left, info.rcWork.top,
                     info.rcWork.right - info.rcWork.left,
                     info.rcWork.bottom - info.rcWork.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        maximized_ = false;
        if (restoreRect_.right > restoreRect_.left) {
            SetWindowPos(hwnd_, nullptr, restoreRect_.left, restoreRect_.top,
                         restoreRect_.right - restoreRect_.left,
                         restoreRect_.bottom - restoreRect_.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    RegWriteDword(L"Maximized", maximized_ ? 1 : 0);
    Render();
}

void App::ScanFolder(ViewState& view) {
    if (thumbs_) thumbs_->ClearQueue();
    view.items.clear();
    view.deep = false;
    view.scroll = std::max(0, view.scroll);
    selection_.clear();
    anchorPath_.clear();
    hoverCard_ = -1;

    if (view.path.empty()) return;
    if (!DirExists(view.path)) {
        SetToast(L"La carpeta ya no existe.", true);
        return;
    }

    std::wstring pattern = JoinPath(view.path, L"*");
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        SetToast(L"No se pudo leer esta carpeta.", true);
        return;
    }

    int found = 0;
    bool limited = false;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if (HasAttribute(data.dwFileAttributes, FILE_ATTRIBUTE_HIDDEN) ||
            HasAttribute(data.dwFileAttributes, FILE_ATTRIBUTE_SYSTEM)) {
            continue;
        }

        MediaItem item{};
        item.name = data.cFileName;
        item.path = JoinPath(view.path, item.name);
        item.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        item.modified = data.ftLastWriteTime;
        item.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        item.ext = item.isDir ? L"" : ExtensionOf(item.name);
        item.kind = KindForFile(item.name, item.isDir);
        if (!item.isDir && item.kind == ItemKind::Other) continue;
        item.cloudOnly = !item.isDir && IsCloudPlaceholder(data.dwFileAttributes);
        view.items.push_back(std::move(item));

        if (++found >= 8000) {
            limited = true;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);

    std::stable_sort(view.items.begin(), view.items.end(),
                     [](const MediaItem& a, const MediaItem& b) {
                         if (a.isDir != b.isDir) return a.isDir;
                         return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
                     });

    if (limited) {
        SetToast(L"Esta carpeta es muy grande: se muestran los primeros 8.000 elementos.");
    }
}

void App::RunDeepSearch(ViewState& view) {
    if (view.search.empty()) {
        if (view.deep) ScanFolder(view);
        return;
    }
    if (!DirExists(view.path)) {
        SetToast(L"La carpeta ya no existe.", true);
        return;
    }

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::vector<MediaItem> results;
    results.reserve(128);
    int visited = 0;
    int maxVisited = 25000;
    int maxResults = 3000;
    bool limited = false;

    std::function<void(const std::wstring&, int)> walk = [&](const std::wstring& dir,
                                                             int depth) {
        if (limited) return;
        std::wstring pattern = JoinPath(dir, L"*");
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(data.cFileName, L".") == 0 ||
                wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            if (HasAttribute(data.dwFileAttributes, FILE_ATTRIBUTE_HIDDEN) ||
                HasAttribute(data.dwFileAttributes, FILE_ATTRIBUTE_SYSTEM)) {
                continue;
            }
            if (++visited > maxVisited) {
                limited = true;
                break;
            }

            const bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool reparse =
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            std::wstring name = data.cFileName;

            if (ContainsNoCase(name, view.search)) {
                MediaItem item{};
                item.name = name;
                item.path = JoinPath(dir, name);
                item.isDir = isDir;
                item.modified = data.ftLastWriteTime;
                item.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                            data.nFileSizeLow;
                item.ext = isDir ? L"" : ExtensionOf(name);
                item.kind = KindForFile(name, isDir);
                if (!isDir && item.kind == ItemKind::Other) {
                    // Ignore executables, games, documents and unknown files.
                } else {
                    item.cloudOnly = !isDir && IsCloudPlaceholder(data.dwFileAttributes);
                    item.context = RelativeTo(dir, view.path);
                    results.push_back(std::move(item));
                    if (static_cast<int>(results.size()) >= maxResults) {
                        limited = true;
                        break;
                    }
                }
            }

            if (isDir && !reparse && depth < 7) {
                walk(JoinPath(dir, name), depth + 1);
                if (limited) break;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    };

    walk(view.path, 0);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    std::stable_sort(results.begin(), results.end(),
                     [](const MediaItem& a, const MediaItem& b) {
                         if (a.isDir != b.isDir) return a.isDir;
                         return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
                     });

    view.items = std::move(results);
    view.deep = true;
    view.scroll = 0;
    selection_.clear();
    anchorPath_.clear();
    hoverCard_ = -1;
    if (limited) {
        SetToast(L"Búsqueda limitada a 3.000 resultados en 25.000 archivos.");
    }
}

void App::Navigate(ViewState& view, const std::wstring& path, bool addHistory) {
    if (path.empty() || !DirExists(path)) {
        SetToast(L"No se pudo abrir esa carpeta.", true);
        return;
    }
    if (SamePath(view.path, path)) {
        RefreshCurrent();
        return;
    }

    view.path = path;
    view.search.clear();
    view.deep = false;
    view.scroll = 0;
    searchFocused_ = false;
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);
    selection_.clear();
    anchorPath_.clear();
    hoverCard_ = -1;

    if (addHistory) {
        if (view.hist >= 0 && view.hist + 1 < static_cast<int>(view.history.size())) {
            view.history.resize(view.hist + 1);
        }
        view.history.push_back(path);
        view.hist = static_cast<int>(view.history.size()) - 1;
    }

    ScanFolder(view);
    Render();
}

void App::GoBack() {
    ViewState& view = View();
    if (view.hist <= 0) return;
    view.hist--;
    if (view.hist < 0 || view.hist >= static_cast<int>(view.history.size())) return;
    std::wstring path = view.history[view.hist];
    view.history.resize(view.hist + 1);
    view.path = path;
    view.search.clear();
    view.deep = false;
    view.scroll = 0;
    selection_.clear();
    hoverCard_ = -1;
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);
    ScanFolder(view);
    Render();
}

void App::GoUp() {
    ViewState& view = View();
    std::wstring parent = ParentPath(view.path);
    if (parent.empty() || SamePath(parent, view.path)) return;
    Navigate(view, parent, true);
}

void App::RefreshCurrent() {
    ViewState& view = View();
    if (tab_ == Tab::Drive) {
        if (driveRoot_.empty() || !DirExists(driveRoot_)) {
            drive_.items.clear();
            drive_.deep = false;
            drive_.path.clear();
            SetToast(L"Google Drive no está disponible.", true);
            Render();
            return;
        }
        if (!DirExists(view.path)) {
            SetDriveRoot(driveRoot_, false);
            SetToast(L"Se volvió a abrir Mi unidad.", false);
            Render();
            return;
        }
    } else if (!DirExists(view.path)) {
        std::wstring fallback = DefaultLibraryPath();
        library_.path = fallback;
        library_.history.clear();
        library_.history.push_back(fallback);
        library_.hist = 0;
        ScanFolder(library_);
        SetToast(L"La carpeta anterior no existe. Se abrió " + fallback, true);
        Render();
        return;
    }

    if (view.deep && !view.search.empty()) {
        RunDeepSearch(view);
    } else {
        ScanFolder(view);
    }
    Render();
}

void App::SwitchTab(Tab tab) {
    if (tab == tab_) return;
    if (editorOpen_) {
        if (!ConfirmCloseEditor()) return;
        CloseAudioEditor();
    }
    if (tab == Tab::Drive && !driveRoot_.empty() && !DirExists(driveRoot_)) {
        driveRoot_.clear();
    }
    if (tab == Tab::Drive && driveRoot_.empty()) {
        std::wstring mount;
        std::wstring detected = DetectGoogleDrive(mount);
        if (!detected.empty()) {
            driveRoot_ = detected;
            driveMount_ = mount;
        }
    }
    tab_ = tab;
    settingsOpen_ = false;
    searchFocused_ = false;
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);
    if (tab_ == Tab::Drive && !driveRoot_.empty() &&
        (drive_.path.empty() || !DirExists(drive_.path))) {
        drive_.path = driveRoot_;
        drive_.history.clear();
        drive_.history.push_back(drive_.path);
        drive_.hist = 0;
        ScanFolder(drive_);
        DeriveDriveMount();
    }
    Render();
}

void App::SetDriveRoot(const std::wstring& path, bool navigate) {
    if (!DirExists(path)) {
        SetToast(L"Esa carpeta de Drive no está disponible.", true);
        return;
    }
    driveRoot_ = path;
    DeriveDriveMount();
    RegWriteStr(L"DriveRoot", driveRoot_);

    drive_.path = path;
    drive_.search.clear();
    drive_.deep = false;
    drive_.scroll = 0;
    drive_.filter = Filter::All;
    drive_.history.clear();
    drive_.history.push_back(path);
    drive_.hist = 0;
    ScanFolder(drive_);
    if (navigate) {
        tab_ = Tab::Drive;
        settingsOpen_ = false;
        searchFocused_ = false;
        pendingDeep_ = false;
        KillTimer(hwnd_, 3);
    }
    Render();
}

void App::DeriveDriveMount() {
    driveMount_.clear();
    if (driveRoot_.empty()) return;
    std::wstring leaf = FileNameOf(driveRoot_);
    if (_wcsicmp(leaf.c_str(), L"My Drive") == 0 ||
        _wcsicmp(leaf.c_str(), L"Mi unidad") == 0) {
        driveMount_ = ParentPath(driveRoot_);
        return;
    }
    std::wstring parent = ParentPath(driveRoot_);
    if (DirExists(JoinPath(parent, L"Shared drives")) ||
        DirExists(JoinPath(parent, L"Unidades compartidas")) ||
        DirExists(JoinPath(parent, L"Other computers")) ||
        DirExists(JoinPath(parent, L"Otros equipos"))) {
        driveMount_ = parent;
    }
}

std::vector<std::wstring> App::DriveSharedCandidates() const {
    std::vector<std::wstring> result;
    if (driveRoot_.empty()) return result;
    std::wstring mount = driveMount_.empty() ? ParentPath(driveRoot_) : driveMount_;
    if (mount.empty()) return result;
    const wchar_t* sharedNames[] = {L"Shared drives", L"Unidades compartidas"};
    const wchar_t* otherNames[] = {L"Other computers", L"Otros equipos"};
    for (const wchar_t* name : sharedNames) {
        std::wstring p = JoinPath(mount, name);
        if (DirExists(p)) {
            result.push_back(p);
            break;
        }
    }
    for (const wchar_t* name : otherNames) {
        std::wstring p = JoinPath(mount, name);
        if (DirExists(p)) {
            result.push_back(p);
            break;
        }
    }
    return result;
}

void App::DetectDrive(bool notify) {
    std::wstring mount;
    std::wstring found = DetectGoogleDrive(mount);
    if (found.empty()) {
        if (notify) {
            SetToast(
                L"No se encontró Google Drive. Instala Drive para escritorio o elige la carpeta.",
                true);
        }
        return;
    }
    driveRoot_ = found;
    driveMount_ = mount;
    SetDriveRoot(found, true);
    RegWriteStr(L"DriveRoot", driveRoot_);
    if (notify) SetToast(L"Google Drive conectado: " + FileNameOf(found), false);
}

void App::ChooseDriveFolder() {
    std::wstring initial = driveRoot_.empty() ? KnownFolderPath(FOLDERID_Profile)
                                              : driveRoot_;
    std::wstring picked;
    if (PickFolder(hwnd_, L"Elige la carpeta de Google Drive (Mi unidad)", initial,
                   picked)) {
        SetDriveRoot(picked, true);
        SetToast(L"Carpeta de Drive actualizada.", false);
    }
}

void App::OpenDriveWeb() {
    ShellExecuteW(hwnd_, L"open", L"https://drive.google.com/drive/my-drive", nullptr,
                  nullptr, SW_SHOWNORMAL);
}

void App::SetOpacity(int alpha) {
    opacity_ = std::max(100, std::min(255, alpha));
    Render();
}

void App::SetToast(const std::wstring& text, bool error) {
    toast_ = text;
    toastIsError_ = error;
    toastUntil_ = GetTickCount64() + 4600;
    if (ready_) Render();
}

bool App::ToastActive() const {
    return !toast_.empty() && GetTickCount64() < toastUntil_;
}

bool App::IsSelected(const std::wstring& path) const {
    return selection_.find(path) != selection_.end();
}

std::vector<std::wstring> App::SelectedPaths() const {
    std::vector<std::wstring> result;
    const ViewState& view = View();
    for (const auto& item : view.items) {
        if (IsSelected(item.path)) result.push_back(item.path);
    }
    return result;
}

bool App::SelectionIsSingleAudio() const {
    if (selection_.size() != 1) return false;
    const ViewState& view = View();
    for (const auto& item : view.items) {
        if (IsSelected(item.path)) return !item.isDir && item.kind == ItemKind::Audio;
    }
    return false;
}

void App::SelectCard(int index, bool ctrl, bool shift) {
    ViewState& view = View();
    if (index < 0 || index >= static_cast<int>(view.items.size())) return;
    const MediaItem& item = view.items[index];

    if (shift && !anchorPath_.empty()) {
        std::vector<int> visible = VisibleIndices(view);
        int anchorPos = -1;
        int targetPos = -1;
        for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
            if (SamePath(view.items[visible[i]].path, anchorPath_)) anchorPos = i;
            if (visible[i] == index) targetPos = i;
        }
        if (anchorPos >= 0 && targetPos >= 0) {
            if (!ctrl) selection_.clear();
            int begin = std::min(anchorPos, targetPos);
            int end = std::max(anchorPos, targetPos);
            for (int i = begin; i <= end; ++i) {
                selection_.insert(view.items[visible[i]].path);
            }
            hoverCard_ = index;
            Render();
            return;
        }
    }

    if (ctrl) {
        if (IsSelected(item.path)) {
            selection_.erase(item.path);
        } else {
            selection_.insert(item.path);
            anchorPath_ = item.path;
        }
    } else {
        selection_.clear();
        selection_.insert(item.path);
        anchorPath_ = item.path;
    }
    hoverCard_ = index;
    Render();
}

void App::OpenItem(const MediaItem& item) {
    if (item.isDir) {
        Navigate(View(), item.path, true);
        return;
    }
    if (item.kind == ItemKind::Audio) {
        OpenAudioEditor(item.path);
        return;
    }
    OpenWithDefault(hwnd_, item.path);
}

void App::OpenSelected() {
    const ViewState& view = View();
    for (const auto& item : view.items) {
        if (IsSelected(item.path)) {
            OpenItem(item);
            return;
        }
    }
}

void App::OnCardDoubleClick(int index) {
    ViewState& view = View();
    if (index < 0 || index >= static_cast<int>(view.items.size())) return;
    OpenItem(view.items[index]);
}

void App::OnClickCard(int index, bool ctrl, bool shift) {
    SelectCard(index, ctrl, shift);
}

void App::ShowContextMenu(int index) {
    ViewState& view = View();
    if (index < 0 || index >= static_cast<int>(view.items.size())) return;
    std::vector<std::wstring> paths = SelectedPaths();
    if (paths.empty()) {
        paths.push_back(view.items[index].path);
    }
    const bool single = paths.size() == 1;
    const MediaItem& item = view.items[index];
    const bool audio = single && !item.isDir && item.kind == ItemKind::Audio;

    HMENU menu = CreatePopupMenu();
    if (audio) {
        AppendMenuW(menu, MF_STRING, kMenuItemEditAudio, L"Editar audio…");
        AppendMenuW(menu, MF_STRING, kMenuItemPlay, L"Reproducir");
        SetMenuDefaultItem(menu, kMenuItemEditAudio, FALSE);
    } else {
        AppendMenuW(menu, MF_STRING, kMenuItemOpen, L"Abrir");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuItemReveal, L"Mostrar en el Explorer");
    AppendMenuW(menu, MF_STRING, kMenuItemCopyPath, L"Copiar ruta");
    if (tab_ == Tab::Library && !driveRoot_.empty() && DirExists(driveRoot_)) {
        AppendMenuW(menu, MF_STRING, kMenuItemCopyDrive, L"Copiar a Google Drive");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuItemDelete, L"Enviar a la papelera");
    AppendMenuW(menu, MF_STRING, kMenuItemProperties, L"Propiedades");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuItemRefresh, L"Actualizar");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x,
                                 cursor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command) HandleMenuCommand(command);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void App::HandleMenuCommand(int command) {
    std::vector<std::wstring> paths = SelectedPaths();
    if (paths.empty()) return;
    const ViewState& view = View();
    const MediaItem* clicked = nullptr;
    for (const auto& item : view.items) {
        if (SamePath(item.path, paths.front())) {
            clicked = &item;
            break;
        }
    }

    switch (command) {
        case kMenuItemOpen:
            if (clicked && clicked->isDir) {
                Navigate(View(), clicked->path, true);
            } else {
                int opened = 0;
                for (const auto& path : paths) {
                    OpenWithDefault(hwnd_, path);
                    if (++opened >= 8) break;
                }
            }
            break;
        case kMenuItemPlay:
            OpenWithDefault(hwnd_, paths.front());
            break;
        case kMenuItemEditAudio:
            if (paths.size() == 1) OpenAudioEditor(paths.front());
            break;
        case kMenuItemReveal:
            RevealInExplorer(hwnd_, paths.front());
            break;
        case kMenuItemCopyPath: {
            std::wstring text;
            for (size_t i = 0; i < paths.size(); ++i) {
                if (i) text += L"\r\n";
                text += paths[i];
            }
            CopyTextToClipboard(hwnd_, text);
            SetToast(L"Ruta copiada al portapapeles.");
            break;
        }
        case kMenuItemCopyDrive:
            if (driveRoot_.empty() || !DirExists(driveRoot_)) {
                SetToast(L"Google Drive no está conectado.", true);
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                bool ok = CopyFilesTo(hwnd_, paths, driveRoot_);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                if (ok) {
                    SetToast(L"Copiado a Google Drive. Se sincronizará automáticamente.");
                    if (tab_ == Tab::Drive) RefreshCurrent();
                } else {
                    SetToast(L"No se pudo copiar a Google Drive.", true);
                }
            }
            break;
        case kMenuItemDelete:
            if (DeleteToRecycleBin(hwnd_, paths)) {
                selection_.clear();
                SetToast(L"Enviado a la papelera.");
                RefreshCurrent();
            }
            break;
        case kMenuItemProperties:
            ShowFileProperties(hwnd_, paths.front());
            break;
        case kMenuItemRefresh:
            RefreshCurrent();
            break;
        default:
            break;
    }
}

void App::StartDragSelection() {
    std::vector<std::wstring> paths = SelectedPaths();
    if (paths.empty() && pressCard_ >= 0 &&
        pressCard_ < static_cast<int>(View().items.size())) {
        paths.push_back(View().items[pressCard_].path);
    }
    if (paths.empty()) return;
    if (GetCapture() == hwnd_) ReleaseCapture();
    dragArmed_ = false;
    pressCard_ = -1;

    // While dragging out, fade the window, make it click-through and move it
    // behind everything so files can be dropped on an editor that was covered
    // by this always-on-top window.
    draggingOut_ = true;
    const int savedOpacity = opacity_;
    const LONG_PTR savedStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    opacity_ = std::min(opacity_, 78);
    windowConstantAlpha_ = 72;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, savedStyle | WS_EX_TRANSPARENT);
    DragAcceptFiles(hwnd_, FALSE);
    SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Render();
    BeginFileDrag(hwnd_, paths);
    DragAcceptFiles(hwnd_, TRUE);
    windowConstantAlpha_ = 255;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, savedStyle);
    opacity_ = savedOpacity;
    draggingOut_ = false;
    if (pin_) {
        ApplyTopmost();
    } else {
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    Render();
}

void App::SearchChanged(bool immediateDeep) {
    ViewState& view = View();
    view.scroll = 0;
    selection_.clear();
    anchorPath_.clear();
    hoverCard_ = -1;
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);

    if (view.search.empty()) {
        if (view.deep) ScanFolder(view);
    } else if (view.recursive) {
        if (immediateDeep) {
            RunDeepSearch(view);
        } else {
            pendingDeep_ = true;
            SetTimer(hwnd_, 3, 420, nullptr);
        }
    }
    Render();
}

void App::ClearSearch(bool keepFocus) {
    ViewState& view = View();
    if (view.search.empty() && !view.deep) {
        if (!keepFocus) searchFocused_ = false;
        Render();
        return;
    }
    view.search.clear();
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);
    if (view.deep) {
        view.deep = false;
        ScanFolder(view);
    }
    view.scroll = 0;
    searchFocused_ = keepFocus;
    caretOn_ = true;
    Render();
}

void App::EnsureSearchFocus() {
    searchFocused_ = true;
    caretOn_ = true;
    Render();
}

void App::RunPendingDeepSearch() {
    KillTimer(hwnd_, 3);
    pendingDeep_ = false;
    ViewState& view = View();
    if (view.recursive && !view.search.empty()) {
        RunDeepSearch(view);
        Render();
    }
}

void App::OnChar(wchar_t ch) {
    if (editorOpen_ || settingsOpen_) return;
    if (GetKeyState(VK_CONTROL) & 0x8000) return;
    if (GetKeyState(VK_MENU) & 0x8000) return;
    if (ch < 32) return;
    if (tab_ == Tab::Drive && (driveRoot_.empty() || !DirExists(driveRoot_))) return;

    if (!searchFocused_) {
        searchFocused_ = true;
        caretOn_ = true;
    }
    ViewState& view = View();
    if (view.search.size() < 120) {
        view.search.push_back(ch);
        SearchChanged(false);
    }
}

void App::OnKeyDown(WPARAM key, bool ctrl, bool shift, bool alt) {
    (void)shift;
    if (alt) return;

    if (!editorOpen_ && ctrl && (key == 'F')) {
        searchFocused_ = true;
        caretOn_ = true;
        Render();
        return;
    }
    if (!editorOpen_ && ctrl) {
        if (key == VK_OEM_PLUS || key == VK_ADD) {
            SetZoom(zoom_ + 10);
            return;
        }
        if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
            SetZoom(zoom_ - 10);
            return;
        }
        if (key == '0' || key == VK_NUMPAD0) {
            SetZoom(100);
            return;
        }
    }

    if (editorOpen_) {
        if (key == VK_ESCAPE) {
            if (ConfirmCloseEditor()) CloseAudioEditor();
            return;
        }
        if (ctrl && key == 'S') {
            SaveAudioAs();
            return;
        }
        if (ctrl && key == 'Z') {
            UndoAudioEdit();
            return;
        }
        if (key == VK_SPACE) {
            if (player_ && player_->IsActive()) {
                if (player_->IsPaused()) {
                    ResumeAudioPlayback();
                } else {
                    PauseAudioPlayback();
                }
            } else {
                BeginAudioPlayback();
            }
            return;
        }
        if (key == VK_DELETE && HasSelection()) {
            DeleteSelection();
            return;
        }
        if (key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END) {
            int64_t frames = ClipFrames(audio_);
            int64_t target = playhead_;
            if (key == VK_LEFT) target -= audio_.rate;
            if (key == VK_RIGHT) target += audio_.rate;
            if (key == VK_HOME) target = 0;
            if (key == VK_END) target = frames;
            target = std::max<int64_t>(0, std::min<int64_t>(frames, target));
            StopAudioPlayback(false);
            playhead_ = target;
            Render();
            return;
        }
        return;
    }

    if (key == VK_ESCAPE) {
        if (settingsOpen_) {
            settingsOpen_ = false;
            Render();
        } else if (searchFocused_) {
            if (View().search.empty()) {
                searchFocused_ = false;
                Render();
            } else {
                ClearSearch(true);
            }
        }
        return;
    }

    if (key == VK_F5) {
        RefreshCurrent();
        return;
    }

    if (key == VK_RETURN) {
        if (searchFocused_) {
            if (pendingDeep_) RunPendingDeepSearch();
            searchFocused_ = false;
            Render();
        } else {
            OpenSelected();
        }
        return;
    }

    if (key == VK_BACK) {
        if (searchFocused_) {
            ViewState& view = View();
            if (!view.search.empty()) {
                view.search.pop_back();
                SearchChanged(false);
            }
        }
        return;
    }

    if (ctrl && key == 'V' && searchFocused_) {
        if (!OpenClipboard(hwnd_)) return;
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (data) {
            auto* text = static_cast<wchar_t*>(GlobalLock(data));
            if (text) {
                std::wstring paste(text);
                GlobalUnlock(data);
                std::wstring clean;
                for (wchar_t c : paste) {
                    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
                    if (c >= 32 && clean.size() < 120) clean.push_back(c);
                }
                ViewState& view = View();
                size_t room = 120 - view.search.size();
                if (clean.size() > room) clean.resize(room);
                view.search += clean;
                SearchChanged(false);
            }
        }
        CloseClipboard();
    }
}

void App::OnTimer(UINT_PTR id) {
    if (id == 1) {
        bool needsPaint = false;
        if (searchFocused_) {
            caretOn_ = !caretOn_;
            needsPaint = true;
        }
        if (!toast_.empty() && GetTickCount64() >= toastUntil_) {
            toast_.clear();
            needsPaint = true;
        }
        if (needsPaint) Render();
    } else if (id == 2) {
        UpdateAudioPlayback();
    } else if (id == 3) {
        RunPendingDeepSearch();
    } else if (id == 4) {
        KillTimer(hwnd_, 4);
        StartUpdateCheck();
    }
}

void App::OnMouseMove(POINT pt, bool leftDown) {
    if (movingWindow_) {
        POINT cursor{};
        GetCursorPos(&cursor);
        int dx = cursor.x - dragStartCursor_.x;
        int dy = cursor.y - dragStartCursor_.y;
        SetWindowPos(hwnd_, nullptr, dragStartRect_.left + dx, dragStartRect_.top + dy,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    if (resizingWindow_) {
        POINT cursor{};
        GetCursorPos(&cursor);
        int dx = cursor.x - dragStartCursor_.x;
        int dy = cursor.y - dragStartCursor_.y;
        const int minW = static_cast<int>(S(920)) + 2 * ShadowMargin();
        const int minH = static_cast<int>(S(610)) + 2 * ShadowMargin();
        RECT rect = dragStartRect_;
        if (resizeEdge_ & 1) rect.left = std::min(rect.right - minW, rect.left + dx);
        if (resizeEdge_ & 2) rect.right = std::max(rect.left + minW, rect.right + dx);
        if (resizeEdge_ & 4) rect.top = std::min(rect.bottom - minH, rect.top + dy);
        if (resizeEdge_ & 8) rect.bottom = std::max(rect.top + minH, rect.bottom + dy);
        SetWindowPos(hwnd_, nullptr, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    if (waveDragging_) {
        SeekToFrame(FrameAtPoint(static_cast<float>(pt.x)), true);
        return;
    }

    if (draggingOpacity_) {
        Layout layout = BuildLayout();
        float trackX = layout.opacityTrack.X;
        float trackW = std::max(1.0f, layout.opacityTrack.Width);
        float t = (static_cast<float>(pt.x) - trackX) / trackW;
        t = std::max(0.0f, std::min(1.0f, t));
        SetOpacity(static_cast<int>(100 + t * 155.0f));
        return;
    }

    if (dragArmed_ && pressCard_ >= 0 && leftDown) {
        int dx = pt.x - pressPoint_.x;
        int dy = pt.y - pressPoint_.y;
        if (dx * dx + dy * dy > static_cast<int>(S(7) * S(7))) {
            StartDragSelection();
            return;
        }
    }

    HitInfo hit = HitTest(pt);
    if (hit.id != hoverId_ || hit.card != hoverCard_) {
        hoverId_ = hit.id;
        hoverCard_ = hit.card;
        Render();
    }
}

void App::OnLButtonDown(POINT pt, bool ctrl, bool shift) {
    Layout layout = BuildLayout();
    HitInfo hit = HitTest(pt);

    if (settingsOpen_) {
        if (hit.id == zid::Settings || hit.id == zid::UpdateButton) {
            // Let the button handlers run.
        } else if (!InRect(layout.settingsPanel, static_cast<float>(pt.x),
                           static_cast<float>(pt.y))) {
            settingsOpen_ = false;
            Render();
            return;
        } else if (hit.id == zid::SettingsPanel) {
            return;
        }
    }

    if (hit.resizeEdge && !maximized_) {
        resizingWindow_ = true;
        resizeEdge_ = hit.resizeEdge;
        dragStartCursor_.x = pt.x;
        dragStartCursor_.y = pt.y;
        GetCursorPos(&dragStartCursor_);
        GetWindowRect(hwnd_, &dragStartRect_);
        SetCapture(hwnd_);
        return;
    }

    if (hit.id == zid::OpacityTrack) {
        draggingOpacity_ = true;
        SetCapture(hwnd_);
        float trackW = std::max(1.0f, layout.opacityTrack.Width);
        float t = (static_cast<float>(pt.x) - layout.opacityTrack.X) / trackW;
        t = std::max(0.0f, std::min(1.0f, t));
        SetOpacity(static_cast<int>(100 + t * 155.0f));
        return;
    }

    if (hit.id == zid::Waveform) {
        if (hit.card < 0) {
            waveDragging_ = true;
            waveMoved_ = false;
            waveAnchor_ = FrameAtPoint(static_cast<float>(pt.x));
            playhead_ = waveAnchor_;
            hasSelection_ = false;
            StopAudioPlayback(false);
            SetCapture(hwnd_);
            Render();
            return;
        }
    }

    if (hit.card >= 0) {
        const MediaItem& item = View().items[hit.card];
        const bool alreadySelected = IsSelected(item.path);
        if (!alreadySelected || ctrl || shift) {
            SelectCard(hit.card, ctrl, shift);
        }
        pressCard_ = hit.card;
        pressPoint_ = pt;
        dragArmed_ = true;
        searchFocused_ = false;
        SetCapture(hwnd_);
        return;
    }

    if (hit.id != zid::None) {
        if (layout.showConnect && (hit.id == zid::Search ||
                                   hit.id == zid::ClearSearch)) {
            return;
        }
        if (hit.id != zid::Search && hit.id != zid::ClearSearch) {
            searchFocused_ = false;
        }
        if (hit.id == zid::Search) {
            EnsureSearchFocus();
            return;
        }
        OnClickZone(hit.id);
        return;
    }

    if (!editorOpen_ && InRect(layout.header, static_cast<float>(pt.x),
                               static_cast<float>(pt.y))) {
        searchFocused_ = false;
        if (!maximized_) {
            movingWindow_ = true;
            GetCursorPos(&dragStartCursor_);
            GetWindowRect(hwnd_, &dragStartRect_);
            SetCapture(hwnd_);
        }
        return;
    }

    if (!editorOpen_ && InRect(layout.grid, static_cast<float>(pt.x),
                               static_cast<float>(pt.y))) {
        selection_.clear();
        anchorPath_.clear();
        hoverCard_ = -1;
        Render();
    }
}

void App::OnLButtonUp(POINT pt) {
    (void)pt;
    if (movingWindow_ || resizingWindow_) {
        movingWindow_ = false;
        resizingWindow_ = false;
        resizeEdge_ = 0;
        if (GetCapture() == hwnd_) ReleaseCapture();
        SaveWindowPlacement();
        Render();
        return;
    }
    if (draggingOpacity_) {
        draggingOpacity_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        RegWriteDword(L"Opacity", static_cast<DWORD>(opacity_));
        Render();
        return;
    }
    if (waveDragging_) {
        waveDragging_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (!waveMoved_) {
            hasSelection_ = false;
            playhead_ = waveAnchor_;
        } else if (selectionEnd_ <= selectionStart_) {
            hasSelection_ = false;
        }
        Render();
        return;
    }
    if (dragArmed_) {
        dragArmed_ = false;
        pressCard_ = -1;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return;
    }
    if (GetCapture() == hwnd_) ReleaseCapture();
}

void App::OnLButtonDblClk(POINT pt) {
    Layout layout = BuildLayout();
    HitInfo hit = HitTest(pt);
    if (settingsOpen_) return;
    if (hit.card >= 0) {
        OnCardDoubleClick(hit.card);
        return;
    }
    if (hit.id == zid::Waveform) {
        int64_t frames = ClipFrames(audio_);
        if (frames > 0) {
            hasSelection_ = true;
            selectionStart_ = 0;
            selectionEnd_ = frames;
            playhead_ = 0;
            StopAudioPlayback(false);
            Render();
        }
        return;
    }
    if (InRect(layout.header, static_cast<float>(pt.x), static_cast<float>(pt.y)) &&
        hit.id == zid::None) {
        ToggleMaximize();
    }
}

void App::OnMouseWheel(int delta) {
    if (editorOpen_) return;
    Layout layout = BuildLayout();
    float maxScroll = std::max(0.0f, layout.contentHeight - layout.grid.Height);
    if (maxScroll <= 0.0f) return;
    ViewState& view = View();
    int steps = delta / WHEEL_DELTA;
    float amount = static_cast<float>(view.scroll) - steps * S(88);
    amount = std::max(0.0f, std::min(maxScroll, amount));
    view.scroll = static_cast<int>(amount + 0.5f);
    Render();
}

void App::OnClickZone(int id) {
    if (id >= zid::SidebarBase && id < zid::CardBase) {
        Layout layout = BuildLayout();
        int index = id - zid::SidebarBase;
        if (index >= 0 && index < static_cast<int>(layout.side.size())) {
            const SideEntry& entry = layout.side[index];
            if (entry.kind == SideEntry::Kind::Item) {
                if (entry.action != 0) {
                    OnClickZone(entry.action);
                } else if (!entry.path.empty()) {
                    Navigate(View(), entry.path, true);
                }
            }
        }
        return;
    }

    switch (id) {
        case zid::Min:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case zid::Max:
            ToggleMaximize();
            break;
        case zid::Close:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case zid::Pin:
            SetPin(!pin_);
            break;
        case zid::Settings:
            settingsOpen_ = !settingsOpen_;
            Render();
            break;
        case zid::TabLibrary:
            SwitchTab(Tab::Library);
            break;
        case zid::TabDrive:
            SwitchTab(Tab::Drive);
            break;
        case zid::Back:
            GoBack();
            break;
        case zid::Up:
            GoUp();
            break;
        case zid::Refresh:
            RefreshCurrent();
            break;
        case zid::OpenFolder:
            if (tab_ == Tab::Drive) {
                ChooseDriveFolder();
            } else {
                std::wstring picked;
                if (PickFolder(hwnd_, L"Abrir carpeta en Nébula", View().path, picked)) {
                    Navigate(library_, picked, true);
                }
            }
            break;
        case zid::Search:
            EnsureSearchFocus();
            break;
        case zid::ClearSearch:
            ClearSearch(true);
            break;
        case zid::FilterAll:
        case zid::FilterImages:
        case zid::FilterVideos:
        case zid::FilterAudio: {
            ViewState& view = View();
            view.filter = static_cast<Filter>(id - zid::FilterAll);
            view.scroll = 0;
            selection_.clear();
            anchorPath_.clear();
            hoverCard_ = -1;
            Render();
            break;
        }
        case zid::Recursive: {
            ViewState& view = View();
            view.recursive = !view.recursive;
            pendingDeep_ = false;
            KillTimer(hwnd_, 3);
            if (view.recursive && !view.search.empty()) {
                RunDeepSearch(view);
            } else if (!view.recursive && view.deep) {
                view.deep = false;
                ScanFolder(view);
            }
            Render();
            break;
        }
        case zid::ZoomOut:
            SetZoom(zoom_ - 10);
            break;
        case zid::ZoomReset:
            SetZoom(100);
            break;
        case zid::ZoomIn:
            SetZoom(zoom_ + 10);
            break;
        case zid::UpdateButton:
            InstallUpdateNow();
            break;
        case zid::DriveDetect:
            DetectDrive(true);
            break;
        case zid::DrivePick:
        case zid::DriveChange:
            ChooseDriveFolder();
            break;
        case zid::DriveWeb:
            OpenDriveWeb();
            break;
        case zid::DriveRefresh:
            RefreshCurrent();
            break;
        case zid::PinToggle:
            SetPin(!pin_);
            break;
        case zid::OpacityTrack:
            break;
        case zid::SettingsPanel:
            break;
        case zid::EditorBack:
            if (ConfirmCloseEditor()) CloseAudioEditor();
            break;
        case zid::EditorSave:
            SaveAudioAs();
            break;
        case zid::EditorUndo:
            UndoAudioEdit();
            break;
        case zid::EditorPlay:
            if (player_ && player_->IsActive()) {
                if (player_->IsPaused()) {
                    ResumeAudioPlayback();
                } else {
                    PauseAudioPlayback();
                }
            } else {
                BeginAudioPlayback();
            }
            break;
        case zid::EditorStop:
            StopAudioPlayback(true);
            break;
        case zid::EditorTrim:
            TrimToSelection();
            break;
        case zid::EditorDelete:
            DeleteSelection();
            break;
        case zid::EditorFadeIn:
            FadeSelection(true);
            break;
        case zid::EditorFadeOut:
            FadeSelection(false);
            break;
        case zid::EditorVolDown:
            ChangeVolume(0.80);
            break;
        case zid::EditorVolUp:
            ChangeVolume(1.25);
            break;
        case zid::Waveform:
            break;
        default:
            break;
    }
}

void App::OnDroppedFiles(HDROP drop) {
    if (!drop) return;
    if (editorOpen_) {
        SetToast(L"Cierra el editor de audio antes de importar archivos.", true);
        DragFinish(drop);
        return;
    }

    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    for (UINT i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) continue;
        std::vector<wchar_t> buffer(length + 2, L'\0');
        if (DragQueryFileW(drop, i, buffer.data(), length + 1) > 0) {
            paths.emplace_back(buffer.data());
        }
    }
    DragFinish(drop);
    if (paths.empty()) return;

    const bool firstIsDir = DirExists(paths.front());
    if (tab_ == Tab::Drive) {
        if (firstIsDir) {
            SetDriveRoot(paths.front(), true);
            SetToast(L"Carpeta de Drive actualizada.", false);
            return;
        }
        if (driveRoot_.empty() || !DirExists(driveRoot_)) {
            SetToast(L"Arrastra una carpeta o conecta Google Drive para soltar archivos.",
                     true);
            return;
        }
        std::wstring destination =
            (!drive_.path.empty() && DirExists(drive_.path)) ? drive_.path : driveRoot_;
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        bool ok = CopyFilesTo(hwnd_, paths, destination);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (ok) {
            RefreshCurrent();
            SetToast(L"Archivos copiados a Google Drive.");
        } else {
            SetToast(L"No se pudieron copiar los archivos a Drive.", true);
        }
        return;
    }

    std::wstring target = firstIsDir ? paths.front() : ParentPath(paths.front());
    if (!target.empty()) {
        Navigate(library_, target, true);
        SetForegroundWindow(hwnd_);
    }
}

HitInfo App::HitTest(POINT pt) {
    HitInfo hit{};
    Layout layout = BuildLayout();
    const float x = static_cast<float>(pt.x);
    const float y = static_cast<float>(pt.y);

    if (!maximized_ && InRect(layout.panel, x, y)) {
        const float edge = S(7);
        bool left = x <= layout.panel.X + edge;
        bool right = x >= layout.panel.GetRight() - edge;
        bool top = y <= layout.panel.Y + edge;
        bool bottom = y >= layout.panel.GetBottom() - edge;
        int bits = 0;
        if (left) bits |= 1;
        if (right) bits |= 2;
        if (top) bits |= 4;
        if (bottom) bits |= 8;
        if (bits) {
            hit.resizeEdge = bits;
            return hit;
        }
    }

    for (auto it = layout.zones.rbegin(); it != layout.zones.rend(); ++it) {
        if (InRect(it->rect, x, y)) {
            hit.id = it->id;
            return hit;
        }
    }

    if (!editorOpen_ && InRect(layout.grid, x, y)) {
        for (auto it = layout.cards.rbegin(); it != layout.cards.rend(); ++it) {
            if (InRect(it->rect, x, y)) {
                hit.card = it->index;
                return hit;
            }
        }
    }
    return hit;
}

const Gdiplus::RectF* App::ZoneRect(const Layout& layout, int id) const {
    for (const auto& zone : layout.zones) {
        if (zone.id == id) return &zone.rect;
    }
    return nullptr;
}

bool App::IsClickable(int id) const {
    if (id == zid::None || id == zid::Search || id == zid::Waveform ||
        id == zid::SettingsPanel) {
        return false;
    }
    if (id >= zid::SidebarBase && id < zid::CardBase) return true;
    return true;
}

void App::BuildSidebar(Layout& layout) {
    layout.side.clear();
    layout.showSidebarTip = false;

    const float x = layout.sidebar.X + S(8);
    const float w = layout.sidebar.Width - S(16);
    float y = layout.sidebar.Y + S(12);

    auto section = [&](const wchar_t* text) {
        SideEntry entry{};
        entry.kind = SideEntry::Kind::Section;
        entry.label = text;
        entry.rect = Gdiplus::RectF(x + S(8), y, w - S(16), S(22));
        layout.side.push_back(entry);
        y += S(26);
    };

    auto item = [&](const std::wstring& glyph, const std::wstring& label,
                    const std::wstring& path, int action, bool active) {
        SideEntry entry{};
        entry.kind = SideEntry::Kind::Item;
        entry.glyph = glyph;
        entry.label = label;
        entry.path = path;
        entry.action = action;
        entry.active = active;
        entry.rect = Gdiplus::RectF(x, y, w, S(34));
        layout.side.push_back(entry);
        AddZone(layout, zid::SidebarBase + static_cast<int>(layout.side.size()) - 1,
                entry.rect);
        y += S(38);
    };

    ViewState& view = View();

    if (tab_ == Tab::Library) {
        section(L"ACCESO RÁPIDO");
        for (const auto& folder : quick_) {
            item(folder.glyph, folder.label, folder.path, 0,
                 SamePath(view.path, folder.path));
        }
        y += S(6);
        item(L"\uE710", L"Abrir carpeta…", L"", zid::OpenFolder, false);
    } else {
        section(L"GOOGLE DRIVE");
        if (!driveRoot_.empty() && DirExists(driveRoot_)) {
            item(L"\uE753", L"Mi unidad", driveRoot_, 0,
                 SamePath(view.path, driveRoot_));
            auto shared = DriveSharedCandidates();
            for (const auto& path : shared) {
                std::wstring leaf = ToLower(FileNameOf(path));
                std::wstring label =
                    (leaf.find(L"other") != std::wstring::npos ||
                     leaf.find(L"equipos") != std::wstring::npos)
                        ? L"Otros equipos"
                        : L"Unidades compartidas";
                std::wstring glyph =
                    (label == L"Otros equipos") ? L"\uE7F4" : L"\uE753";
                item(glyph, label, path, 0, SamePath(view.path, path));
            }
            y += S(6);
            section(L"ACCIONES");
            item(L"\uE774", L"Abrir en el navegador", L"", zid::DriveWeb, false);
            item(L"\uE70F", L"Cambiar carpeta…", L"", zid::DriveChange, false);
            item(L"\uE72C", L"Actualizar", L"", zid::DriveRefresh, false);
        } else {
            item(L"\uE710", L"Elegir carpeta…", L"", zid::DrivePick, false);
            item(L"\uE774", L"Abrir en el navegador", L"", zid::DriveWeb, false);
            item(L"\uE721", L"Buscar instalación", L"", zid::DriveDetect, false);
        }
    }

    const float bottom = layout.sidebar.GetBottom() - S(10);
    const float tipHeight = S(82);
    if (y + S(8) < bottom - tipHeight && layout.sidebar.Height > S(360)) {
        layout.showSidebarTip = true;
        layout.sidebarTip = Gdiplus::RectF(x, bottom - tipHeight, w, tipHeight);
    }
}

Layout App::BuildLayout() {
    Layout layout{};
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float margin = static_cast<float>(ShadowMargin());
    layout.panel = Gdiplus::RectF(margin, margin, client.right - margin * 2,
                                  client.bottom - margin * 2);
    const float pad = S(22);
    const float panelRight = layout.panel.GetRight();
    const float panelBottom = layout.panel.GetBottom();

    layout.header = Gdiplus::RectF(layout.panel.X, layout.panel.Y, layout.panel.Width,
                                   S(62));
    layout.toolbar = Gdiplus::RectF(layout.panel.X + pad, layout.header.GetBottom() + S(2),
                                    layout.panel.Width - pad * 2, S(40));
    layout.filters = Gdiplus::RectF(layout.toolbar.X, layout.toolbar.GetBottom() + S(6),
                                    layout.toolbar.Width, S(38));
    layout.status = Gdiplus::RectF(layout.panel.X + pad, panelBottom - S(40),
                                   layout.panel.Width - pad * 2, S(30));

    // Header: tabs and window controls.
    const float tabW = S(132);
    const float tabGap = S(4);
    const float tabGroupW = tabW * 2 + tabGap;
    const float tabX = layout.panel.X + (layout.panel.Width - tabGroupW) * 0.5f;
    const float tabY = layout.panel.Y + S(13);
    AddZone(layout, zid::TabLibrary, Gdiplus::RectF(tabX, tabY, tabW, S(36)));
    AddZone(layout, zid::TabDrive,
            Gdiplus::RectF(tabX + tabW + tabGap, tabY, tabW, S(36)));

    const float buttonSize = S(34);
    const float buttonGap = S(3);
    const float controlsW = buttonSize * 5 + buttonGap * 4;
    const float controlsStart = panelRight - S(20) - controlsW;
    float controlX = controlsStart;
    const float controlY = layout.panel.Y + S(13);
    const int controlIds[5] = {zid::Settings, zid::Pin, zid::Min, zid::Max, zid::Close};
    for (int controlId : controlIds) {
        AddZone(layout, controlId,
                Gdiplus::RectF(controlX, controlY, buttonSize, buttonSize));
        controlX += buttonSize + buttonGap;
    }
    if (updateReady_) {
        const float updateW = S(104);
        layout.updateButton = Gdiplus::RectF(
            controlsStart - S(10) - updateW, layout.panel.Y + S(16), updateW, S(30));
        AddZone(layout, zid::UpdateButton, layout.updateButton);
    }

    if (!editorOpen_) {
        // Toolbar.
        const float navSize = S(34);
        const float navGap = S(6);
        const float navY = layout.toolbar.Y + S(3);
        AddZone(layout, zid::Back,
                Gdiplus::RectF(layout.toolbar.X, navY, navSize, navSize));
        AddZone(layout, zid::Up,
                Gdiplus::RectF(layout.toolbar.X + navSize + navGap, navY, navSize,
                               navSize));
        AddZone(layout, zid::Refresh,
                Gdiplus::RectF(layout.toolbar.X + (navSize + navGap) * 2, navY, navSize,
                               navSize));

        const float openW = S(112);
        const float searchW = S(228);
        const float controlH = S(34);
        const float controlY2 = layout.toolbar.Y + S(3);
        layout.search = Gdiplus::RectF(layout.toolbar.GetRight() - searchW, controlY2,
                                       searchW, controlH);
        const float openX = layout.search.X - S(10) - openW;
        const Gdiplus::RectF openRect(openX, controlY2, openW, controlH);
        const float pathX = layout.toolbar.X + (navSize + navGap) * 3 + S(6);
        layout.pathPill = Gdiplus::RectF(pathX, controlY2,
                                         openX - S(10) - pathX, controlH);
        AddZone(layout, zid::OpenFolder, openRect);
        AddZone(layout, zid::Search, layout.search);
        if (!View().search.empty()) {
            layout.searchClear = Gdiplus::RectF(layout.search.GetRight() - S(30),
                                                layout.search.Y + S(7), S(20), S(20));
            AddZone(layout, zid::ClearSearch, layout.searchClear);
        }

        // Filter chips.
        float chipX = layout.filters.X;
        const float chipY = layout.filters.Y + S(4);
        const float chipH = S(28);
        for (int i = 0; i < kFilterCount; ++i) {
            float chipW = S(static_cast<float>(kFilterWidths[i]));
            AddZone(layout, zid::FilterAll + i,
                    Gdiplus::RectF(chipX, chipY, chipW, chipH));
            chipX += chipW + S(6);
        }
        chipX += S(6);
        AddZone(layout, zid::Recursive,
                Gdiplus::RectF(chipX, chipY, S(116), chipH));

        // Zoom controls at the right edge of the filter bar.
        const float zoomW = S(110);
        const float zoomH = S(28);
        layout.zoomGroup = Gdiplus::RectF(
            layout.filters.GetRight() - zoomW, layout.filters.Y + S(5), zoomW,
            zoomH);
        const float zoomButton = S(26);
        const float zoomResetW = S(46);
        const float zoomGap = S(4);
        AddZone(layout, zid::ZoomOut,
                Gdiplus::RectF(layout.zoomGroup.X + S(2), layout.zoomGroup.Y,
                               zoomButton, zoomH));
        AddZone(layout, zid::ZoomReset,
                Gdiplus::RectF(layout.zoomGroup.X + S(2) + zoomButton + zoomGap,
                               layout.zoomGroup.Y, zoomResetW, zoomH));
        AddZone(layout, zid::ZoomIn,
                Gdiplus::RectF(layout.zoomGroup.X + S(2) + zoomButton + zoomGap +
                                   zoomResetW + zoomGap,
                               layout.zoomGroup.Y, zoomButton, zoomH));

        // Main content.
        layout.content = Gdiplus::RectF(
            layout.toolbar.X, layout.filters.GetBottom() + S(6), layout.toolbar.Width,
            layout.status.Y - (layout.filters.GetBottom() + S(6)) - S(2));
        layout.sidebar = Gdiplus::RectF(layout.content.X, layout.content.Y, S(196),
                                        layout.content.Height);
        const float gridX = layout.sidebar.GetRight() + S(16);
        layout.grid = Gdiplus::RectF(gridX, layout.content.Y,
                                     layout.content.GetRight() - gridX,
                                     layout.content.Height);

        BuildSidebar(layout);

        const bool driveUnavailable =
            tab_ == Tab::Drive &&
            (driveRoot_.empty() || !DirExists(driveRoot_) || drive_.path.empty());
        if (driveUnavailable) {
            layout.showConnect = true;
            const float panelW = std::min(layout.grid.Width, S(570));
            const float panelH = std::min(layout.grid.Height - S(16), S(332));
            const float px = layout.grid.X + (layout.grid.Width - panelW) * 0.5f;
            const float py = layout.grid.Y + (layout.grid.Height - panelH) * 0.5f;
            layout.connectPanel = Gdiplus::RectF(px, py, panelW, panelH);
            const float buttonH = S(38);
            const float buttonY = layout.connectPanel.GetBottom() - S(58);
            const float gap = S(8);
            const float w1 = S(164), w2 = S(146), w3 = S(156);
            float bx = layout.connectPanel.X +
                       (layout.connectPanel.Width - (w1 + w2 + w3 + gap * 2)) * 0.5f;
            layout.connectDetect = Gdiplus::RectF(bx, buttonY, w1, buttonH);
            layout.connectPick = Gdiplus::RectF(bx + w1 + gap, buttonY, w2, buttonH);
            layout.connectWeb =
                Gdiplus::RectF(bx + w1 + w2 + gap * 2, buttonY, w3, buttonH);
            AddZone(layout, zid::DriveDetect, layout.connectDetect);
            AddZone(layout, zid::DrivePick, layout.connectPick);
            AddZone(layout, zid::DriveWeb, layout.connectWeb);
        } else {
            ViewState& view = View();
            layout.visible = VisibleIndices(view);
            const float zoomScale = std::clamp(zoom_ / 100.0f, 0.8f, 1.6f);
            const float cardW = S(176) * zoomScale;
            const float cardH = S(172) * zoomScale;
            const float gap = S(14) * zoomScale;
            const int columns = std::max(
                1, static_cast<int>((layout.grid.Width + gap) / (cardW + gap)));
            layout.columns = columns;
            const int rows =
                layout.visible.empty()
                    ? 0
                    : (static_cast<int>(layout.visible.size()) + columns - 1) / columns;
            layout.contentHeight =
                rows > 0 ? rows * cardH + (rows - 1) * gap + S(2) : 0.0f;
            const float maxScroll =
                std::max(0.0f, layout.contentHeight - layout.grid.Height);
            view.scroll = static_cast<int>(std::max(
                0.0f, std::min(maxScroll, static_cast<float>(view.scroll))));
            const float originY = layout.grid.Y + S(2) - static_cast<float>(view.scroll);
            for (int i = 0; i < static_cast<int>(layout.visible.size()); ++i) {
                const int col = i % columns;
                const int row = i / columns;
                Gdiplus::RectF rect(layout.grid.X + col * (cardW + gap),
                                    originY + row * (cardH + gap), cardW, cardH);
                layout.cards.push_back(CardHit{layout.visible[i], rect});
            }
            if (maxScroll > 0.0f) {
                layout.showScrollThumb = true;
                const float trackX = layout.grid.GetRight() - S(6);
                const float thumbH =
                    std::max(S(30), layout.grid.Height * layout.grid.Height /
                                        std::max(layout.grid.Height, layout.contentHeight));
                const float t = static_cast<float>(view.scroll) / maxScroll;
                const float thumbY = layout.grid.Y + t * (layout.grid.Height - thumbH);
                layout.scrollThumb = Gdiplus::RectF(trackX, thumbY, S(4), thumbH);
            }
        }
    } else {
        layout.editor = Gdiplus::RectF(layout.panel.X + pad,
                                       layout.header.GetBottom() + S(8),
                                       layout.panel.Width - pad * 2,
                                       panelBottom - (layout.header.GetBottom() + S(8)) -
                                           S(14));
        layout.editorTop = Gdiplus::RectF(layout.editor.X, layout.editor.Y,
                                         layout.editor.Width, S(52));
        AddZone(layout, zid::EditorBack,
                Gdiplus::RectF(layout.editor.X, layout.editor.Y + S(8), S(34), S(34)));
        const float saveW = S(118);
        const float undoW = S(88);
        AddZone(layout, zid::EditorSave,
                Gdiplus::RectF(layout.editor.GetRight() - saveW,
                               layout.editor.Y + S(8), saveW, S(34)));
        AddZone(layout, zid::EditorUndo,
                Gdiplus::RectF(layout.editor.GetRight() - saveW - S(8) - undoW,
                               layout.editor.Y + S(8), undoW, S(34)));

        const float waveY = layout.editor.Y + S(54);
        const float bottomH = S(94);
        layout.editorWave = Gdiplus::RectF(
            layout.editor.X, waveY, layout.editor.Width,
            layout.editor.GetBottom() - bottomH - S(10) - waveY);
        layout.editorPlot = Gdiplus::RectF(
            layout.editorWave.X + S(14), layout.editorWave.Y + S(32),
            layout.editorWave.Width - S(28), layout.editorWave.Height - S(44));
        AddZone(layout, zid::Waveform, layout.editorPlot);

        layout.editorBottom = Gdiplus::RectF(
            layout.editor.X, layout.editorWave.GetBottom() + S(10),
            layout.editor.Width, layout.editor.GetBottom() - layout.editorWave.GetBottom() - S(10));
        layout.editorPlay = Gdiplus::RectF(layout.editorBottom.X + S(14),
                                           layout.editorBottom.Y + S(12), S(42), S(42));
        layout.editorStop = Gdiplus::RectF(layout.editorPlay.GetRight() + S(10),
                                           layout.editorBottom.Y + S(16), S(34), S(34));
        AddZone(layout, zid::EditorPlay, layout.editorPlay);
        AddZone(layout, zid::EditorStop, layout.editorStop);

        const float actionW = S(82);
        const float actionGap = S(6);
        const float actionsTotal = actionW * 6 + actionGap * 5;
        float actionX = layout.editorBottom.GetRight() - S(14) - actionsTotal;
        const float actionY = layout.editorBottom.Y + S(16);
        const int actionIds[6] = {zid::EditorTrim,   zid::EditorDelete,
                                  zid::EditorFadeIn, zid::EditorFadeOut,
                                  zid::EditorVolDown, zid::EditorVolUp};
        for (int actionId : actionIds) {
            AddZone(layout, actionId,
                    Gdiplus::RectF(actionX, actionY, actionW, S(30)));
            actionX += actionW + actionGap;
        }
    }

    if (settingsOpen_) {
        layout.settingsPanel = Gdiplus::RectF(
            layout.panel.GetRight() - pad - S(288), layout.header.GetBottom() + S(6),
            S(288), S(176));
        layout.opacityTrack = Gdiplus::RectF(
            layout.settingsPanel.X + S(18), layout.settingsPanel.Y + S(80),
            layout.settingsPanel.Width - S(36), S(8));
        layout.toggleSwitch = Gdiplus::RectF(
            layout.settingsPanel.GetRight() - S(54),
            layout.settingsPanel.Y + S(122), S(36), S(20));
        AddZone(layout, zid::SettingsPanel, layout.settingsPanel);
        AddZone(layout, zid::PinToggle,
                Gdiplus::RectF(layout.settingsPanel.X + S(12),
                               layout.settingsPanel.Y + S(112),
                               layout.settingsPanel.Width - S(24), S(40)));
        AddZone(layout, zid::OpacityTrack, InflateRect(layout.opacityTrack, S(10)));
    }

    return layout;
}

void App::EnsureBuffer(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == bufferW_ && height == bufferH_ && memDC_ && dib_) return;

    if (memDC_) {
        SelectObject(memDC_, GetStockObject(NULL_BRUSH));
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    if (dib_) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    bits_ = nullptr;
    bufferW_ = 0;
    bufferH_ = 0;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    dib_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    memDC_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    if (memDC_ && dib_) {
        SelectObject(memDC_, dib_);
        bufferW_ = width;
        bufferH_ = height;
    }
}

void App::Render() {
    if (!ready_ || !hwnd_ || IsIconic(hwnd_)) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    EnsureBuffer(width, height);
    if (!memDC_ || !bits_ || !dib_) return;

    memset(bits_, 0, static_cast<size_t>(width) * height * 4);
    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits_));
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    Layout layout = BuildLayout();
    DrawWindow(graphics, layout);

    HDC screen = GetDC(nullptr);
    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    POINT destination{windowRect.left, windowRect.top};
    SIZE size{width, height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0,
                        static_cast<BYTE>(std::clamp(windowConstantAlpha_, 0, 255)),
                        AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, &destination, &size, memDC_, &source, 0, &blend,
                        ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

// ------------------------------------------------------------------ audio editor

void App::OpenAudioEditor(const std::wstring& path) {
    if (editorOpen_) return;
    if (!FileExists(path)) {
        SetToast(L"El archivo de audio ya no existe.", true);
        return;
    }
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    AudioClip clip;
    std::wstring error;
    bool ok = LoadAudioFile(path, clip, error);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (!ok) {
        MessageBoxW(hwnd_,
                    (L"No se pudo abrir el audio.\n\n" +
                     (error.empty() ? std::wstring(L"Formato no compatible.") : error))
                        .c_str(),
                    L"Nébula Media", MB_OK | MB_ICONWARNING);
        return;
    }

    audio_ = std::move(clip);
    editorOpen_ = true;
    settingsOpen_ = false;
    searchFocused_ = false;
    pendingDeep_ = false;
    KillTimer(hwnd_, 3);
    playhead_ = 0;
    selectionStart_ = selectionEnd_ = 0;
    hasSelection_ = false;
    canUndo_ = false;
    undoData_.clear();
    undoLabel_.clear();
    player_ = std::make_unique<AudioPlayer>();
    if (!player_->Open(audio_.rate, audio_.channels)) {
        SetToast(L"No se pudo abrir el dispositivo de audio. Puedes editar y guardar igualmente.",
                 true);
    }
    if (audio_.truncated) {
        SetToast(L"Archivo largo: se cargaron los primeros 10 minutos en el editor.");
    }
    Render();
}

bool App::ConfirmCloseEditor() {
    if (!editorOpen_) return true;
    if (!audio_.dirty) return true;
    int answer = MessageBoxW(
        hwnd_,
        L"Hay cambios sin guardar en el editor de audio.\n\n¿Quieres descartarlos?",
        L"Nébula Media", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    return answer == IDYES;
}

void App::CloseAudioEditor() {
    StopAudioPlayback(false);
    if (player_) {
        player_->Close();
        player_.reset();
    }
    audio_ = AudioClip{};
    undoData_.clear();
    canUndo_ = false;
    hasSelection_ = false;
    editorOpen_ = false;
    playhead_ = 0;
    selectionStart_ = selectionEnd_ = 0;
    Render();
}

void App::RebuildAudioPeaks() {
    RebuildPeaks(audio_);
}

void App::PushUndo(const std::wstring& label) {
    if (audio_.samples.empty()) return;
    try {
        undoData_ = audio_.samples;
        undoChannels_ = audio_.channels;
        undoRate_ = audio_.rate;
        canUndo_ = true;
        undoLabel_ = label;
    } catch (const std::bad_alloc&) {
        canUndo_ = false;
        undoData_.clear();
        SetToast(L"No hay memoria suficiente para deshacer este cambio.", true);
    }
}

void App::UndoAudioEdit() {
    if (!canUndo_ || undoData_.empty()) {
        SetToast(L"No hay cambios que deshacer.");
        return;
    }
    StopAudioPlayback(false);
    audio_.samples = std::move(undoData_);
    undoData_.clear();
    audio_.channels = undoChannels_;
    audio_.rate = undoRate_;
    canUndo_ = false;
    audio_.dirty = true;
    hasSelection_ = false;
    selectionStart_ = selectionEnd_ = 0;
    playhead_ = std::min(playhead_, ClipFrames(audio_));
    RebuildAudioPeaks();
    Render();
    SetToast(L"Cambio deshecho.");
}

void App::TrimToSelection() {
    if (!HasSelection()) {
        SetToast(L"Selecciona un fragmento de la onda para recortar.", true);
        return;
    }
    StopAudioPlayback(false);
    PushUndo(L"Recortar");
    const int ch = audio_.channels;
    const int64_t start = std::max<int64_t>(0, selectionStart_);
    const int64_t end = std::min<int64_t>(ClipFrames(audio_), selectionEnd_);
    if (end <= start || ch <= 0) return;
    audio_.samples.erase(audio_.samples.begin() + end * ch, audio_.samples.end());
    audio_.samples.erase(audio_.samples.begin(), audio_.samples.begin() + start * ch);
    audio_.dirty = true;
    hasSelection_ = false;
    selectionStart_ = selectionEnd_ = 0;
    playhead_ = 0;
    RebuildAudioPeaks();
    Render();
    SetToast(L"Recortado al fragmento seleccionado.");
}

void App::DeleteSelection() {
    if (!HasSelection()) {
        SetToast(L"Selecciona un fragmento de la onda para eliminarlo.", true);
        return;
    }
    StopAudioPlayback(false);
    PushUndo(L"Eliminar");
    const int ch = audio_.channels;
    const int64_t start = std::max<int64_t>(0, selectionStart_);
    const int64_t end = std::min<int64_t>(ClipFrames(audio_), selectionEnd_);
    if (end <= start || ch <= 0) return;
    audio_.samples.erase(audio_.samples.begin() + start * ch,
                         audio_.samples.begin() + end * ch);
    audio_.dirty = true;
    hasSelection_ = false;
    selectionStart_ = selectionEnd_ = 0;
    playhead_ = start;
    RebuildAudioPeaks();
    Render();
    SetToast(L"Fragmento eliminado.");
}

void App::FadeSelection(bool fadeIn) {
    const int64_t total = ClipFrames(audio_);
    if (total <= 1) return;
    int64_t start = HasSelection() ? selectionStart_ : 0;
    int64_t end = HasSelection() ? selectionEnd_ : total;
    start = std::max<int64_t>(0, std::min(total, start));
    end = std::max<int64_t>(0, std::min(total, end));
    if (end - start < 2) {
        SetToast(L"El fragmento es demasiado corto para aplicar un fundido.", true);
        return;
    }
    StopAudioPlayback(false);
    PushUndo(fadeIn ? L"Fundido de entrada" : L"Fundido de salida");
    const int ch = audio_.channels;
    const int64_t length = end - start;
    for (int64_t i = 0; i < length; ++i) {
        float gain = static_cast<float>(i) / static_cast<float>(length - 1);
        if (!fadeIn) gain = 1.0f - gain;
        for (int c = 0; c < ch; ++c) {
            int32_t value = static_cast<int32_t>(
                audio_.samples[(start + i) * ch + c] * gain);
            value = std::max(-32768, std::min(32767, value));
            audio_.samples[(start + i) * ch + c] = static_cast<int16_t>(value);
        }
    }
    audio_.dirty = true;
    RebuildAudioPeaks();
    Render();
    SetToast(fadeIn ? L"Fundido de entrada aplicado." : L"Fundido de salida aplicado.");
}

void App::ChangeVolume(double factor) {
    const int64_t total = ClipFrames(audio_);
    if (total <= 0) return;
    int64_t start = HasSelection() ? selectionStart_ : 0;
    int64_t end = HasSelection() ? selectionEnd_ : total;
    start = std::max<int64_t>(0, std::min(total, start));
    end = std::max<int64_t>(0, std::min(total, end));
    if (end <= start) return;
    StopAudioPlayback(false);
    PushUndo(L"Volumen");
    const int ch = audio_.channels;
    for (int64_t f = start; f < end; ++f) {
        for (int c = 0; c < ch; ++c) {
            int32_t value = static_cast<int32_t>(
                audio_.samples[f * ch + c] * factor);
            value = std::max(-32768, std::min(32767, value));
            audio_.samples[f * ch + c] = static_cast<int16_t>(value);
        }
    }
    audio_.dirty = true;
    RebuildAudioPeaks();
    Render();
    SetToast(HasSelection() ? L"Volumen aplicado a la selección."
                            : L"Volumen aplicado a todo el audio.");
}

void App::SaveAudioAs() {
    if (ClipFrames(audio_) <= 0) {
        SetToast(L"No hay audio para guardar.", true);
        return;
    }
    std::wstring suggested =
        JoinPath(ParentPath(audio_.sourcePath), StemOf(audio_.sourcePath) + L" - editado.wav");
    std::wstring path;
    if (!PickSaveWav(hwnd_, suggested, path)) return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::wstring error;
    bool ok = SaveWavFile(path, audio_, error);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (!ok) {
        MessageBoxW(hwnd_, (L"No se pudo guardar el WAV.\n\n" + error).c_str(),
                    L"Nébula Media", MB_OK | MB_ICONWARNING);
        return;
    }
    audio_.dirty = false;
    audio_.sourcePath = path;
    audio_.displayName = FileNameOf(path);
    RegWriteStr(L"LastAudioExport", path);
    SetToast(L"Audio guardado: " + FileNameOf(path));
    Render();
}

void App::BeginAudioPlayback() {
    if (!editorOpen_) return;
    if (!player_ || !player_->IsOpen()) {
        if (!player_) player_ = std::make_unique<AudioPlayer>();
        if (!player_->Open(audio_.rate, audio_.channels)) {
            SetToast(L"No se pudo iniciar el dispositivo de audio.", true);
            return;
        }
    }
    const int64_t frames = ClipFrames(audio_);
    if (frames <= 0) return;
    int64_t start = playhead_;
    int64_t end = frames;
    if (HasSelection()) {
        start = selectionStart_;
        end = selectionEnd_;
    }
    if (start >= end) {
        start = 0;
        end = frames;
    }
    if (end <= start) return;
    if (!player_->Play(audio_.samples.data(), frames, audio_.channels, audio_.rate, start,
                       end)) {
        SetToast(L"No se pudo reproducir el audio.", true);
        return;
    }
    playBegin_ = start;
    playEnd_ = end;
    playhead_ = start;
    SetTimer(hwnd_, 2, 50, nullptr);
    Render();
}

void App::PauseAudioPlayback() {
    if (!player_ || !player_->IsActive()) return;
    playhead_ = std::min(player_->PositionFrames(), ClipFrames(audio_));
    player_->Pause();
    KillTimer(hwnd_, 2);
    Render();
}

void App::ResumeAudioPlayback() {
    if (!player_ || !player_->IsActive()) {
        BeginAudioPlayback();
        return;
    }
    player_->Resume();
    SetTimer(hwnd_, 2, 50, nullptr);
    Render();
}

void App::StopAudioPlayback(bool resetPlayhead) {
    KillTimer(hwnd_, 2);
    if (player_) player_->Stop();
    if (resetPlayhead) {
        playhead_ = HasSelection() ? selectionStart_ : 0;
        Render();
    }
}

void App::UpdateAudioPlayback() {
    if (!editorOpen_ || !player_ || !player_->IsActive() || player_->IsPaused()) return;
    int64_t position = player_->PositionFrames();
    if (player_->Finished() || position >= playEnd_) {
        player_->Stop();
        KillTimer(hwnd_, 2);
        playhead_ = playEnd_;
        Render();
        return;
    }
    playhead_ = std::max<int64_t>(0, std::min(position, ClipFrames(audio_)));
    Render();
}

void App::SeekToFrame(int64_t frame, bool extendSelection) {
    const int64_t frames = ClipFrames(audio_);
    frame = std::max<int64_t>(0, std::min(frames, frame));
    if (extendSelection) {
        if (frame != waveAnchor_) waveMoved_ = true;
        playhead_ = waveAnchor_;
        selectionStart_ = std::min(waveAnchor_, frame);
        selectionEnd_ = std::max(waveAnchor_, frame);
        hasSelection_ = selectionEnd_ > selectionStart_;
    } else {
        playhead_ = frame;
        hasSelection_ = false;
        selectionStart_ = selectionEnd_ = 0;
    }
    Render();
}

void App::FinishWaveDrag(POINT pt) {
    SeekToFrame(FrameAtPoint(static_cast<float>(pt.x)), true);
    if (!waveMoved_) {
        hasSelection_ = false;
        selectionStart_ = selectionEnd_ = 0;
        playhead_ = waveAnchor_;
    }
}

int64_t App::FrameAtPoint(float x) {
    Layout layout = BuildLayout();
    const Gdiplus::RectF& plot = layout.editorPlot;
    const int64_t frames = ClipFrames(audio_);
    if (frames <= 0 || plot.Width <= 1.0f) return 0;
    float t = (x - plot.X) / plot.Width;
    t = std::max(0.0f, std::min(1.0f, t));
    return static_cast<int64_t>(t * static_cast<float>(frames));
}

void App::RebuildPeaksFromSamples() {
    RebuildPeaks(audio_);
}

void App::ClearAudioSelection() {
    hasSelection_ = false;
    selectionStart_ = selectionEnd_ = 0;
}

std::wstring App::HoverItemName(const Layout& layout) const {
    if (hoverCard_ < 0 || editorOpen_) return L"";
    const ViewState& view = View();
    if (hoverCard_ >= static_cast<int>(view.items.size())) return L"";
    bool visible = false;
    for (const auto& card : layout.cards) {
        if (card.index == hoverCard_) {
            visible = true;
            break;
        }
    }
    if (!visible) return L"";
    const MediaItem& item = view.items[hoverCard_];
    std::wstring text = item.name;
    if (!item.isDir && item.kind == ItemKind::Audio) {
        text += L"   ·   doble clic para editar el audio";
    } else if (item.cloudOnly) {
        text += L"   ·   solo en la nube";
    }
    return text;
}

// ------------------------------------------------------------------ helpers

void App::CreateRuntimeIcon() {}

}  // namespace nebula
