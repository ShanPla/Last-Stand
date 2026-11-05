#pragma once

#include <filesystem>
#include <string>

// Reading and writing one save file safely. This has no window code, so the unit tests
// can use it. Platform decides which file to use. The browser build does not use this.
namespace ls::savefile {

enum class ReadResult {
    Found,       // The file was read. It may still hold nothing useful.
    Missing,     // There is no file. Safe to create one.
    Unreadable,  // The file may exist but could not be read (locked, no permission).
};

// Missing and Unreadable are different on purpose: a save that could not be read must
// not be written over with defaults, because the good data in it may still be there.
ReadResult readFile(const std::filesystem::path& path, std::string* text);

// Writes to a temporary file next to the target and then moves it into place, so the
// old file stays whole until the new one is complete. Returns false if anything failed,
// and then the old file is untouched.
bool writeFileSafely(const std::filesystem::path& path, const std::string& text);

}  // namespace ls::savefile
