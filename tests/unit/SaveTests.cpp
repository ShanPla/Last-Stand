#include "doctest.h"

#include "core/Save.h"

#include <cmath>
#include <limits>
#include <string>

using namespace ls;

namespace {

SaveData sample() {
    SaveData data;
    data.settings.musicVolume = 0.25f;
    data.settings.effectsVolume = 0.5f;
    data.settings.screenShake = 0.0f;
    data.settings.flashIntensity = 0.75f;
    data.settings.damageNumbers = false;
    data.settings.reducedMotion = true;
    data.best[1].bestTimeSeconds = 421.5f;
    data.best[1].mostKills = 812;
    data.best[1].highestLevel = 14;
    data.best[1].wins = 2;
    data.best[1].runs = 9;
    return data;
}

bool settingsAreSane(const SaveData& data) {
    const Settings& s = data.settings;
    const auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
    return unit(s.musicVolume) && unit(s.effectsVolume) && unit(s.screenShake) && unit(s.flashIntensity) &&
           std::isfinite(data.best[1].bestTimeSeconds) && data.best[1].bestTimeSeconds >= 0.0f && data.best[1].mostKills >= 0 &&
           data.best[1].highestLevel >= 0 && data.best[1].wins >= 0 && data.best[1].runs >= 0;
}

}  // namespace

TEST_CASE("a saved game reads back exactly") {
    const SaveData original = sample();
    const SaveData loaded = save::parse(save::serialize(original));
    CHECK(loaded.version == kSaveVersion);
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.25f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(0.5f));
    CHECK(loaded.settings.screenShake == doctest::Approx(0.0f));
    CHECK(loaded.settings.flashIntensity == doctest::Approx(0.75f));
    CHECK(loaded.settings.damageNumbers == false);
    CHECK(loaded.settings.reducedMotion == true);
    CHECK(loaded.best[1].bestTimeSeconds == doctest::Approx(421.5f));
    CHECK(loaded.best[1].mostKills == 812);
    CHECK(loaded.best[1].highestLevel == 14);
    CHECK(loaded.best[1].wins == 2);
    CHECK(loaded.best[1].runs == 9);
}

TEST_CASE("missing or empty text gives the fallback") {
    SaveData fallback;
    fallback.settings.reducedMotion = true;
    fallback.settings.musicVolume = 0.3f;
    const SaveData fromEmpty = save::parse("", fallback);
    CHECK(fromEmpty.settings.reducedMotion == true);
    CHECK(fromEmpty.settings.musicVolume == doctest::Approx(0.3f));
    CHECK(fromEmpty.best[1].mostKills == 0);
}

TEST_CASE("corrupt text never crashes and keeps defaults") {
    const char* const broken[] = {
        "garbage",
        "{{{{ not a save }}}}",
        "=\n=\n==\n",
        "music\neffects\n",
        "music=\n",
        "music=abc\nflash=--\n",
        "\x01\x02\x03\xff\xfe binary \n\n\n",
        "version=\nmusic=",
        "null",
        "undefined",
        "[object Object]",
    };
    for (const char* text : broken) {
        const SaveData loaded = save::parse(text);
        CHECK(settingsAreSane(loaded));
        CHECK(loaded.settings.musicVolume == doctest::Approx(SaveData{}.settings.musicVolume));
        CHECK(loaded.best[1].runs == 0);
    }
}

TEST_CASE("partly valid text keeps the good fields and defaults the rest") {
    const SaveData loaded = save::parse("music=0.1\nthis line is junk\neffects=oops\nwins=3\nmostKills=");
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.1f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(SaveData{}.settings.effectsVolume));
    CHECK(loaded.best[1].wins == 3);
    CHECK(loaded.best[1].mostKills == 0);
}

