#include "../src/common.h"

#include <cstdio>

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::wstring> paths = {
        L"C:\\Users\\Gamer\\Videos\\clip uno.mp4",
        L"C:\\Users\\Gamer\\Music\\tema.mp3",
    };
    IDataObject* data = nebula::CreateFileDataObject(paths);
    if (!data) {
        wprintf(L"no data object\n");
        CoUninitialize();
        return 1;
    }

    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (data->QueryGetData(&format) != S_OK) {
        wprintf(L"QueryGetData failed\n");
        data->Release();
        CoUninitialize();
        return 1;
    }

    STGMEDIUM medium{};
    HRESULT hr = data->GetData(&format, &medium);
    if (FAILED(hr) || medium.hGlobal == nullptr) {
        wprintf(L"GetData failed 0x%08X\n", static_cast<unsigned>(hr));
        data->Release();
        CoUninitialize();
        return 1;
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(medium.hGlobal));
    if (!drop || !drop->fWide) {
        wprintf(L"bad DROPFILES\n");
        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        data->Release();
        CoUninitialize();
        return 1;
    }
    auto* text = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(drop) + drop->pFiles);
    int count = 0;
    while (*text) {
        wprintf(L"path[%d]=%ls\n", count, text);
        text += wcslen(text) + 1;
        ++count;
    }
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    data->Release();
    CoUninitialize();
    return count == 2 ? 0 : 1;
}
