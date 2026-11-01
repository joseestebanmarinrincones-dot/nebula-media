#include "common.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace nebula {

namespace {

constexpr int kPeakBuckets = 2048;
constexpr int kMaxMinutes = 10;

template <class T>
class Com {
public:
    Com() = default;
    ~Com() {
        if (p) p->Release();
    }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T* get() const { return p; }
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T* p = nullptr;
};

std::once_flag g_mfOnce;
bool g_mfOk = false;

// Media Foundation is loaded on demand so the player libraries never delay
// the normal launch of the media library.
using MfStartupFn = HRESULT(STDMETHODCALLTYPE*)(ULONG, ULONG);
using MfCreateMediaTypeFn = HRESULT(STDMETHODCALLTYPE*)(IMFMediaType**);
using MfCreateSourceReaderFn = HRESULT(STDMETHODCALLTYPE*)(LPCWSTR, IMFAttributes*,
                                                           IMFSourceReader**);

MfCreateMediaTypeFn g_mfCreateMediaType = nullptr;
MfCreateSourceReaderFn g_mfCreateSourceReader = nullptr;

const GUID kMediaTypeAudio = {0x73647561,
                              0x0000,
                              0x0010,
                              {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kAudioFormatPcm = {0x00000001,
                              0x0000,
                              0x0010,
                              {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kAudioFormatFloat = {0x00000003,
                                0x0000,
                                0x0010,
                                {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kMtMajorType = {0x48EBA18E,
                           0xF8C9,
                           0x4687,
                           {0xBF, 0x11, 0x0A, 0x74, 0xC9, 0xF9, 0x6A, 0x8F}};
const GUID kMtSubtype = {0xF7E34C9A,
                         0x42E8,
                         0x4714,
                         {0xB7, 0x4B, 0xCB, 0x29, 0xD7, 0x2C, 0x35, 0xE5}};
const GUID kMtAudioChannels = {0x37E48BF5,
                               0x645E,
                               0x4C5B,
                               {0x89, 0xDE, 0xAD, 0xA9, 0xE2, 0x9B, 0x69, 0x6A}};
const GUID kMtAudioRate = {0x5FAEEAE7,
                           0x0290,
                           0x4C31,
                           {0x9E, 0x8A, 0xC5, 0x34, 0xF6, 0x8D, 0x9D, 0xBA}};
const GUID kMtAudioBits = {0xF2DEB57F,
                           0x40FA,
                           0x4764,
                           {0xAA, 0x33, 0xED, 0x4F, 0x2D, 0x1F, 0xF6, 0x69}};

bool EnsureMediaFoundation(std::wstring& error) {
    std::call_once(g_mfOnce, []() {
        HMODULE plat = LoadLibraryW(L"mfplat.dll");
        HMODULE readwrite = LoadLibraryW(L"mfreadwrite.dll");
        if (!plat || !readwrite) return;

        auto startup =
            reinterpret_cast<MfStartupFn>(GetProcAddress(plat, "MFStartup"));
        auto createType = reinterpret_cast<MfCreateMediaTypeFn>(
            GetProcAddress(plat, "MFCreateMediaType"));
        auto createReader = reinterpret_cast<MfCreateSourceReaderFn>(
            GetProcAddress(readwrite, "MFCreateSourceReaderFromURL"));
        if (!startup || !createType || !createReader) return;
        if (FAILED(startup(MF_VERSION, MFSTARTUP_FULL))) return;

        g_mfCreateMediaType = createType;
        g_mfCreateSourceReader = createReader;
        g_mfOk = true;
    });
    if (!g_mfOk) {
        error = L"No se pudo iniciar Media Foundation.";
        return false;
    }
    return true;
}

int16_t ToInt16(float v) {
    v = std::max(-1.0f, std::min(1.0f, v));
    return static_cast<int16_t>(std::lround(v * 32767.0f));
}

bool WriteAll(HANDLE file, const void* data, size_t bytes, std::wstring& error) {
    const BYTE* p = static_cast<const BYTE*>(data);
    while (bytes > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes, 1u << 22));
        DWORD written = 0;
        if (!WriteFile(file, p, chunk, &written, nullptr) || written == 0) {
            error = L"No se pudo escribir el archivo WAV.";
            return false;
        }
        p += written;
        bytes -= written;
    }
    return true;
}

void WriteU32(BYTE*& p, uint32_t v) {
    p[0] = static_cast<BYTE>(v & 0xFF);
    p[1] = static_cast<BYTE>((v >> 8) & 0xFF);
    p[2] = static_cast<BYTE>((v >> 16) & 0xFF);
    p[3] = static_cast<BYTE>((v >> 24) & 0xFF);
    p += 4;
}

void WriteU16(BYTE*& p, uint16_t v) {
    p[0] = static_cast<BYTE>(v & 0xFF);
    p[1] = static_cast<BYTE>((v >> 8) & 0xFF);
    p += 2;
}

}  // namespace

void RebuildPeaks(AudioClip& clip) {
    clip.peakMin.assign(kPeakBuckets, 0.0f);
    clip.peakMax.assign(kPeakBuckets, 0.0f);
    const int64_t frames = ClipFrames(clip);
    if (frames <= 0 || clip.channels <= 0) return;

    const int ch = clip.channels;
    const int16_t* data = clip.samples.data();
    for (int b = 0; b < kPeakBuckets; ++b) {
        int64_t begin = frames * b / kPeakBuckets;
        int64_t end = frames * (b + 1) / kPeakBuckets;
        if (end <= begin) end = begin + 1;
        if (end > frames) end = frames;
        float mn = 1.0f;
        float mx = -1.0f;
        for (int64_t f = begin; f < end; ++f) {
            int sum = 0;
            for (int c = 0; c < ch; ++c) sum += data[f * ch + c];
            float v = static_cast<float>(sum) / (32768.0f * ch);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        if (mn > mx) {
            mn = mx = 0.0f;
        }
        clip.peakMin[b] = mn;
        clip.peakMax[b] = mx;
    }
}

bool LoadAudioFile(const std::wstring& path, AudioClip& out, std::wstring& error) {
    error.clear();
    out = AudioClip{};
    out.sourcePath = path;
    out.displayName = FileNameOf(path);

    if (!FileExists(path)) {
        error = L"El archivo de audio ya no existe.";
        return false;
    }
    if (!EnsureMediaFoundation(error)) return false;

    Com<IMFSourceReader> reader;
    HRESULT hr =
        g_mfCreateSourceReader(path.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        error = L"No se pudo abrir el audio con Media Foundation. " + FormatHResult(hr);
        return false;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    bool configured = false;
    {
        Com<IMFMediaType> type;
        if (SUCCEEDED(g_mfCreateMediaType(&type))) {
            type->SetGUID(kMtMajorType, kMediaTypeAudio);
            type->SetGUID(kMtSubtype, kAudioFormatPcm);
            type->SetUINT32(kMtAudioBits, 16);
            type->SetUINT32(kMtAudioChannels, 2);
            type->SetUINT32(kMtAudioRate, 48000);
            configured = SUCCEEDED(reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.get()));
        }
    }
    if (!configured) {
        Com<IMFMediaType> type;
        if (SUCCEEDED(g_mfCreateMediaType(&type))) {
            type->SetGUID(kMtMajorType, kMediaTypeAudio);
            type->SetGUID(kMtSubtype, kAudioFormatPcm);
            configured = SUCCEEDED(reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.get()));
        }
    }
    if (!configured) {
        error = L"Este formato de audio no es compatible con el editor.";
        return false;
    }

    UINT32 channels = 2;
    UINT32 rate = 48000;
    UINT32 bits = 16;
    GUID subtype = kAudioFormatPcm;
    {
        Com<IMFMediaType> current;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                                  &current)) &&
            current) {
            current->GetUINT32(kMtAudioChannels, &channels);
            current->GetUINT32(kMtAudioRate, &rate);
            current->GetUINT32(kMtAudioBits, &bits);
            current->GetGUID(kMtSubtype, &subtype);
        }
    }

    if (channels == 0 || channels > 8) channels = 2;
    if (rate < 4000 || rate > 384000) rate = 48000;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) bits = 16;

