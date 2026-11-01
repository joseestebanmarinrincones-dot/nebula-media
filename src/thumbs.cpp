#include "common.h"

#include <algorithm>
#include <vector>

namespace nebula {

namespace {

constexpr size_t kCacheLimit = 180;
constexpr size_t kDiskCacheMaxFiles = 400;
constexpr size_t kDiskCacheTargetFiles = 300;

uint64_t DiskKey(const std::wstring& path, uint64_t size, uint64_t writeTime) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (i * 8)) & 0xFF;
            hash *= 1099511628211ull;
        }
    };
    for (wchar_t c : path) {
        hash ^= static_cast<uint16_t>(c);
        hash *= 1099511628211ull;
    }
    mix(size);
    mix(writeTime);
    mix(1);  // Cache format version.
    return hash;
}

std::wstring CacheDirectory() {
    static const std::wstring dir = [] {
        const std::wstring base = KnownFolderPath(FOLDERID_LocalAppData);
        if (base.empty()) return std::wstring();
        const std::wstring path =
            JoinPath(JoinPath(base, L"Nebula Media"), L"ThumbCache");
        if (!EnsureDirectory(path)) return std::wstring();
        return path;
    }();
    return dir;
}

std::wstring DiskCacheFile(const std::wstring& path,
                           const WIN32_FILE_ATTRIBUTE_DATA& info) {
    const std::wstring dir = CacheDirectory();
    if (dir.empty()) return std::wstring();
    const uint64_t size =
        (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const uint64_t stamp =
        (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
        info.ftLastWriteTime.dwLowDateTime;
    wchar_t name[40]{};
    swprintf(name, 40, L"%016llx.png",
             static_cast<unsigned long long>(DiskKey(path, size, stamp)));
    return JoinPath(dir, name);
}

std::mutex g_diskCacheMutex;
size_t g_diskCacheCount = 0;

size_t PruneDiskCacheLocked() {
    const std::wstring dir = CacheDirectory();
    if (dir.empty()) return 0;

    struct Entry {
        std::wstring path;
        FILETIME time{};
    };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(JoinPath(dir, L"*.png").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            Entry entry;
            entry.path = JoinPath(dir, data.cFileName);
            entry.time = data.ftLastWriteTime;
            entries.push_back(std::move(entry));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    // Remove leftovers from an interrupted save.
    HANDLE temp = FindFirstFileW(JoinPath(dir, L"*.tmp").c_str(), &data);
    if (temp != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                DeleteFileW(JoinPath(dir, data.cFileName).c_str());
            }
        } while (FindNextFileW(temp, &data));
        FindClose(temp);
    }

    if (entries.size() <= kDiskCacheMaxFiles) return entries.size();

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return CompareFileTime(&a.time, &b.time) < 0;
    });
    const size_t remove =
        entries.size() > kDiskCacheTargetFiles ? entries.size() - kDiskCacheTargetFiles : 0;
    for (size_t i = 0; i < remove; ++i) {
        DeleteFileW(entries[i].path.c_str());
    }
    return entries.size() - remove;
}

void EnsureDiskCachePruned() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::lock_guard<std::mutex> lock(g_diskCacheMutex);
        g_diskCacheCount = PruneDiskCacheLocked();
    });
}

void NoteSavedThumb() {
    std::lock_guard<std::mutex> lock(g_diskCacheMutex);
    ++g_diskCacheCount;
    if (g_diskCacheCount > kDiskCacheMaxFiles) {
        g_diskCacheCount = PruneDiskCacheLocked();
    }
}

std::shared_ptr<Gdiplus::Bitmap> LoadDiskThumb(const std::wstring& file) {
    Gdiplus::Bitmap disk(file.c_str(), FALSE);
    if (disk.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const int width = static_cast<int>(disk.GetWidth());
    const int height = static_cast<int>(disk.GetHeight());
    if (width <= 0 || height <= 0) return nullptr;
    Gdiplus::Bitmap* clone =
        disk.Clone(0, 0, width, height, PixelFormat32bppPARGB);
    if (!clone || clone->GetLastStatus() != Gdiplus::Ok) {
        delete clone;
        return nullptr;
    }
    return std::shared_ptr<Gdiplus::Bitmap>(clone);
}

const CLSID* PngEncoder() {
    static std::once_flag once;
    static CLSID clsid{};
    static bool found = false;
    std::call_once(once, [] {
        UINT count = 0;
        UINT bytes = 0;
        if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok ||
            bytes == 0) {
            return;
        }
        std::vector<BYTE> storage(bytes);
        auto* encoders =
            reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
        if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
            return;
        }
        for (UINT i = 0; i < count; ++i) {
            if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
                clsid = encoders[i].Clsid;
                found = true;
                break;
            }
        }
    });
    return found ? &clsid : nullptr;
}

