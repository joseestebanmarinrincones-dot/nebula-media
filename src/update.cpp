#include "common.h"
#include "update_config.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace nebula {

namespace {

constexpr size_t kMaxFeedBytes = 64 * 1024;
constexpr size_t kMaxUpdateBytes = 100 * 1024 * 1024;

struct UrlParts {
    std::wstring host;
    std::wstring path;
};

std::wstring Win32Error(const wchar_t* prefix) {
    wchar_t buffer[64]{};
    swprintf(buffer, 64, L" (%lu)", GetLastError());
    return std::wstring(prefix) + buffer;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                   nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::wstring Trim(std::wstring text) {
    while (!text.empty() && std::iswspace(text.front())) text.erase(text.begin());
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    return text;
}

bool ParseVersion(const std::wstring& version, std::vector<int>& parts) {
    parts.clear();
    std::wstring current;
    for (wchar_t c : version) {
        if (c == L'.') {
            if (current.empty() || current.size() > 8) return false;
            parts.push_back(std::stoi(current));
            current.clear();
        } else if (c >= L'0' && c <= L'9') {
            current.push_back(c);
        } else {
            return false;
        }
    }
    if (current.empty() || current.size() > 8) return false;
    parts.push_back(std::stoi(current));
    return !parts.empty();
}

bool IsNewer(const std::wstring& latest, const std::wstring& current) {
    std::vector<int> a;
    std::vector<int> b;
    if (!ParseVersion(latest, a) || !ParseVersion(current, b)) return false;
    const size_t count = std::max(a.size(), b.size());
    for (size_t i = 0; i < count; ++i) {
        int av = i < a.size() ? a[i] : 0;
        int bv = i < b.size() ? b[i] : 0;
        if (av != bv) return av > bv;
    }
    return false;
}

bool CrackHttpsUrl(const std::wstring& url, UrlParts& parts, std::wstring& error) {
    if (url.rfind(L"https://", 0) != 0) {
        error = L"La URL de actualización no usa HTTPS.";
        return false;
    }
    const std::wstring rest = url.substr(8);
    const size_t slash = rest.find(L'/');
    if (slash == std::wstring::npos) {
        error = L"URL de actualización inválida.";
        return false;
    }
    parts.host = rest.substr(0, slash);
    parts.path = rest.substr(slash);
    if (parts.host.empty() || parts.path.empty() ||
        parts.host.find(L':') != std::wstring::npos) {
        error = L"URL de actualización inválida.";
        return false;
    }
    return true;
}

bool HttpGet(const std::wstring& url, size_t maxBytes, std::vector<BYTE>& data,
             std::wstring& error) {
    data.clear();
    UrlParts parts;
    if (!CrackHttpsUrl(url, parts, error)) return false;

    HINTERNET session = WinHttpOpen(
        L"NebulaMedia/" NEBULA_VERSION_W L" (Windows)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = Win32Error(L"No se pudo iniciar WinHTTP");
        return false;
    }
    WinHttpSetTimeouts(session, 6000, 6000, 10000, 20000);

    HINTERNET connection =
        WinHttpConnect(session, parts.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        error = Win32Error(L"No se pudo conectar con GitHub");
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        error = Win32Error(L"No se pudo crear la petición HTTPS");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                     sizeof(redirect));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                     sizeof(protocols));

    bool ok = false;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = Win32Error(L"No se pudo enviar la petición");
    } else if (!WinHttpReceiveResponse(request, nullptr)) {
        error = Win32Error(L"No se recibió respuesta de GitHub");
    } else {
        DWORD status = 0;
        DWORD length = sizeof(status);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
                                 WINHTTP_NO_HEADER_INDEX)) {
            error = Win32Error(L"No se pudo leer el estado HTTP");
        } else if (status != 200) {
            wchar_t buffer[64]{};
            swprintf(buffer, 64, L"GitHub respondió HTTP %lu.", status);
            error = buffer;
        } else {
            ok = true;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    error = Win32Error(L"No se pudo leer la respuesta");
                    ok = false;
                    break;
                }
                if (available == 0) break;
                if (data.size() + available > maxBytes) {
                    error = L"La respuesta de actualización es demasiado grande.";
                    ok = false;
                    break;
                }
                std::vector<BYTE> chunk(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                    error = Win32Error(L"No se pudo descargar la actualización");
                    ok = false;
                    break;
                }
                if (read == 0) break;
                data.insert(data.end(), chunk.begin(), chunk.begin() + read);
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

