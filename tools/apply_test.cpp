#include "../src/common.h"

#include <cstdio>

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    nebula::UpdateResult result = nebula::CheckForUpdatesBlocking(L"1.0.0");
    wprintf(L"available=%d downloaded=%d version=%ls\n", result.available ? 1 : 0,
            result.downloaded ? 1 : 0, result.version.c_str());
    if (!result.downloaded) {
        wprintf(L"error=%ls\n", result.error.c_str());
        CoUninitialize();
        return 1;
    }
    std::wstring error;
    const bool ok = nebula::LaunchSelfUpdate(result.path, error);
    wprintf(L"launch=%d error=%ls\n", ok ? 1 : 0, error.c_str());
    CoUninitialize();
    return ok ? 0 : 1;
}
