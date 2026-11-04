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
    data.best.bestTimeSeconds = 421.5f;
    data.best.mostKills = 812;
    data.best.highestLevel = 14;
    data.best.wins = 2;
    data.best.runs = 9;
    return data;
}

bool settingsAreSane(const SaveData& data) {
    const Settings& s = data.settings;
    const auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
    return unit(s.musicVolume) && unit(s.effectsVolume) && unit(s.screenShake) && unit(s.flashIntensity) &&
           std::isfinite(data.best.bestTimeSeconds) && data.best.bestTimeSeconds >= 0.0f && data.best.mostKills >= 0 &&
           data.best.highestLevel >= 0 && data.best.wins >= 0 && data.best.runs >= 0;
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
    CHECK(loaded.best.bestTimeSeconds == doctest::Approx(421.5f));
    CHECK(loaded.best.mostKills == 812);
    CHECK(loaded.best.highestLevel == 14);
    CHECK(loaded.best.wins == 2);
    CHECK(loaded.best.runs == 9);
}

TEST_CASE("missing or empty text gives the fallback") {
    SaveData fallback;
    fallback.settings.reducedMotion = true;
    fallback.settings.musicVolume = 0.3f;
    const SaveData fromEmpty = save::parse("", fallback);
    CHECK(fromEmpty.settings.reducedMotion == true);
    CHECK(fromEmpty.settings.musicVolume == doctest::Approx(0.3f));
    CHECK(fromEmpty.best.mostKills == 0);
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
        CHECK(loaded.best.runs == 0);
    }
}

TEST_CASE("partly valid text keeps the good fields and defaults the rest") {
    const SaveData loaded = save::parse("music=0.1\nthis line is junk\neffects=oops\nwins=3\nmostKills=");
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.1f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(SaveData{}.settings.effectsVolume));
    CHECK(loaded.best.wins == 3);
    CHECK(loaded.best.mostKills == 0);
}

TEST_CASE("out of range numbers are clamped") {
    const SaveData loaded = save::parse(
        "music=9000\neffects=-5\nshake=1e300\nflash=2\nbestTime=-1\nmostKills=99999999999999\nhighestLevel=-7\nwins=1e12\nruns=-1\n");
    CHECK(loaded.settings.musicVolume == doctest::Approx(1.0f));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(0.0f));
    CHECK(loaded.settings.screenShake == doctest::Approx(1.0f));
    CHECK(loaded.settings.flashIntensity == doctest::Approx(1.0f));
    CHECK(loaded.best.bestTimeSeconds == doctest::Approx(0.0f));
    CHECK(loaded.best.mostKills > 0);
    CHECK(loaded.best.mostKills <= 100000000);
    CHECK(loaded.best.highestLevel == 0);
    CHECK(loaded.best.wins <= 100000000);
    CHECK(loaded.best.runs == 0);
    CHECK(settingsAreSane(loaded));
}

TEST_CASE("NaN and infinity are rejected") {
    const SaveData loaded = save::parse("music=nan\neffects=inf\nshake=-inf\nflash=NaN\nbestTime=nan\n");
    CHECK(loaded.settings.musicVolume == doctest::Approx(SaveData{}.settings.musicVolume));
    CHECK(loaded.settings.effectsVolume == doctest::Approx(SaveData{}.settings.effectsVolume));
    CHECK(loaded.settings.screenShake == doctest::Approx(SaveData{}.settings.screenShake));
    CHECK(loaded.settings.flashIntensity == doctest::Approx(SaveData{}.settings.flashIntensity));
    CHECK(loaded.best.bestTimeSeconds == doctest::Approx(0.0f));
    CHECK(settingsAreSane(loaded));
}

TEST_CASE("a NaN in the data being saved still produces a loadable file") {
    SaveData data = sample();
    data.settings.musicVolume = std::numeric_limits<float>::quiet_NaN();
    const SaveData loaded = save::parse(save::serialize(data));
    CHECK(settingsAreSane(loaded));
    CHECK(loaded.best.mostKills == 812);
}

TEST_CASE("a newer version with extra keys still loads what it understands") {
    const SaveData loaded = save::parse("version=99\nmusic=0.4\nfutureThing=hello\nanother=1=2\nwins=5\n");
    CHECK(loaded.version == kSaveVersion);
    CHECK(loaded.settings.musicVolume == doctest::Approx(0.4f));
    CHECK(loaded.best.wins == 5);
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
