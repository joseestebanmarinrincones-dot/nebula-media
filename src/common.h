#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "version.h"

namespace nebula {

constexpr int WM_APP_THUMB = WM_APP + 1;
constexpr int WM_APP_UPDATE = WM_APP + 2;

struct UpdateResult {
    bool available = false;
    bool downloaded = false;
    std::wstring version;
    std::wstring path;
    std::wstring error;
};

UpdateResult CheckForUpdatesBlocking(const std::wstring& currentVersion);
bool LaunchSelfUpdate(const std::wstring& updatePath, std::wstring& error,
                      bool restart = false);

// ---------------------------------------------------------------- data model

enum class Filter { All = 0, Images, Videos, Audio, Count };
enum class Tab { Library = 0, Drive = 1 };
enum class ItemKind { Folder, Image, Video, Audio, Program, Other };

struct MediaItem {
    std::wstring path;
    std::wstring name;
    std::wstring ext;        // ".mp4"
    std::wstring context;    // relative parent, used by deep search
    ItemKind kind = ItemKind::Other;
    uint64_t size = 0;
    FILETIME modified{};
    bool isDir = false;
    bool cloudOnly = false;
};

struct ViewState {
    std::wstring path;
    std::vector<MediaItem> items;
    Filter filter = Filter::All;
    std::wstring search;
    int scroll = 0;
    bool recursive = false;  // search inside subfolders
    bool deep = false;       // current items are deep-search results
    std::vector<std::wstring> history;
    int hist = -1;
};

struct QuickFolder {
    std::wstring label;
    std::wstring glyph;
    std::wstring path;
};

// ---------------------------------------------------------------- audio

struct AudioClip {
    std::vector<int16_t> samples;  // interleaved
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    int channels = 2;
    int rate = 48000;
    std::wstring sourcePath;
    std::wstring displayName;
    bool dirty = false;
    bool truncated = false;
};

inline int64_t ClipFrames(const AudioClip& c) {
    return c.channels > 0 ? static_cast<int64_t>(c.samples.size()) / c.channels : 0;
}

std::wstring FormatTime(double seconds, bool hundredths = true);
void RebuildPeaks(AudioClip& clip);
bool LoadAudioFile(const std::wstring& path, AudioClip& out, std::wstring& error);
bool SaveWavFile(const std::wstring& path, const AudioClip& clip, std::wstring& error);
bool PickSaveWav(HWND owner, const std::wstring& suggestedFile, std::wstring& outPath);

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool Open(int rate, int channels);
    void Close();
    bool Play(const int16_t* data, int64_t totalFrames, int channels, int rate,
              int64_t startFrame, int64_t endFrame);
    void Pause();
    void Resume();
    void Stop();
    bool IsOpen() const { return out_ != nullptr; }
    bool IsPlaying() const { return playing_ && !paused_; }
    bool IsPaused() const { return playing_ && paused_; }
    bool IsActive() const { return playing_; }
    bool Finished() const;
    int64_t PositionFrames() const;

private:
    HWAVEOUT out_ = nullptr;
    WAVEFORMATEX fmt_{};
    WAVEHDR hdr_{};
    bool prepared_ = false;
    bool playing_ = false;
    bool paused_ = false;
    int64_t startFrame_ = 0;
    int64_t endFrame_ = 0;
};

// ---------------------------------------------------------------- thumbnails

class ThumbnailLoader {
public:
    explicit ThumbnailLoader(HWND notify);
    ~ThumbnailLoader();
    ThumbnailLoader(const ThumbnailLoader&) = delete;
    ThumbnailLoader& operator=(const ThumbnailLoader&) = delete;

    void Start();
    void Stop();
    void Request(const std::wstring& path);
    std::shared_ptr<Gdiplus::Bitmap> Get(const std::wstring& path);
    bool Failed(const std::wstring& path);
    void ClearQueue();

private:
    void Worker();
    std::shared_ptr<Gdiplus::Bitmap> RenderThumb(const std::wstring& path);

