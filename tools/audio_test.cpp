#include "../src/common.h"

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t* path = argc > 1 ? argv[1] : L"build\\test-tone.wav";
    nebula::AudioClip clip;
    std::wstring error;
    if (!nebula::LoadAudioFile(path, clip, error)) {
        wprintf(L"LOAD FAILED (%ls): %ls\n", path, error.c_str());
        CoUninitialize();
        return 1;
    }
    wprintf(L"loaded %ls: frames=%lld rate=%d channels=%d peaks=%zu truncated=%d\n",
            path, static_cast<long long>(nebula::ClipFrames(clip)), clip.rate,
            clip.channels, clip.peakMin.size(), clip.truncated ? 1 : 0);
    CoUninitialize();
    return 0;
}
