#include "core/SaveFile.h"

#ifndef __EMSCRIPTEN__

#include <fstream>
#include <system_error>

namespace ls::savefile {

namespace {
constexpr std::streamsize kMaxReadBytes = 8192;  // A save is far smaller. More is not ours.
}

ReadResult readFile(const std::filesystem::path& path, std::string* text) {
    text->clear();
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) return ReadResult::Missing;
    if (error) return ReadResult::Unreadable;

    std::ifstream in(path, std::ios::binary);
    if (!in) return ReadResult::Unreadable;
    char buffer[kMaxReadBytes];
    in.read(buffer, kMaxReadBytes);
    // Reaching the end of a short file sets eof and fail together, which is normal.
    // Only a real read error (bad) means the data can't be trusted.
    if (in.bad()) return ReadResult::Unreadable;
    text->assign(buffer, static_cast<size_t>(in.gcount()));
    return ReadResult::Found;
}

bool writeFileSafely(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    std::error_code error;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        out.close();  // The data reaches the disk here, so errors show up in the state below.
        if (out.fail()) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }
    // Replacing a file in one step: a crash now leaves either the old file or the new one.
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

}  // namespace ls::savefile

#endif