    const bool isFloat = subtype == kAudioFormatFloat;
    if (bits == 8 || (bits == 32 && !isFloat)) {
        error = L"Profundidad de audio no compatible.";
        return false;
    }

    const int outChannels = channels == 1 ? 1 : 2;
    const int bytesPerSample = static_cast<int>(bits / 8);
    const int blockAlign = static_cast<int>(channels) * bytesPerSample;
    const int64_t maxFrames =
        static_cast<int64_t>(rate) * 60 * kMaxMinutes;
    out.channels = outChannels;
    out.rate = static_cast<int>(rate);
    out.samples.reserve(static_cast<size_t>(std::min<int64_t>(maxFrames,
                                                              static_cast<int64_t>(rate) * 60 * 3)) *
                        outChannels);

    bool failed = false;
    bool truncated = false;

    for (;;) {
        DWORD flags = 0;
        DWORD actualStream = 0;
        LONGLONG timestamp = 0;
        Com<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &actualStream,
                                &flags, &timestamp, &sample);
        if (FAILED(hr)) {
            error = L"Error al leer el audio. " + FormatHResult(hr);
            failed = true;
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            // Extremely rare for audio-only files; keep the configured format.
        }

        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) continue;

        BYTE* data = nullptr;
        DWORD maxLen = 0;
        DWORD curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen)) || !data) continue;

        const int64_t framesInBuffer = blockAlign > 0 ? (curLen / blockAlign) : 0;
        int64_t framesToRead = framesInBuffer;
        const int64_t remaining = maxFrames - ClipFrames(out);
        if (remaining <= 0) {
            buffer->Unlock();
            truncated = true;
            break;
        }
        if (framesToRead > remaining) {
            framesToRead = remaining;
            truncated = true;
        }

        for (int64_t f = 0; f < framesToRead; ++f) {
            const BYTE* frame = data + f * blockAlign;
            int16_t values[8]{};
            for (UINT32 c = 0; c < channels && c < 8; ++c) {
                const BYTE* s = frame + c * bytesPerSample;
                int16_t value = 0;
                if (bits == 16) {
                    value = *reinterpret_cast<const int16_t*>(s);
                } else if (bits == 24) {
                    int32_t v = static_cast<int32_t>(s[0]) |
                                (static_cast<int32_t>(s[1]) << 8) |
                                (static_cast<int32_t>(s[2]) << 16);
                    if (v & 0x00800000) v |= static_cast<int32_t>(0xFF000000);
                    value = static_cast<int16_t>(v >> 8);
                } else if (bits == 32 && isFloat) {
                    float v = 0.0f;
                    memcpy(&v, s, 4);
                    value = ToInt16(v);
                } else if (bits == 32) {
                    int32_t v = 0;
                    memcpy(&v, s, 4);
                    value = static_cast<int16_t>(v >> 16);
                }
                values[c] = value;
            }
            if (outChannels == 1) {
                out.samples.push_back(values[0]);
            } else {
                out.samples.push_back(values[0]);
                out.samples.push_back(channels >= 2 ? values[1] : values[0]);
            }
        }

        buffer->Unlock();
        if (truncated) break;
    }

    if (failed) {
        out = AudioClip{};
        return false;
    }

    if (out.samples.empty()) {
        error = L"El archivo no contiene audio que se pueda editar.";
        out = AudioClip{};
        return false;
    }

    // Keep whole frames only.
    const size_t usable = (out.samples.size() / outChannels) * outChannels;
    if (usable != out.samples.size()) out.samples.resize(usable);

    out.truncated = truncated;
    out.dirty = false;
    RebuildPeaks(out);
    return true;
}

