#include "core/NativePaths.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ls::platform {

namespace {

#ifdef _WIN32
// A game's folder next to the program can be read-only (a zip, Program Files, a shortcut that
// starts somewhere else), and APPDATA is the place Windows keeps per-user program data.
std::wstring buildSavePath() {
    wchar_t appData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();

    std::wstring folder(appData, length);
    folder += L"\\LastStand";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return std::wstring();
    return folder + L"\\laststand.save";
}
#endif

}  // namespace

const std::wstring& nativeSavePath() {
#ifdef _WIN32
    // Worked out once. If the folder can't be made now, it will not be later either.
    static const std::wstring path = buildSavePath();
    return path;
#else
    static const std::wstring path;
    return path;
#endif
}

}  // namespace ls::platform
