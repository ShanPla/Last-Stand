#pragma once

#include <string>

namespace ls::platform {

// Where the native Windows build keeps its save file: %APPDATA%\LastStand\laststand.save.
// The folder is created on the first call. The answer is empty when that is not possible
// (not Windows, no APPDATA, or a folder that can't be created), and then the caller keeps
// to the current folder.
//
// This lives in its own file because it needs windows.h, whose names clash with raylib's
// (Rectangle, CloseWindow, ShowCursor and others), so it must never be included next to
// raylib.h.
const std::wstring& nativeSavePath();

}  // namespace ls::platform
