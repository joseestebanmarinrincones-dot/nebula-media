#include "common.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <vector>

namespace nebula {

namespace {
constexpr const wchar_t* kRegKey = L"Software\\NebulaMedia";

std::wstring NormalizePath(std::wstring s) {
    for (wchar_t& c : s) {
        if (c == L'/') c = L'\\';
    }
    while (s.size() > 1 && s.back() == L'\\') {
        if (s.size() == 3 && s[1] == L':') break;
        s.pop_back();
    }
    return s;
}

std::wstring ReplaceDotWithComma(std::wstring s) {
    for (wchar_t& c : s) {
        if (c == L'.') c = L',';
    }
    return s;
}

std::wstring FindChildDir(const std::wstring& parent,
                          std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* name : names) {
        std::wstring candidate = JoinPath(parent, name);
        if (DirExists(candidate)) return candidate;
    }
    return L"";
}

std::wstring FirstExisting(const std::vector<std::wstring>& paths) {
    for (const auto& p : paths) {
        if (DirExists(p)) return p;
    }
    return L"";
}

}  // namespace

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool EndsWithNoCase(const std::wstring& s, const std::wstring& suffix) {
    if (s.size() < suffix.size()) return false;
    return _wcsicmp(s.c_str() + (s.size() - suffix.size()), suffix.c_str()) == 0;
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return a.empty() && b.empty();
    return _wcsicmp(NormalizePath(a).c_str(), NormalizePath(b).c_str()) == 0;
}

bool DirExists(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring left = a;
    while (!left.empty() && (left.back() == L'\\' || left.back() == L'/')) {
        if (left.size() == 3 && left[1] == L':') break;
        left.pop_back();
    }
    std::wstring right = b;
    while (!right.empty() && (right.front() == L'\\' || right.front() == L'/')) {
        right.erase(right.begin());
    }
    return left + L"\\" + right;
}

std::wstring ParentPath(const std::wstring& path) {
    std::wstring p = NormalizePath(path);
    if (p.empty()) return L"";
    if (p.size() == 3 && p[1] == L':') return p;
    size_t slash = p.find_last_of(L'\\');
    if (slash == std::wstring::npos) return L"";
    if (slash == 2 && p[1] == L':') return p.substr(0, 3);
    if (slash == 0) return p.substr(0, 1);
    return p.substr(0, slash);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring StemOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return name;
    return name.substr(0, dot);
}

std::wstring ExtensionOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return L"";
    return ToLower(name.substr(dot));
}

std::wstring RelativeTo(const std::wstring& path, const std::wstring& base) {
    std::wstring p = NormalizePath(path);
    std::wstring b = NormalizePath(base);
    if (b.empty()) return p;
    if (p.size() > b.size() && _wcsnicmp(p.c_str(), b.c_str(), b.size()) == 0 &&
        (p[b.size()] == L'\\' || p[b.size()] == L'/')) {
        return p.substr(b.size() + 1);
    }
    return p;
}

std::wstring FormatSize(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[64]{};
    if (unit == 0) {
        swprintf(buf, 64, L"%llu B", static_cast<unsigned long long>(bytes));
        return buf;
    }
    if (value < 10.0) {
        swprintf(buf, 64, L"%.1f %s", value, units[unit]);
    } else {
        swprintf(buf, 64, L"%.0f %s", value, units[unit]);
    }
    return ReplaceDotWithComma(buf);
}

ItemKind KindForFile(const std::wstring& path, bool isDir) {
    if (isDir) return ItemKind::Folder;
    const std::wstring ext = ExtensionOf(path);

    static const std::unordered_set<std::wstring> images = {
        L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".tif", L".tiff",
        L".heic", L".heif", L".avif", L".svg", L".psd", L".raw", L".cr2", L".nef",
        L".dng", L".ico"};
    static const std::unordered_set<std::wstring> videos = {
        L".mp4", L".mov", L".mkv", L".avi", L".wmv", L".webm", L".m4v", L".mpg",
        L".mpeg", L".flv", L".ts", L".mts", L".m2ts", L".3gp", L".ogv", L".mxf",
        L".vob"};
    static const std::unordered_set<std::wstring> audios = {
        L".mp3", L".wav", L".flac", L".aac", L".ogg", L".m4a", L".wma", L".aiff",
        L".aif", L".opus", L".mid", L".midi", L".amr", L".ape"};

    if (images.count(ext)) return ItemKind::Image;
    if (videos.count(ext)) return ItemKind::Video;
    if (audios.count(ext)) return ItemKind::Audio;
    return ItemKind::Other;
}

std::wstring KindMeta(const MediaItem& item) {
    if (item.isDir) {
        std::wstring s = L"Carpeta";
        if (!item.context.empty()) s += L" · " + item.context;
        return s;
    }
    std::wstring ext = item.ext;
    if (!ext.empty() && ext.front() == L'.') ext.erase(ext.begin());
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towupper(c));
    if (ext.empty()) ext = L"ARCHIVO";
    std::wstring s = ext + L" · " + FormatSize(item.size);
    if (!item.context.empty()) s += L" · " + item.context;
    return s;
}