void SaveDiskThumb(const std::wstring& file, Gdiplus::Bitmap* bitmap) {
    if (!bitmap) return;
    const CLSID* encoder = PngEncoder();
    if (!encoder) return;

    const std::wstring temp = file + L".tmp";
    DeleteFileW(temp.c_str());
    if (bitmap->Save(temp.c_str(), encoder, nullptr) != Gdiplus::Ok) {
        DeleteFileW(temp.c_str());
        return;
    }
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return;
    }
    NoteSavedThumb();
}

std::shared_ptr<Gdiplus::Bitmap> BitmapFromHBitmap(HBITMAP handle) {
    if (!handle) return nullptr;
    BITMAP bitmap{};
    if (!GetObjectW(handle, sizeof(bitmap), &bitmap) || bitmap.bmWidth <= 0 ||
        bitmap.bmHeight <= 0) {
        return nullptr;
    }
    const int width = bitmap.bmWidth;
    const int height = bitmap.bmHeight;
    const int stride = width * 4;
    std::vector<BYTE> pixels(static_cast<size_t>(stride) * height, 0);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, handle, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines == 0) return nullptr;

    bool hasAlpha = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            hasAlpha = true;
            break;
        }
    }
    if (!hasAlpha) {
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    }

    auto result = std::make_shared<Gdiplus::Bitmap>(width, height,
                                                    PixelFormat32bppARGB);
    if (result->GetLastStatus() != Gdiplus::Ok) return nullptr;

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (result->LockBits(&rect, Gdiplus::ImageLockModeWrite,
                         PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return nullptr;
    }
    for (int y = 0; y < height; ++y) {
        memcpy(static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
               pixels.data() + static_cast<size_t>(y) * stride,
               static_cast<size_t>(stride));
    }
    result->UnlockBits(&data);
    return result;
}

}  // namespace

ThumbnailLoader::ThumbnailLoader(HWND notify) : hwnd_(notify) {}

ThumbnailLoader::~ThumbnailLoader() {
    Stop();
    cache_.clear();
    failed_.clear();
    order_.clear();
}

void ThumbnailLoader::Start() {
    if (started_) return;
    started_ = true;
    stop_ = false;
    const int count = 2;
    for (int i = 0; i < count; ++i) {
        workers_.emplace_back(&ThumbnailLoader::Worker, this);
    }
}

void ThumbnailLoader::Stop() {
    if (!started_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
        queued_.clear();
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    started_ = false;
}

void ThumbnailLoader::Request(const std::wstring& path) {
    if (!started_ || path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.count(path) || failed_.count(path) || queued_.count(path)) return;
        queued_.insert(path);
        queue_.push_back(path);
    }
    cv_.notify_one();
}

std::shared_ptr<Gdiplus::Bitmap> ThumbnailLoader::Get(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(path);
    return it == cache_.end() ? nullptr : it->second;
}

bool ThumbnailLoader::Failed(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_.count(path) != 0;
}

void ThumbnailLoader::ClearQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    queued_.clear();
}

std::shared_ptr<Gdiplus::Bitmap> ThumbnailLoader::RenderThumb(const std::wstring& path) {
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                           IID_PPV_ARGS(&factory))) ||
        !factory) {
        return nullptr;
    }

    HBITMAP handle = nullptr;
    SIZE size{360, 240};
    HRESULT hr = factory->GetImage(size, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK,
                                   &handle);
    if (FAILED(hr) || !handle) {
        SIZE iconSize{96, 96};
        factory->GetImage(iconSize, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &handle);
    }
    factory->Release();
    if (!handle) return nullptr;

    auto bitmap = BitmapFromHBitmap(handle);
    DeleteObject(handle);
    return bitmap;
}

void ThumbnailLoader::Worker() {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    (void)com;
    EnsureDiskCachePruned();

    for (;;) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
            if (stop_) break;
            path = queue_.front();
            queue_.pop_front();
        }

        std::shared_ptr<Gdiplus::Bitmap> bitmap;
        std::wstring cacheFile;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            cacheFile = DiskCacheFile(path, info);
            if (!cacheFile.empty()) bitmap = LoadDiskThumb(cacheFile);
        }
        if (!bitmap) {
            bitmap = RenderThumb(path);
            if (bitmap && !cacheFile.empty()) SaveDiskThumb(cacheFile, bitmap.get());
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            queued_.erase(path);
            if (bitmap) {
                if (!cache_.count(path)) {
                    cache_[path] = bitmap;
                    order_.push_back(path);
                    while (order_.size() > kCacheLimit) {
                        const std::wstring& old = order_.front();
                        cache_.erase(old);
                        order_.pop_front();
                    }
                }
            } else {
                failed_.insert(path);
            }
        }
        if (hwnd_) PostMessageW(hwnd_, WM_APP_THUMB, 0, 0);
    }

    CoUninitialize();
}

}  // namespace nebula