bool Sha256File(const std::wstring& path, std::wstring& hex, std::wstring& error) {
    hex.clear();
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<BYTE> hashObject;
    std::vector<BYTE> digest;
    DWORD objectLength = 0;
    DWORD digestLength = 0;
    DWORD resultLength = 0;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        !algorithm) {
        error = L"No se pudo iniciar SHA-256.";
        return false;
    }
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                          &resultLength, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&digestLength), sizeof(digestLength),
                          &resultLength, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        error = L"No se pudo preparar SHA-256.";
        return false;
    }
    hashObject.resize(objectLength);
    digest.resize(digestLength);
    if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0,
                         0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        error = L"No se pudo iniciar SHA-256.";
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"No se pudo leer el archivo descargado.";
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    bool ok = true;
    std::vector<BYTE> buffer(256 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                      nullptr)) {
            error = L"No se pudo leer el archivo descargado.";
            ok = false;
            break;
        }
        if (read == 0) break;
        if (BCryptHashData(hash, buffer.data(), read, 0) < 0) {
            error = L"No se pudo calcular SHA-256.";
            ok = false;
            break;
        }
    }
    CloseHandle(file);

    if (ok && BCryptFinishHash(hash, digest.data(), digestLength, 0) < 0) {
        error = L"No se pudo terminar SHA-256.";
        ok = false;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;

    static const wchar_t* digits = L"0123456789abcdef";
    hex.reserve(digest.size() * 2);
    for (BYTE byte : digest) {
        hex.push_back(digits[(byte >> 4) & 0xF]);
        hex.push_back(digits[byte & 0xF]);
    }
    return true;
}

bool WriteAllBytes(const std::wstring& path, const std::vector<BYTE>& data,
                   std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"No se pudo guardar la actualización.";
        return false;
    }
    bool ok = true;
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(data.size() - offset, 1024 * 1024));
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            error = L"No se pudo guardar la actualización.";
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool ParseFeed(const std::wstring& text, std::wstring& version, std::wstring& url,
               std::wstring& sha256) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        const size_t equals = line.find(L'=');
        if (equals != std::wstring::npos) {
            std::wstring key = Trim(line.substr(0, equals));
            std::wstring value = Trim(line.substr(equals + 1));
            for (wchar_t& c : key) c = static_cast<wchar_t>(std::towlower(c));
            if (key == L"version") version = value;
            if (key == L"url") url = value;
            if (key == L"sha256") sha256 = value;
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    if (version.rfind(L"v", 0) == 0) version.erase(version.begin());
    if (version.empty() || url.empty() || sha256.size() != 64) return false;
    if (url.rfind(L"https://github.com/", 0) != 0 ||
        url.find(L"/releases/download/") == std::wstring::npos ||
        url.find(L".exe") == std::wstring::npos) {
        return false;
    }
    for (wchar_t c : sha256) {
        if (!std::iswxdigit(c)) return false;
    }
    return true;
}

}  // namespace

