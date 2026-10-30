#include "../src/common.h"

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring path = argc > 1 ? argv[1] : L"build\\media-preview\\Amanecer.png";
    std::wstring parent = nebula::ParentPath(path);
    PIDLIST_ABSOLUTE parentPidl = nullptr;
    SFGAOF attrs = 0;
    HRESULT hr = SHParseDisplayName(parent.c_str(), nullptr, &parentPidl, 0, &attrs);
    PIDLIST_ABSOLUTE filePidl = nullptr;
    if (SUCCEEDED(hr)) {
        hr = SHParseDisplayName(path.c_str(), nullptr, &filePidl, 0, &attrs);
    }
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && filePidl) {
        PCUITEMID_CHILD child = ILFindLastID(filePidl);
        hr = SHCreateDataObject(parentPidl, 1, &child, nullptr, IID_IDataObject,
                                reinterpret_cast<void**>(&data));
    }
    wprintf(L"SHCreateDataObject hr=0x%08X data=%p\n", static_cast<unsigned>(hr),
            static_cast<void*>(data));
    if (data) {
        IEnumFORMATETC* enumerator = nullptr;
        if (SUCCEEDED(data->EnumFormatEtc(DATADIR_GET, &enumerator)) && enumerator) {
            FORMATETC format{};
            ULONG fetched = 0;
            while (enumerator->Next(1, &format, &fetched) == S_OK) {
                wchar_t name[128]{};
                if (GetClipboardFormatNameW(format.cfFormat, name, 128) == 0) {
                    swprintf(name, 128, L"cf=%u", format.cfFormat);
                }
                wprintf(L"format: %ls\n", name);
            }
            enumerator->Release();
        }
        FORMATETC drop{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        wprintf(L"CF_HDROP QueryGetData: 0x%08X\n",
                static_cast<unsigned>(data->QueryGetData(&drop)));
        data->Release();
    }
    if (parentPidl) CoTaskMemFree(parentPidl);
    if (filePidl) CoTaskMemFree(filePidl);
    CoUninitialize();
    return data ? 0 : 1;
}
