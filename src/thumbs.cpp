#include "common.h"

#include <algorithm>
#include <vector>

namespace nebula {

namespace {

constexpr size_t kCacheLimit = 180;

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

    for (;;) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
            if (stop_) break;
            path = queue_.front();
            queue_.pop_front();
        }

        auto bitmap = RenderThumb(path);

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