std::wstring KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) {
        if (raw) CoTaskMemFree(raw);
        return L"";
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::wstring DetectGoogleDrive(std::wstring& mountRoot) {
    mountRoot.clear();

    wchar_t drives[512]{};
    DWORD len = GetLogicalDriveStringsW(511, drives);
    if (len > 0) {
        for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
            std::wstring root(p);
            UINT type = GetDriveTypeW(root.c_str());
            if (type != DRIVE_FIXED && type != DRIVE_REMOTE) continue;

            wchar_t label[256]{};
            bool labelOk = GetVolumeInformationW(root.c_str(), label, 256, nullptr,
                                                 nullptr, nullptr, nullptr, 0) != 0;
            const bool googleLabel =
                labelOk && ToLower(label).find(L"google") != std::wstring::npos;

            std::wstring myDrive = FindChildDir(root, {L"My Drive", L"Mi unidad"});
            const bool sharedLayout =
                DirExists(JoinPath(root, L"Shared drives")) ||
                DirExists(JoinPath(root, L"Unidades compartidas")) ||
                DirExists(JoinPath(root, L"Other computers")) ||
                DirExists(JoinPath(root, L"Otros equipos"));
            if (!googleLabel && !(sharedLayout && !myDrive.empty())) continue;

            mountRoot = root;
            if (!myDrive.empty()) return myDrive;
            return root;
        }
    }

    std::wstring profile = KnownFolderPath(FOLDERID_Profile);
    std::wstring homeDrive = FirstExisting({
        JoinPath(profile, L"Google Drive\\My Drive"),
        JoinPath(profile, L"Google Drive\\Mi unidad"),
        JoinPath(profile, L"Google Drive"),
        JoinPath(profile, L"My Drive"),
    });
    if (!homeDrive.empty()) {
        mountRoot = ParentPath(homeDrive);
        return homeDrive;
    }
    return L"";
}

bool IsCloudPlaceholder(DWORD attributes) {
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    constexpr DWORD recall = 0x00400000;  // FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
    constexpr DWORD recallOpen = 0x00040000;  // FILE_ATTRIBUTE_RECALL_ON_OPEN
    return (attributes & (recall | recallOpen | FILE_ATTRIBUTE_OFFLINE)) != 0;
}

void OpenWithDefault(HWND owner, const std::wstring& path) {
    HINSTANCE result = ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr,
                                     SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(owner,
                    (L"No se pudo abrir este archivo.\n\n" + path).c_str(),
                    L"Nébula Media", MB_OK | MB_ICONWARNING);
    }
}

void RevealInExplorer(HWND owner, const std::wstring& path) {
    if (DirExists(path)) {
        ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(owner, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dest = GlobalLock(mem);
        if (dest) {
            memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

bool PickFolder(HWND owner, const std::wstring& title, const std::wstring& initial,
                std::wstring& outPath) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!title.empty()) dialog->SetTitle(title.c_str());

    if (!initial.empty() && DirExists(initial)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                                  IID_PPV_ARGS(&folder))) &&
            folder) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    hr = dialog->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
                outPath = raw;
                CoTaskMemFree(raw);
            }
            result->Release();
        }
    }
    dialog->Release();
    return !outPath.empty();
}

bool DeleteToRecycleBin(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    std::wstring from;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        from += p;
        from.push_back(L'\0');
    }
    from.push_back(L'\0');
    if (from.size() <= 1) return false;

    SHFILEOPSTRUCTW op{};
    op.hwnd = owner;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO;
    int result = SHFileOperationW(&op);
    return result == 0 && !op.fAnyOperationsAborted;
}

void ShowFileProperties(HWND owner, const std::wstring& path) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.hwnd = owner;
    info.lpVerb = L"properties";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

bool CopyFilesTo(HWND owner, const std::vector<std::wstring>& paths,
                 const std::wstring& destDir) {
    if (paths.empty() || !DirExists(destDir)) return false;
    std::wstring from;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        from += p;
        from.push_back(L'\0');
    }
    from.push_back(L'\0');
    if (from.size() <= 1) return false;

    std::wstring to = destDir;
    to.push_back(L'\0');
    to.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.hwnd = owner;
    op.wFunc = FO_COPY;
    op.pFrom = from.c_str();
    op.pTo = to.c_str();
    op.fFlags = FOF_ALLOWUNDO;
    int result = SHFileOperationW(&op);
    return result == 0 && !op.fAnyOperationsAborted;
}

std::wstring RegReadStr(const wchar_t* name, const std::wstring& fallback) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(key);
        return fallback;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer.data()), &bytes) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return fallback;
    }
    RegCloseKey(key);
    buffer.back() = L'\0';
    return std::wstring(buffer.data());
}

void RegWriteStr(const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

DWORD RegReadDword(const wchar_t* name, DWORD fallback) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type,
                                      reinterpret_cast<LPBYTE>(&value), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_DWORD || bytes != sizeof(value)) {
        return fallback;
    }
    return value;
}

void RegWriteDword(const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                   sizeof(value));
    RegCloseKey(key);
}

std::wstring FormatTime(double seconds, bool hundredths) {
    if (!(seconds > 0.0)) seconds = 0.0;
    long long totalMs = static_cast<long long>(seconds * 1000.0 + 0.5);
    long long h = totalMs / 3600000;
    long long m = (totalMs / 60000) % 60;
    long long s = (totalMs / 1000) % 60;
    long long cs = (totalMs / 10) % 100;
    wchar_t buf[64]{};
    if (h > 0) {
        if (hundredths) {
            swprintf(buf, 64, L"%lld:%02lld:%02lld.%02lld", h, m, s, cs);
        } else {
            swprintf(buf, 64, L"%lld:%02lld:%02lld", h, m, s);
        }
    } else {
        if (hundredths) {
            swprintf(buf, 64, L"%02lld:%02lld.%02lld", m, s, cs);
        } else {
            swprintf(buf, 64, L"%02lld:%02lld", m, s);
        }
    }
    return buf;
}

std::wstring FormatHResult(HRESULT hr) {
    wchar_t buf[32]{};
    swprintf(buf, 32, L"0x%08X", static_cast<unsigned>(hr));
    return buf;
}

}  // namespace nebula
