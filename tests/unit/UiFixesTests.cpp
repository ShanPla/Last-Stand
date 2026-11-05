// Tests for fixes found in review of the page shell, saving and debug switch. The parts
// that need a window or a browser are covered by the browser tests (uifixes.test.mjs).
#include "doctest.h"

#include "core/DebugSwitch.h"
#include "core/Save.h"
#include "core/SaveFile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ls;

namespace {

namespace fs = std::filesystem;

// A fresh folder for one test, removed again when the test ends.
struct TempFolder {
    fs::path path;
    TempFolder() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("laststand-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void writeRaw(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readRaw(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---- The debug overlay -----------------------------------------------------------

TEST_CASE("a release build hides the debug overlay unless asked with --debug") {
    const char* plain[] = {"LastStand.exe"};
    const char* asked[] = {"LastStand.exe", "--debug"};
    const char* other[] = {"LastStand.exe", "--fullscreen", "debug"};
    CHECK_FALSE(platform::nativeDebugWanted(true, 1, plain));
    CHECK_FALSE(platform::nativeDebugWanted(true, 3, other));
    CHECK(platform::nativeDebugWanted(true, 2, asked));
    CHECK_FALSE(platform::nativeDebugWanted(true, 0, nullptr));
}

TEST_CASE("a developer build always shows the debug overlay") {
    const char* plain[] = {"LastStand.exe"};
    CHECK(platform::nativeDebugWanted(false, 1, plain));
    CHECK(platform::nativeDebugWanted(false, 0, nullptr));
}

// ---- Best runs from another tab or copy ---------------------------------------------

TEST_CASE("merging keeps the higher best run of each field, and never lowers one") {
    SaveData mine;
    mine.best[1].bestTimeSeconds = 60.0f;
    mine.best[1].mostKills = 500;
    mine.best[1].highestLevel = 3;
    mine.best[1].wins = 0;
    mine.best[1].runs = 1;

    SaveData stored;
    stored.best[1].bestTimeSeconds = 300.0f;  // Another tab survived longer.
    stored.best[1].mostKills = 200;           // But killed less.
    stored.best[1].highestLevel = 9;
    stored.best[1].wins = 1;
    stored.best[1].runs = 4;
    stored.best[0].mostKills = 77;  // A different level is merged on its own.

    save::mergeBestRuns(mine, stored);
    CHECK(mine.best[1].bestTimeSeconds == doctest::Approx(300.0f));
    CHECK(mine.best[1].mostKills == 500);
    CHECK(mine.best[1].highestLevel == 9);
    CHECK(mine.best[1].wins == 1);
    CHECK(mine.best[1].runs == 4);
    CHECK(mine.best[0].mostKills == 77);
    CHECK(mine.best[2].mostKills == 0);
}

TEST_CASE("merging never touches the settings in this copy") {
    SaveData mine;
    mine.settings.musicVolume = 0.1f;
    mine.settings.reducedMotion = true;
    mine.map = 1;
    SaveData stored;
    stored.settings.musicVolume = 0.9f;
    stored.settings.reducedMotion = false;
    stored.map = 0;
    save::mergeBestRuns(mine, stored);
    CHECK(mine.settings.musicVolume == doctest::Approx(0.1f));
    CHECK(mine.settings.reducedMotion);
    CHECK(mine.map == 1);
}

// ---- The save file on disk ------------------------------------------------------------

TEST_CASE("a missing save file is Missing, which is safe to create") {
    TempFolder folder;
    std::string text = "left over";
    CHECK(savefile::readFile(folder.path / "none.save", &text) == savefile::ReadResult::Missing);
    CHECK(text.empty());
}

TEST_CASE("a save that cannot be read is Unreadable, never treated as no save") {
    TempFolder folder;
    // A folder with the save's name stands in for a file that is locked or has no permission:
    // it exists, but its content can't be read.
    const fs::path blocked = folder.path / "laststand.save";
    fs::create_directories(blocked);
    std::string text;
    CHECK(savefile::readFile(blocked, &text) == savefile::ReadResult::Unreadable);
}

TEST_CASE("a written save reads back, and a second write replaces it") {
    TempFolder folder;
    const fs::path file = folder.path / "laststand.save";
    CHECK(savefile::writeFileSafely(file, "version=1\nmusic=0.500\n"));
    std::string text;
    CHECK(savefile::readFile(file, &text) == savefile::ReadResult::Found);
    CHECK(text == "version=1\nmusic=0.500\n");

    CHECK(savefile::writeFileSafely(file, "version=1\nmusic=0.900\n"));
    CHECK(readRaw(file) == "version=1\nmusic=0.900\n");
    // The temporary file is gone once the move is done.
    CHECK_FALSE(fs::exists(folder.path / "laststand.save.tmp"));
}

TEST_CASE("a failed write leaves the old save whole") {
    TempFolder folder;
    const fs::path file = folder.path / "laststand.save";
    writeRaw(file, "version=1\nbestTime=421.50\n");
    // A folder where the temporary file must go makes the write fail early.
    fs::create_directories(folder.path / "laststand.save.tmp");
    CHECK_FALSE(savefile::writeFileSafely(file, "version=1\nbestTime=0.00\n"));
    CHECK(readRaw(file) == "version=1\nbestTime=421.50\n");
}

TEST_CASE("writing into a folder that does not exist fails without creating anything") {
    TempFolder folder;
    const fs::path file = folder.path / "missing-folder" / "laststand.save";
    CHECK_FALSE(savefile::writeFileSafely(file, "version=1\n"));
    CHECK_FALSE(fs::exists(folder.path / "missing-folder"));
}