bool SaveWavFile(const std::wstring& path, const AudioClip& clip, std::wstring& error) {
    error.clear();
    const int64_t frames = ClipFrames(clip);
    if (frames <= 0 || clip.channels <= 0) {
        error = L"No hay audio para guardar.";
        return false;
    }
    const uint64_t dataBytes64 =
        static_cast<uint64_t>(frames) * clip.channels * sizeof(int16_t);
    if (dataBytes64 > 0xFFFFFFFFull - 64) {
        error = L"El audio es demasiado largo para guardarlo como WAV.";
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"No se pudo crear el archivo WAV.";
        return false;
    }

    BYTE header[44]{};
    BYTE* p = header;
    memcpy(p, "RIFF", 4);
    p += 4;
    WriteU32(p, static_cast<uint32_t>(36 + dataBytes64));
    memcpy(p, "WAVEfmt ", 8);
    p += 8;
    WriteU32(p, 16);
    WriteU16(p, 1);
    WriteU16(p, static_cast<uint16_t>(clip.channels));
    WriteU32(p, static_cast<uint32_t>(clip.rate));
    WriteU32(p, static_cast<uint32_t>(clip.rate) * clip.channels * 2);
    WriteU16(p, static_cast<uint16_t>(clip.channels * 2));
    WriteU16(p, 16);
    memcpy(p, "data", 4);
    p += 4;
    WriteU32(p, static_cast<uint32_t>(dataBytes64));

    bool ok = WriteAll(file, header, sizeof(header), error);
    if (ok) {
        ok = WriteAll(file, clip.samples.data(),
                      static_cast<size_t>(dataBytes64), error);
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

bool PickSaveWav(HWND owner, const std::wstring& suggestedFile, std::wstring& outPath) {
    outPath.clear();
    IFileSaveDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    const COMDLG_FILTERSPEC filters[] = {{L"Audio WAV", L"*.wav"}};
    dialog->SetFileTypes(1, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"wav");
    dialog->SetTitle(L"Guardar audio como WAV");
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT |
                       FOS_PATHMUSTEXIST);

    std::wstring name = FileNameOf(suggestedFile);
    if (name.empty()) name = L"audio-editado.wav";
    dialog->SetFileName(name.c_str());

    std::wstring folder = ParentPath(suggestedFile);
    if (!folder.empty() && DirExists(folder)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item))) &&
            item) {
            dialog->SetFolder(item);
            item->Release();
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
    if (!outPath.empty() && ExtensionOf(outPath) != L".wav") {
        outPath = StemOf(outPath) + L".wav";
    }
    return !outPath.empty();
}

