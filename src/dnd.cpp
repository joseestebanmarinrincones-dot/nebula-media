#include "common.h"

#include <algorithm>

namespace nebula {

namespace {

bool FormatMatches(const FORMATETC& a, const FORMATETC& b) {
    return a.cfFormat == b.cfFormat && a.dwAspect == b.dwAspect &&
           (a.tymed & b.tymed) != 0;
}

UINT PreferredDropEffectFormat() {
    static UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
    return format;
}

class FileDataObject final : public IDataObject {
public:
    explicit FileDataObject(std::vector<std::wstring> paths) : paths_(std::move(paths)) {
        FORMATETC drop{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        FORMATETC effect{static_cast<CLIPFORMAT>(PreferredDropEffectFormat()), nullptr,
                         DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        formats_.push_back(drop);
        formats_.push_back(text);
        formats_.push_back(effect);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&ref_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!format || !medium) return E_POINTER;
        if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;

        medium->pUnkForRelease = nullptr;
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = nullptr;

        if (format->cfFormat == CF_HDROP) {
            size_t chars = 1;
            for (const auto& path : paths_) chars += path.size() + 1;
            const size_t bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!memory) return E_OUTOFMEMORY;
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            if (!drop) {
                GlobalFree(memory);
                return E_OUTOFMEMORY;
            }
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            auto* dest = reinterpret_cast<wchar_t*>(
                reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
            for (const auto& path : paths_) {
                memcpy(dest, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
                dest += path.size() + 1;
            }
            *dest = L'\0';
            GlobalUnlock(memory);
            medium->hGlobal = memory;
            return S_OK;
        }

        if (format->cfFormat == CF_UNICODETEXT) {
            std::wstring text;
            for (size_t i = 0; i < paths_.size(); ++i) {
                if (i) text += L"\r\n";
                text += paths_[i];
            }
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!memory) return E_OUTOFMEMORY;
            void* dest = GlobalLock(memory);
            if (!dest) {
                GlobalFree(memory);
                return E_OUTOFMEMORY;
            }
            memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(memory);
            medium->hGlobal = memory;
            return S_OK;
        }

        if (format->cfFormat == static_cast<CLIPFORMAT>(PreferredDropEffectFormat())) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            if (!memory) return E_OUTOFMEMORY;
            auto* value = static_cast<DWORD*>(GlobalLock(memory));
            if (!value) {
                GlobalFree(memory);
                return E_OUTOFMEMORY;
            }
            *value = DROPEFFECT_COPY;
            GlobalUnlock(memory);
            medium->hGlobal = memory;
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) return E_POINTER;
        for (const auto& item : formats_) {
            if (FormatMatches(*format, item)) return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* input,
                                                    FORMATETC* output) override {
        if (!input || !output) return E_POINTER;
        *output = *input;
        output->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,
                                            IEnumFORMATETC** enumerator) override {
        if (!enumerator) return E_POINTER;
        *enumerator = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        *enumerator = new FormatEnumerator(formats_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*,
                                      DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    class FormatEnumerator final : public IEnumFORMATETC {
    public:
        explicit FormatEnumerator(std::vector<FORMATETC> formats)
            : formats_(std::move(formats)) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
                *ppv = static_cast<IEnumFORMATETC*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return static_cast<ULONG>(InterlockedIncrement(&ref_));
        }

        ULONG STDMETHODCALLTYPE Release() override {
            LONG value = InterlockedDecrement(&ref_);
            if (value == 0) delete this;
            return static_cast<ULONG>(value);
        }

        HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out,
                                       ULONG* fetched) override {
            if (!out && count) return E_POINTER;
            ULONG available = static_cast<ULONG>(formats_.size() - std::min<size_t>(
                index_, formats_.size()));
            ULONG taken = std::min(count, available);
            for (ULONG i = 0; i < taken; ++i) out[i] = formats_[index_ + i];
            index_ += taken;
            if (fetched) *fetched = taken;
            return taken == count ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
            index_ = std::min<size_t>(index_ + count, formats_.size());
            return index_ < formats_.size() ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE Reset() override {
            index_ = 0;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
            if (!out) return E_POINTER;
            auto* clone = new FormatEnumerator(formats_);
            clone->index_ = index_;
            *out = clone;
            return S_OK;
        }

    private:
        LONG ref_ = 1;
        std::vector<FORMATETC> formats_;
        size_t index_ = 0;
    };

    LONG ref_ = 1;
    std::vector<std::wstring> paths_;
    std::vector<FORMATETC> formats_;
};

class FileDropSource final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&ref_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keyState) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (keyState & MK_RBUTTON) return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG ref_ = 1;
};

}  // namespace

IDataObject* CreateFileDataObject(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return nullptr;
    return new FileDataObject(paths);
}

HRESULT BeginFileDrag(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return E_INVALIDARG;

    IDataObject* data = CreateFileDataObject(paths);
    if (!data) return E_OUTOFMEMORY;
    auto* source = new FileDropSource();
    DWORD effect = DROPEFFECT_NONE;
    HRESULT result = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    source->Release();
    data->Release();
    SetFocus(owner);
    return result;
}

}  // namespace nebula