UpdateResult CheckForUpdatesBlocking(const std::wstring& currentVersion) {
    UpdateResult result;
    const std::wstring feedUrl = std::wstring(L"https://github.com/") +
                                 NEBULA_REPO_OWNER_W + L"/" + NEBULA_REPO_NAME_W +
                                 L"/releases/latest/download/latest.txt";

    std::vector<BYTE> feedBytes;
    std::wstring error;
    if (!HttpGet(feedUrl, kMaxFeedBytes, feedBytes, error)) {
        result.error = error;
        return result;
    }
    std::string feedUtf8(reinterpret_cast<const char*>(feedBytes.data()),
                         feedBytes.size());
    const std::wstring feed = Utf8ToWide(feedUtf8);

    std::wstring version;
    std::wstring url;
    std::wstring expectedSha;
    if (!ParseFeed(feed, version, url, expectedSha)) {
        result.error = L"El archivo de actualización de GitHub no es válido.";
        return result;
    }
    if (!IsNewer(version, currentVersion)) {
        return result;
    }

    result.available = true;
    result.version = version;

    const std::wstring updatesDir = JoinPath(
        JoinPath(KnownFolderPath(FOLDERID_LocalAppData), L"Nebula Media"), L"Updates");
    if (!EnsureDirectory(updatesDir)) {
        result.error = L"No se pudo crear la carpeta de actualizaciones.";
        return result;
    }
    const std::wstring target = JoinPath(updatesDir, L"NebulaMedia-" + version + L".exe");

    std::wstring actualSha;
    if (FileExists(target) && Sha256File(target, actualSha, error) &&
        _wcsicmp(actualSha.c_str(), expectedSha.c_str()) == 0) {
        result.downloaded = true;
        result.path = target;
        return result;
    }

    std::vector<BYTE> exeBytes;
    if (!HttpGet(url, kMaxUpdateBytes, exeBytes, error)) {
        result.error = error;
        return result;
    }
    if (exeBytes.empty()) {
        result.error = L"La actualización descargada está vacía.";
        return result;
    }

    const std::wstring part = target + L".part";
    if (!WriteAllBytes(part, exeBytes, error)) {
        result.error = error;
        return result;
    }
    if (!Sha256File(part, actualSha, error) ||
        _wcsicmp(actualSha.c_str(), expectedSha.c_str()) != 0) {
        DeleteFileW(part.c_str());
        result.error = L"La firma SHA-256 de la actualización no coincide.";
        return result;
    }
    DeleteFileW(target.c_str());
    if (!MoveFileExW(part.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(part.c_str());
        result.error = L"No se pudo preparar el archivo de actualización.";
        return result;
    }

    result.downloaded = true;
    result.path = target;
    (void)WideToUtf8;
    return result;
}

bool LaunchSelfUpdate(const std::wstring& updatePath, std::wstring& error) {
    error.clear();
    if (!FileExists(updatePath)) {
        error = L"El archivo de actualización ya no existe.";
        return false;
    }

    wchar_t exeBuffer[32768]{};
    DWORD exeLength = GetModuleFileNameW(nullptr, exeBuffer, 32768);
    if (exeLength == 0 || exeLength >= 32768) {
        error = L"No se pudo localizar el ejecutable actual.";
        return false;
    }
    const std::wstring target(exeBuffer, exeLength);
    const std::wstring updatesDir = ParentPath(updatePath);
    if (updatesDir.empty() || !EnsureDirectory(updatesDir)) {
        error = L"No se pudo preparar la carpeta de actualización.";
        return false;
    }

    const std::wstring scriptPath = JoinPath(updatesDir, L"apply-update.ps1");
    const std::string script = R"PS(param([int]$AppPid,[string]$Target,[string]$Source)
$ErrorActionPreference = 'SilentlyContinue'
for ($i = 0; $i -lt 240; $i++) {
    if (-not (Get-Process -Id $AppPid -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 250
}
$copied = $false
for ($i = 0; $i -lt 60; $i++) {
    try {
        Copy-Item -LiteralPath $Source -Destination $Target -Force -ErrorAction Stop
        $copied = $true
        break
    } catch {
        Start-Sleep -Milliseconds 250
    }
}
if ($copied) {
    Remove-Item -LiteralPath $Source -Force -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
)PS";
    std::vector<BYTE> scriptBytes(script.begin(), script.end());
    if (!WriteAllBytes(scriptPath, scriptBytes, error)) return false;

    wchar_t windowsDir[MAX_PATH]{};
    UINT windowsLength = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (windowsLength == 0 || windowsLength >= MAX_PATH) {
        error = L"No se pudo localizar Windows PowerShell.";
        return false;
    }
    const std::wstring powershell =
        std::wstring(windowsDir) +
        L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (!FileExists(powershell)) {
        error = L"No se encontró Windows PowerShell.";
        return false;
    }

    auto quoted = [](const std::wstring& value) {
        return L"\"" + value + L"\"";
    };
    const std::wstring commandLine =
        quoted(powershell) +
        L" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File " +
        quoted(scriptPath) + L" -AppPid " +
        std::to_wstring(GetCurrentProcessId()) + L" -Target " + quoted(target) +
        L" -Source " + quoted(updatePath);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = commandLine;
    if (!CreateProcessW(powershell.c_str(), mutableCommand.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = Win32Error(L"No se pudo iniciar el actualizador");
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace nebula