// ------------------------------------------------------------------ playback

AudioPlayer::~AudioPlayer() { Close(); }

bool AudioPlayer::Open(int rate, int channels) {
    Close();
    if (rate <= 0 || channels <= 0 || channels > 8) return false;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = static_cast<WORD>(channels);
    fmt.nSamplesPerSec = static_cast<DWORD>(rate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = static_cast<WORD>(channels * 2);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    MMRESULT result = waveOutOpen(&out_, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        out_ = nullptr;
        return false;
    }
    fmt_ = fmt;
    return true;
}

void AudioPlayer::Close() {
    Stop();
    if (out_) {
        waveOutClose(out_);
        out_ = nullptr;
    }
    fmt_ = WAVEFORMATEX{};
}

bool AudioPlayer::Play(const int16_t* data, int64_t totalFrames, int channels, int rate,
                       int64_t startFrame, int64_t endFrame) {
    if (!data || channels <= 0 || totalFrames <= 0) return false;
    if (startFrame < 0) startFrame = 0;
    if (endFrame > totalFrames) endFrame = totalFrames;
    if (startFrame >= endFrame) return false;

    if (!out_ || fmt_.nChannels != channels || fmt_.nSamplesPerSec != static_cast<DWORD>(rate)) {
        if (!Open(rate, channels)) return false;
    }
    Stop();

    const int64_t frames = endFrame - startFrame;
    const uint64_t bytes64 =
        static_cast<uint64_t>(frames) * channels * sizeof(int16_t);
    if (bytes64 == 0 || bytes64 > 0xFFFFFFFFull) return false;

    hdr_ = WAVEHDR{};
    hdr_.lpData = reinterpret_cast<LPSTR>(
        const_cast<int16_t*>(data) + startFrame * channels);
    hdr_.dwBufferLength = static_cast<DWORD>(bytes64);
    hdr_.dwFlags = 0;

    MMRESULT result = waveOutPrepareHeader(out_, &hdr_, sizeof(hdr_));
    if (result != MMSYSERR_NOERROR) return false;
    prepared_ = true;

    result = waveOutWrite(out_, &hdr_, sizeof(hdr_));
    if (result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(out_, &hdr_, sizeof(hdr_));
        prepared_ = false;
        return false;
    }

    playing_ = true;
    paused_ = false;
    startFrame_ = startFrame;
    endFrame_ = endFrame;
    return true;
}

void AudioPlayer::Pause() {
    if (!out_ || !playing_ || paused_) return;
    waveOutPause(out_);
    paused_ = true;
}

void AudioPlayer::Resume() {
    if (!out_ || !playing_ || !paused_) return;
    waveOutRestart(out_);
    paused_ = false;
}

void AudioPlayer::Stop() {
    if (out_) waveOutReset(out_);
    if (out_ && prepared_) {
        for (int i = 0; i < 20; ++i) {
            MMRESULT result = waveOutUnprepareHeader(out_, &hdr_, sizeof(hdr_));
            if (result != WAVERR_STILLPLAYING) break;
            Sleep(2);
        }
    }
    prepared_ = false;
    playing_ = false;
    paused_ = false;
}

bool AudioPlayer::Finished() const {
    if (!playing_ || !prepared_) return true;
    return (hdr_.dwFlags & WHDR_DONE) != 0;
}

int64_t AudioPlayer::PositionFrames() const {
    if (!out_ || !playing_) return startFrame_;
    MMTIME time{};
    time.wType = TIME_SAMPLES;
    MMRESULT result = waveOutGetPosition(out_, &time, sizeof(time));
    int64_t played = 0;
    if (result == MMSYSERR_NOERROR && time.wType == TIME_SAMPLES) {
        played = static_cast<int64_t>(time.u.sample);
    } else if (result == MMSYSERR_NOERROR && time.wType == TIME_BYTES &&
               fmt_.nBlockAlign > 0) {
        played = static_cast<int64_t>(time.u.cb) / fmt_.nBlockAlign;
    }
    const int64_t total = endFrame_ - startFrame_;
    if (played < 0) played = 0;
    if (played > total) played = total;
    return startFrame_ + played;
}

}  // namespace nebula
