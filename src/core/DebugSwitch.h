#pragma once

#include <cstring>

namespace ls::platform {

// Decides whether the native build shows the debug overlay. Players run the Release
// build, so it stays hidden there unless they start the program with --debug. Any
// other build (a developer's own) shows it. Kept as a plain function with no window
// code, so a unit test can check it.
inline bool nativeDebugWanted(bool releaseBuild, int argc, const char* const* argv) {
    if (!releaseBuild) return true;
    for (int i = 1; i < argc; ++i) {
        if (argv != nullptr && argv[i] != nullptr && std::strcmp(argv[i], "--debug") == 0) return true;
    }
    return false;
}

}  // namespace ls::platform