TEST_CASE("out of range numbers are clamped") {
    const SaveData loaded = save::parse(
        "music=9000\neffects=-5\nshake=1e300\nflash=2\nbestTime=-1\nmostKills=99999999999999\nhighestLevel=-7\nwins=1e12\nruns=-1\n");
    CHECK(loaded.settings.musicVolume == doctest::Approx(1.0f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(0.0f));
    CHECK(loaded.settings.screenShake == doctest::Approx(1.0f));
    CHECK(loaded.settings.flashIntensity == doctest::Approx(1.0f));
    CHECK(loaded.best[1].bestTimeSeconds == doctest::Approx(0.0f));
    CHECK(loaded.best[1].mostKills > 0);
    CHECK(loaded.best[1].mostKills <= 100000000);
    CHECK(loaded.best[1].highestLevel == 0);
    CHECK(loaded.best[1].wins <= 100000000);
    CHECK(loaded.best[1].runs == 0);
    CHECK(settingsAreSane(loaded));
}

TEST_CASE("NaN and infinity are rejected") {
    const SaveData loaded = save::parse("music=nan\neffects=inf\nshake=-inf\nflash=NaN\nbestTime=nan\n");
    CHECK(loaded.settings.musicVolume == doctest::Approx(SaveData{}.settings.musicVolume));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(SaveData{}.settings.effectsVolume));
    CHECK(loaded.settings.screenShake == doctest::Approx(SaveData{}.settings.screenShake));
    CHECK(loaded.settings.flashIntensity == doctest::Approx(SaveData{}.settings.flashIntensity));
    CHECK(loaded.best[1].bestTimeSeconds == doctest::Approx(0.0f));
    CHECK(settingsAreSane(loaded));
}

TEST_CASE("a NaN in the data being saved still produces a loadable file") {
    SaveData data = sample();
    data.settings.musicVolume = std::numeric_limits<float>::quiet_NaN();
    const SaveData loaded = save::parse(save::serialize(data));
    CHECK(settingsAreSane(loaded));
    CHECK(loaded.best[1].mostKills == 812);
}

TEST_CASE("a newer version with extra keys still loads what it understands") {
    const SaveData loaded = save::parse("version=99\nmusic=0.4\nfutureThing=hello\nanother=1=2\nwins=5\n");
    CHECK(loaded.version == kSaveVersion);
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.4f));
    CHECK(loaded.best[1].wins == 5);
}

TEST_CASE("an older or nonsense version number loads without error") {
    const SaveData old = save::parse("version=0\nmusic=0.6\n");
    CHECK(old.settings.musicVolume == doctest::Approx(0.6f));
    const SaveData negative = save::parse("version=-3\nmusic=0.2\n");
    CHECK(negative.settings.musicVolume == doctest::Approx(0.2f));
    const SaveData text = save::parse("version=abc\nmusic=0.9\n");
    CHECK(text.settings.musicVolume == doctest::Approx(0.9f));
}

TEST_CASE("windows line endings, spaces and repeated keys are tolerated") {
    const SaveData loaded = save::parse("music = 0.2\r\n  effects=0.3  \r\nmusic=0.8\r\n");
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.8f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(0.3f));
}

TEST_CASE("very large text is treated as corrupt") {
    std::string huge = "music=0.1\n";
    huge.append(100000, 'x');
    const SaveData loaded = save::parse(huge);
    CHECK(loaded.settings.musicVolume == doctest::Approx(SaveData{}.settings.musicVolume));
}

TEST_CASE("boolean settings only accept 0 or 1") {
    SaveData fallback;
    fallback.settings.damageNumbers = true;
    fallback.settings.reducedMotion = false;
    const SaveData loaded = save::parse("damageNumbers=maybe\nreducedMotion=2\n", fallback);
    CHECK(loaded.settings.damageNumbers == true);
    CHECK(loaded.settings.reducedMotion == false);
}

// ---- Difficulty --------------------------------------------------------------------------

TEST_CASE("the chosen difficulty and a best run for each level read back exactly") {
    SaveData original = sample();
    original.difficulty = Difficulty::Hard;
    original.bestFor(Difficulty::Easy).bestTimeSeconds = 600.0f;
    original.bestFor(Difficulty::Easy).mostKills = 4000;
    original.bestFor(Difficulty::Easy).wins = 3;
    original.bestFor(Difficulty::Hard).highestLevel = 21;
    original.bestFor(Difficulty::Hard).runs = 4;
    const SaveData loaded = save::parse(save::serialize(original));
    CHECK(loaded.difficulty == Difficulty::Hard);
    CHECK(loaded.bestFor(Difficulty::Easy).bestTimeSeconds == doctest::Approx(600.0f));
    CHECK(loaded.bestFor(Difficulty::Easy).mostKills == 4000);
    CHECK(loaded.bestFor(Difficulty::Easy).wins == 3);
    CHECK(loaded.bestFor(Difficulty::Hard).highestLevel == 21);
    CHECK(loaded.bestFor(Difficulty::Hard).runs == 4);
    // Normal is untouched by the others.
    CHECK(loaded.bestFor(Difficulty::Normal).mostKills == 812);
    CHECK(loaded.bestFor(Difficulty::Normal).bestTimeSeconds == doctest::Approx(421.5f));
    CHECK(loaded.bestFor(Difficulty::Hard).mostKills == 0);
}

