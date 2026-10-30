#include "../src/common.h"

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring current = argc > 1 ? argv[1] : L"1.0.0";
    const nebula::UpdateResult result = nebula::CheckForUpdatesBlocking(current);
    wprintf(L"current=%ls available=%d downloaded=%d version=%ls\n", current.c_str(),
            result.available ? 1 : 0, result.downloaded ? 1 : 0,
            result.version.c_str());
    if (!result.path.empty()) wprintf(L"path=%ls\n", result.path.c_str());
    if (!result.error.empty()) wprintf(L"error=%ls\n", result.error.c_str());
    CoUninitialize();
    return result.downloaded ? 0 : 1;
}