    HWND hwnd_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::wstring> queue_;
    std::unordered_set<std::wstring> queued_;
    std::unordered_set<std::wstring> failed_;
    std::unordered_map<std::wstring, std::shared_ptr<Gdiplus::Bitmap>> cache_;
    std::deque<std::wstring> order_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
    bool started_ = false;
};

// ---------------------------------------------------------------- drag & drop

HRESULT BeginFileDrag(HWND owner, const std::vector<std::wstring>& paths);
IDataObject* CreateFileDataObject(const std::vector<std::wstring>& paths);

// ---------------------------------------------------------------- helpers

std::wstring ToLower(std::wstring s);
bool EndsWithNoCase(const std::wstring& s, const std::wstring& suffix);
bool SamePath(const std::wstring& a, const std::wstring& b);
bool DirExists(const std::wstring& path);
bool FileExists(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring ParentPath(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring StemOf(const std::wstring& path);
std::wstring ExtensionOf(const std::wstring& path);
std::wstring RelativeTo(const std::wstring& path, const std::wstring& base);
std::wstring FormatSize(uint64_t bytes);
ItemKind KindForFile(const std::wstring& path, bool isDir);
std::wstring KindMeta(const MediaItem& item);
std::wstring KnownFolderPath(REFKNOWNFOLDERID id);
std::wstring DetectGoogleDrive(std::wstring& mountRoot);
bool IsCloudPlaceholder(DWORD attributes);
void OpenWithDefault(HWND owner, const std::wstring& path);
void RevealInExplorer(HWND owner, const std::wstring& path);
void CopyTextToClipboard(HWND owner, const std::wstring& text);
bool PickFolder(HWND owner, const std::wstring& title, const std::wstring& initial,
                std::wstring& outPath);
bool DeleteToRecycleBin(HWND owner, const std::vector<std::wstring>& paths);
void ShowFileProperties(HWND owner, const std::wstring& path);
bool CopyFilesTo(HWND owner, const std::vector<std::wstring>& paths,
                 const std::wstring& destDir);
bool EnsureDirectory(const std::wstring& path);

std::wstring RegReadStr(const wchar_t* name, const std::wstring& fallback);
void RegWriteStr(const wchar_t* name, const std::wstring& value);
DWORD RegReadDword(const wchar_t* name, DWORD fallback);
void RegWriteDword(const wchar_t* name, DWORD value);

std::wstring FormatHResult(HRESULT hr);

// Frees cached GDI+ fonts. Call after the App is destroyed and before
// GdiplusShutdown.
void ShutdownPaintResources();

// ---------------------------------------------------------------- UI constants

namespace ui {

constexpr Gdiplus::ARGB C(unsigned a, unsigned r, unsigned g, unsigned b) {
    return (static_cast<Gdiplus::ARGB>(a) << 24) | (static_cast<Gdiplus::ARGB>(r) << 16) |
           (static_cast<Gdiplus::ARGB>(g) << 8) | static_cast<Gdiplus::ARGB>(b);
}

constexpr Gdiplus::ARGB WithAlpha(Gdiplus::ARGB c, unsigned a) {
    return (c & 0x00FFFFFFu) | (static_cast<Gdiplus::ARGB>(a) << 24);
}

constexpr Gdiplus::ARGB TextPrimary   = C(255, 244, 244, 249);
constexpr Gdiplus::ARGB TextSecondary = C(255, 162, 165, 180);
constexpr Gdiplus::ARGB TextMuted     = C(255, 112, 115, 132);
constexpr Gdiplus::ARGB Accent        = C(255, 139, 92, 246);
constexpr Gdiplus::ARGB AccentSoft    = C(255, 167, 139, 250);
constexpr Gdiplus::ARGB Accent2       = C(255, 34, 211, 238);
constexpr Gdiplus::ARGB Success       = C(255, 74, 222, 128);
constexpr Gdiplus::ARGB Danger        = C(255, 248, 113, 113);
constexpr Gdiplus::ARGB CardFill      = C(16, 255, 255, 255);
constexpr Gdiplus::ARGB CardFillHover = C(28, 255, 255, 255);
constexpr Gdiplus::ARGB Border        = C(30, 255, 255, 255);
constexpr Gdiplus::ARGB BorderSoft    = C(16, 255, 255, 255);

constexpr Gdiplus::ARGB KindColor(ItemKind kind) {
    switch (kind) {
        case ItemKind::Image:    return AccentSoft;
        case ItemKind::Video:    return Accent2;
        case ItemKind::Audio:    return Success;
        case ItemKind::Program:  return C(255, 251, 191, 36);
        case ItemKind::Folder:   return C(255, 96, 165, 250);
        default:                 return TextMuted;
    }
}

}  // namespace ui

// ---------------------------------------------------------------- window ids

namespace zid {
enum : int {
    None = 0,
    Min, Max, Close, Pin, Settings,
    TabLibrary, TabDrive,
    Back, Up, Refresh, OpenFolder, Search, ClearSearch,
    FilterAll, FilterImages, FilterVideos, FilterAudio,
    Recursive,
    DriveDetect, DrivePick, DriveWeb,
    DriveChange, DriveRefresh,
    SettingsPanel, OpacityTrack, PinToggle,
    EditorBack, EditorSave, EditorUndo,
    EditorPlay, EditorStop, EditorTrim, EditorDelete,
    EditorFadeIn, EditorFadeOut, EditorVolDown, EditorVolUp,
    Waveform,
    ZoomOut, ZoomReset, ZoomIn,
    UpdateButton,
    SidebarBase = 1000,
    CardBase = 2000
};
}  // namespace zid

struct Zone {
    int id = zid::None;
    Gdiplus::RectF rect{};
};

struct CardHit {
    int index = -1;
    Gdiplus::RectF rect{};
};

struct SideEntry {
    enum class Kind { Section, Item } kind = Kind::Item;
    std::wstring label;
    std::wstring glyph;
    std::wstring path;
    int action = 0;
    bool active = false;
    Gdiplus::RectF rect{};
};

struct Layout {
    Gdiplus::RectF panel{};
    Gdiplus::RectF header{};
    Gdiplus::RectF toolbar{};
    Gdiplus::RectF filters{};
    Gdiplus::RectF content{};
    Gdiplus::RectF sidebar{};
    Gdiplus::RectF grid{};
    Gdiplus::RectF status{};
    Gdiplus::RectF pathPill{};
    Gdiplus::RectF search{};
    Gdiplus::RectF searchClear{};
    Gdiplus::RectF zoomGroup{};
    Gdiplus::RectF updateButton{};
    Gdiplus::RectF sidebarTip{};
    bool showSidebarTip = false;
    Gdiplus::RectF scrollThumb{};
    bool showScrollThumb = false;

    // Drive connect screen
    Gdiplus::RectF connectPanel{};
    Gdiplus::RectF connectDetect{};
    Gdiplus::RectF connectPick{};
    Gdiplus::RectF connectWeb{};
    bool showConnect = false;

    // Settings overlay
    Gdiplus::RectF settingsPanel{};
    Gdiplus::RectF opacityTrack{};
    Gdiplus::RectF toggleSwitch{};

    // Audio editor
    Gdiplus::RectF editor{};
    Gdiplus::RectF editorTop{};
    Gdiplus::RectF editorWave{};
    Gdiplus::RectF editorPlot{};
    Gdiplus::RectF editorBottom{};
    Gdiplus::RectF editorPlay{};
    Gdiplus::RectF editorStop{};

    std::vector<Zone> zones;
    std::vector<CardHit> cards;
    std::vector<SideEntry> side;
    std::vector<int> visible;
    int columns = 1;
    float contentHeight = 0.f;
};

struct HitInfo {
    int id = zid::None;
    int card = -1;
    int resizeEdge = 0;  // bitmask 1=L,2=R,4=T,8=B
};

// ---------------------------------------------------------------- application

class App {
public:
    App() = default;
    ~App();

    bool Create(HINSTANCE instance, bool showWindow = true);
    int Run();
    HWND Hwnd() const { return hwnd_; }

    // Internal, non-interactive snapshot support used for visual regression
    // checks. It never displays a window.
    void PrepareSnapshot(const std::wstring& mode, const std::wstring& argument);
    bool SaveSnapshot(const std::wstring& path);

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void CreateRuntimeIcon();
    void LoadSettings();
    void SaveSettings();
    void LoadQuickFolders();
    void ApplyTopmost();

    ViewState& View();
    const ViewState& View() const;
    bool EditorOpen() const { return editorOpen_; }

    void ScanFolder(ViewState& view);
    void RunDeepSearch(ViewState& view);
    void Navigate(ViewState& view, const std::wstring& path, bool addHistory);
    void RefreshCurrent();
    void GoBack();
    void GoUp();
    void SwitchTab(Tab tab);

    Layout BuildLayout();
    const Layout& EnsureLayout();
    HitInfo HitTest(POINT client);
    const Gdiplus::RectF* ZoneRect(const Layout& layout, int id) const;
    bool IsClickable(int id) const;

    void Render(bool rebuildLayout = true);
    void EnsureBuffer(int width, int height);
    void EnsureBackgroundCache(const Layout& layout);
    bool EasterEggActive() const;
    Gdiplus::Bitmap* EasterEggBitmap();
    void DrawEasterEgg(Gdiplus::Graphics& g, const Layout& layout);

    void DrawWindow(Gdiplus::Graphics& g, const Layout& layout);
    void DrawBackground(Gdiplus::Graphics& g, const Layout& layout);
    void DrawHeader(Gdiplus::Graphics& g, const Layout& layout);
    void DrawToolbar(Gdiplus::Graphics& g, const Layout& layout);
    void DrawFilters(Gdiplus::Graphics& g, const Layout& layout);
    void DrawSidebar(Gdiplus::Graphics& g, const Layout& layout);
    void DrawGrid(Gdiplus::Graphics& g, const Layout& layout);
    void DrawCard(Gdiplus::Graphics& g, const CardHit& card, bool selected, bool hover,
                  const MediaItem& item);
    void DrawStatus(Gdiplus::Graphics& g, const Layout& layout);
    void DrawSettings(Gdiplus::Graphics& g, const Layout& layout);
    void DrawConnect(Gdiplus::Graphics& g, const Layout& layout);
    void DrawEditor(Gdiplus::Graphics& g, const Layout& layout);

    void BuildSidebar(Layout& layout);

    void OnLButtonDown(POINT pt, bool ctrl, bool shift);
    void OnLButtonUp(POINT pt);
    void OnLButtonDblClk(POINT pt);
    void OnMouseMove(POINT pt, bool leftDown);
    void OnMouseWheel(int delta);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key, bool ctrl, bool shift, bool alt);
    void OnTimer(UINT_PTR id);
    void OnClickZone(int id);
    void OnClickCard(int index, bool ctrl, bool shift);
    void OnCardDoubleClick(int index);
    void OnDroppedFiles(HDROP drop);

    void SelectCard(int index, bool ctrl, bool shift);
    void OpenItem(const MediaItem& item);
    void OpenSelected();
    void ShowContextMenu(int index);
    void HandleMenuCommand(int cmd);
    void StartDragSelection();
    std::vector<std::wstring> SelectedPaths() const;
    bool IsSelected(const std::wstring& path) const;
    bool SelectionIsSingleAudio() const;
    std::vector<int> VisibleIndices(const ViewState& view) const;
    Filter& CurrentFilter();

    void SetToast(const std::wstring& text, bool error = false);
    bool ToastActive() const;
    std::wstring HoverItemName(const Layout& layout) const;
    void SearchChanged(bool immediateDeep);
    void ClearSearch(bool keepFocus);
    void EnsureSearchFocus();
    void RunPendingDeepSearch();

    void DetectDrive(bool notify);
    void ChooseDriveFolder();
    void SetDriveRoot(const std::wstring& path, bool navigate);
    void DeriveDriveMount();
    void OpenDriveWeb();
    std::vector<std::wstring> DriveSharedCandidates() const;

    void ToggleMaximize();
    void SetPin(bool pin);
    void SetOpacity(int alpha);
    void SaveWindowPlacement();
    int ShadowMargin() const;
    float S(float value) const;
    void SetZoom(int zoomPercent);
    void InstallUpdateNow();
    void StartUpdateCheck();
    void UpdateUninstallVersion();

    void OpenAudioEditor(const std::wstring& path);
    bool ConfirmCloseEditor();
    void CloseAudioEditor();
    void RebuildAudioPeaks();
    void PushUndo(const std::wstring& label);
    void UndoAudioEdit();
    void TrimToSelection();
    void DeleteSelection();
    void FadeSelection(bool fadeIn);
    void ChangeVolume(double factor);
    void SaveAudioAs();
    void BeginAudioPlayback();
    void PauseAudioPlayback();
    void ResumeAudioPlayback();
    void StopAudioPlayback(bool resetPlayhead);
    void UpdateAudioPlayback();
    void SeekToFrame(int64_t frame, bool extendSelection);
    void FinishWaveDrag(POINT pt);
    int64_t FrameAtPoint(float x);
    void RebuildPeaksFromSamples();
    bool HasSelection() const { return hasSelection_ && selectionEnd_ > selectionStart_; }
    void ClearAudioSelection();

    // state ------------------------------------------------------------
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    int dpi_ = 96;
    bool ready_ = false;

    Tab tab_ = Tab::Library;
    ViewState library_;
    ViewState drive_;
    std::wstring driveRoot_;
    std::wstring driveMount_;

    std::vector<QuickFolder> quick_;
    std::unordered_set<std::wstring> selection_;
    std::wstring anchorPath_;

    int hoverId_ = zid::None;
    int hoverCard_ = -1;
    int pressCard_ = -1;
    POINT pressPoint_{};
    bool dragArmed_ = false;
    bool movingWindow_ = false;
    bool resizingWindow_ = false;
    int resizeEdge_ = 0;
    POINT dragStartCursor_{};
    RECT dragStartRect_{};
    bool maximized_ = false;
    RECT restoreRect_{};
    bool pin_ = true;
    int opacity_ = 232;
    bool settingsOpen_ = false;
    bool draggingOpacity_ = false;
    bool draggingOut_ = false;
    int zoom_ = 100;

    std::unique_ptr<ThumbnailLoader> thumbs_;
    uint64_t lastThumbPaint_ = 0;
    uint64_t lastScrollTick_ = 0;
    bool scrollTimerActive_ = false;
    Layout layoutCache_;
    bool layoutCacheValid_ = false;
    std::wstring toast_;
    uint64_t toastUntil_ = 0;
    bool toastIsError_ = false;
    bool searchFocused_ = false;
    bool caretOn_ = true;
    bool pendingDeep_ = false;
    bool updateCheckStarted_ = false;
    bool updateReady_ = false;
    std::wstring updateVersion_;
    std::wstring updatePath_;

    // rendering
    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    void* bits_ = nullptr;
    int bufferW_ = 0;
    int bufferH_ = 0;
    int windowConstantAlpha_ = 255;
    std::unique_ptr<Gdiplus::Bitmap> backgroundCache_;
    int backgroundW_ = 0;
    int backgroundH_ = 0;
    int backgroundDpi_ = 0;
    int backgroundOpacity_ = -1;
    bool backgroundMaximized_ = false;
    bool backgroundEaster_ = false;
    std::unique_ptr<Gdiplus::Bitmap> easterEgg_;
    bool easterEggTried_ = false;

    // audio editor
    bool editorOpen_ = false;
    AudioClip audio_;
    std::unique_ptr<AudioPlayer> player_;
    bool waveDragging_ = false;
    bool waveMoved_ = false;
    int64_t waveAnchor_ = 0;
    int64_t playhead_ = 0;
    int64_t selectionStart_ = 0;
    int64_t selectionEnd_ = 0;
    bool hasSelection_ = false;
    int64_t playBegin_ = 0;
    int64_t playEnd_ = 0;
    std::vector<int16_t> undoData_;
    int undoChannels_ = 0;
    int undoRate_ = 0;
    bool canUndo_ = false;
    std::wstring undoLabel_;
    bool audioLoading_ = false;
    bool mfReady_ = false;
};

}  // namespace nebula