TEST_CASE("an old save with no difficulty is Normal, and its best run counts as Normal") {
    // This is exactly what the game wrote before levels existed.
    const SaveData loaded = save::parse(
        "version=1\nmusic=0.500\neffects=0.800\nshake=1.000\nflash=1.000\ndamageNumbers=1\nreducedMotion=0\n"
        "bestTime=300.00\nmostKills=250\nhighestLevel=9\nwins=1\nruns=6\n");
    CHECK(loaded.version == kSaveVersion);
    CHECK(loaded.difficulty == Difficulty::Normal);
    CHECK(loaded.bestFor(Difficulty::Normal).bestTimeSeconds == doctest::Approx(300.0f));
    CHECK(loaded.bestFor(Difficulty::Normal).mostKills == 250);
    CHECK(loaded.bestFor(Difficulty::Normal).runs == 6);
    CHECK(loaded.bestFor(Difficulty::Easy).runs == 0);
    CHECK(loaded.bestFor(Difficulty::Hard).runs == 0);
}

TEST_CASE("a difficulty value out of range is clamped, and junk keeps the fallback") {
    CHECK(save::parse("difficulty=0\n").difficulty == Difficulty::Easy);
    CHECK(save::parse("difficulty=2\n").difficulty == Difficulty::Hard);
    CHECK(save::parse("difficulty=9\n").difficulty == Difficulty::Hard);
    CHECK(save::parse("difficulty=-4\n").difficulty == Difficulty::Easy);
    CHECK(save::parse("difficulty=1e99\n").difficulty == Difficulty::Hard);
    SaveData fallback;
    fallback.difficulty = Difficulty::Easy;
    for (const char* text : {"difficulty=\n", "difficulty=hard\n", "difficulty=nan\n", "difficulty=1.x\n", "difficulty==\n"}) {
        CHECK(save::parse(text, fallback).difficulty == Difficulty::Easy);
    }
    CHECK(save::parse("difficulty=hard\n").difficulty == Difficulty::Normal);
}

TEST_CASE("per-level best runs are clamped like the Normal one, and unknown prefixes are ignored") {
    const SaveData loaded = save::parse("easy.bestTime=-5\neasy.mostKills=99999999999\nhard.wins=-3\nhard.runs=abc\nmedium.runs=7\neasy.bogus=1\n");
    CHECK(loaded.bestFor(Difficulty::Easy).bestTimeSeconds == doctest::Approx(0.0f));
    CHECK(loaded.bestFor(Difficulty::Easy).mostKills == 100000000);
    CHECK(loaded.bestFor(Difficulty::Hard).wins == 0);
    CHECK(loaded.bestFor(Difficulty::Hard).runs == 0);
    CHECK(loaded.bestFor(Difficulty::Normal).runs == 0);
    CHECK(settingsAreSane(loaded));
}

TEST_CASE("the largest possible save still fits in the size limit") {
    SaveData data;
    for (int level = 0; level < kDifficultyCount; ++level) {
        data.best[level].bestTimeSeconds = 86400.0f;
        data.best[level].mostKills = 100000000;
        data.best[level].highestLevel = 100000;
        data.best[level].wins = 100000000;
        data.best[level].runs = 100000000;
    }
    const SaveData loaded = save::parse(save::serialize(data));
    CHECK(loaded.bestFor(Difficulty::Hard).runs == 100000000);
    CHECK(loaded.bestFor(Difficulty::Easy).mostKills == 100000000);
    CHECK(save::serialize(data).size() < 2048);
}

TEST_CASE("the chosen map survives a save, and a bad or missing value is safe") {
    ls::SaveData data;
    data.map = 1;
    CHECK(ls::save::parse(ls::save::serialize(data)).map == 1);
    CHECK(ls::save::parse("version=1\n").map == 0);
    CHECK(ls::save::parse("version=1\nmap=9000\n").map <= 255);
    CHECK(ls::save::parse("version=1\nmap=-3\n").map >= 0);
    CHECK(ls::save::parse("version=1\nmap=abc\n").map == 0);
}
