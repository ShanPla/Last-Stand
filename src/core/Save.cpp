#include "core/Save.h"

#include "components/Net/Net.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ls::save {

namespace {

// Anything bigger than this is not a save file this game wrote.
constexpr size_t kMaxSaveBytes = 4096;

constexpr float kMaxTimeSeconds = 86400.0f;
constexpr int kMaxCount = 100000000;
constexpr int kMaxLevel = 100000;

// serialize() lists the levels in this order, and Normal is the one with no prefix.
static_assert(static_cast<int>(Difficulty::Easy) == 0 && static_cast<int>(Difficulty::Normal) == 1 && static_cast<int>(Difficulty::Hard) == 2 &&
                  static_cast<int>(Difficulty::Hell) == 3,
              "the save writes one best run per level in enum order");

constexpr std::string_view kEasyPrefix = "easy.";
constexpr std::string_view kHardPrefix = "hard.";
constexpr std::string_view kHellPrefix = "hell.";
// Endless best runs put this in front of the level prefix, so "endless.hard.bestTime". An
// older save has no such keys, and its runs all stay Story's.
constexpr std::string_view kEndlessPrefix = "endless.";

// Parses a whole token as a finite number. Rejects NaN, infinity, and trailing junk.
bool parseNumber(std::string_view token, double* out) {
    if (token.empty() || token.size() > 32) return false;
    char buffer[40];
    for (size_t i = 0; i < token.size(); ++i) buffer[i] = token[i];
    buffer[token.size()] = '\0';
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(buffer, &end);
    if (end == buffer || *end != '\0') return false;
    if (!std::isfinite(value)) return false;
    *out = value;
    return true;
}

// A bad value keeps what was already in the field, a good one is clamped.
void readFloat(std::string_view token, float low, float high, float* field) {
    double value = 0.0;
    if (!parseNumber(token, &value)) return;
    if (value < static_cast<double>(low)) value = static_cast<double>(low);
    if (value > static_cast<double>(high)) value = static_cast<double>(high);
    *field = static_cast<float>(value);
}

void readInt(std::string_view token, int low, int high, int* field) {
    double value = 0.0;
    if (!parseNumber(token, &value)) return;
    if (value < static_cast<double>(low)) value = static_cast<double>(low);
    if (value > static_cast<double>(high)) value = static_cast<double>(high);
    *field = static_cast<int>(value);
}

void readBool(std::string_view token, bool* field) {
    if (token == "1") *field = true;
    else if (token == "0") *field = false;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// Best-run keys, without their level prefix.
void applyBestKey(BestRun& best, std::string_view key, std::string_view value) {
    if (key == "bestTime") readFloat(value, 0.0f, kMaxTimeSeconds, &best.bestTimeSeconds);
    else if (key == "mostKills") readInt(value, 0, kMaxCount, &best.mostKills);
    else if (key == "highestLevel") readInt(value, 0, kMaxLevel, &best.highestLevel);
    else if (key == "wins") readInt(value, 0, kMaxCount, &best.wins);
    else if (key == "runs") readInt(value, 0, kMaxCount, &best.runs);
    else if (key == "bosses") readInt(value, 0, kMaxCount, &best.mostBosses);
}

bool startsWith(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }

// A best-run key into one table of levels. Normal keeps the plain names an older save
// already uses. The other levels add a prefix.
void applyLevelKey(BestRun* table, std::string_view key, std::string_view value) {
    const auto at = [table](Difficulty level) -> BestRun& { return table[static_cast<int>(level)]; };
    if (startsWith(key, kEasyPrefix)) applyBestKey(at(Difficulty::Easy), key.substr(kEasyPrefix.size()), value);
    else if (startsWith(key, kHellPrefix)) applyBestKey(at(Difficulty::Hell), key.substr(kHellPrefix.size()), value);
    else if (startsWith(key, kHardPrefix)) applyBestKey(at(Difficulty::Hard), key.substr(kHardPrefix.size()), value);
    else applyBestKey(at(Difficulty::Normal), key, value);
}

void applyKey(SaveData& data, std::string_view key, std::string_view value) {
    if (key == "music") readFloat(value, 0.0f, 1.0f, &data.settings.musicVolume);
    else if (key == "effects") readFloat(value, 0.0f, 1.0f, &data.settings.effectsVolume);
    else if (key == "shake") readFloat(value, 0.0f, 1.0f, &data.settings.screenShake);
    else if (key == "flash") readFloat(value, 0.0f, 1.0f, &data.settings.flashIntensity);
    else if (key == "damageNumbers") readBool(value, &data.settings.damageNumbers);
    else if (key == "reducedMotion") readBool(value, &data.settings.reducedMotion);
    else if (key == "difficulty") {
        int level = static_cast<int>(data.difficulty);
        readInt(value, 0, kDifficultyCount - 1, &level);
        data.difficulty = static_cast<Difficulty>(level);
    }
    // The map list can grow, so only the range of a byte is checked here. The app turns
    // the number into a real map, with an unknown one falling back to the default.
    else if (key == "map") readInt(value, 0, 255, &data.map);
    else if (key == "mode") {
        int mode = static_cast<int>(data.mode);
        readInt(value, 0, static_cast<int>(RunMode::Count) - 1, &mode);
        data.mode = static_cast<RunMode>(mode);
    }
    else if (key == "coopName") {
        // Only a name the server would accept is kept, so a damaged value never reaches a button.
        if (net::validName(std::string(value).c_str())) std::snprintf(data.coopName, sizeof(data.coopName), "%s", std::string(value).c_str());
    }
    else if (startsWith(key, kEndlessPrefix)) applyLevelKey(data.endlessBest, key.substr(kEndlessPrefix.size()), value);
    else applyLevelKey(data.best, key, value);
    // Unknown keys are ignored, so a save from a newer version still loads.
}

}  // namespace

std::string serialize(const SaveData& data) {
    char buffer[2048];
    int n = std::snprintf(buffer, sizeof(buffer),
                          "version=%d\nmusic=%.3f\neffects=%.3f\nshake=%.3f\nflash=%.3f\ndamageNumbers=%d\nreducedMotion=%d\ndifficulty=%d\nmap=%d\nmode=%d\ncoopName=%s\n",
                          kSaveVersion, static_cast<double>(data.settings.musicVolume), static_cast<double>(data.settings.effectsVolume),
                          static_cast<double>(data.settings.screenShake), static_cast<double>(data.settings.flashIntensity),
                          data.settings.damageNumbers ? 1 : 0, data.settings.reducedMotion ? 1 : 0, static_cast<int>(data.difficulty), data.map,
                          static_cast<int>(data.mode), data.coopName);
    const char* const prefixes[kDifficultyCount] = {"easy.", "", "hard.", "hell."};
    for (int level = 0; level < kDifficultyCount && n > 0 && n < static_cast<int>(sizeof(buffer)); ++level) {
        const BestRun& best = data.best[level];
        const char* p = prefixes[level];
        n += std::snprintf(buffer + n, sizeof(buffer) - static_cast<size_t>(n),
                           "%sbestTime=%.2f\n%smostKills=%d\n%shighestLevel=%d\n%swins=%d\n%sruns=%d\n", p, static_cast<double>(best.bestTimeSeconds), p,
                           best.mostKills, p, best.highestLevel, p, best.wins, p, best.runs);
    }
    // Endless has no wins, so it stores the bosses killed instead.
    for (int level = 0; level < kDifficultyCount && n > 0 && n < static_cast<int>(sizeof(buffer)); ++level) {
        const BestRun& best = data.endlessBest[level];
        const char* p = prefixes[level];
        n += std::snprintf(buffer + n, sizeof(buffer) - static_cast<size_t>(n),
                           "endless.%sbestTime=%.2f\nendless.%smostKills=%d\nendless.%shighestLevel=%d\nendless.%sbosses=%d\nendless.%sruns=%d\n", p,
                           static_cast<double>(best.bestTimeSeconds), p, best.mostKills, p, best.highestLevel, p, best.mostBosses, p, best.runs);
    }
    return std::string(buffer);
}

SaveData parse(std::string_view text, const SaveData& fallback) {
    SaveData data = fallback;
    data.version = kSaveVersion;
    if (text.empty() || text.size() > kMaxSaveBytes) return data;

    while (!text.empty()) {
        const size_t lineEnd = text.find('\n');
        const std::string_view line = trim(text.substr(0, lineEnd));
        text = lineEnd == std::string_view::npos ? std::string_view{} : text.substr(lineEnd + 1);

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        // The version line is read for completeness only. Every version this game has
        // shipped uses the same keys, and unknown ones are skipped, so all load alike.
        if (key == "version") continue;
        applyKey(data, key, value);
    }
    return data;
}

void mergeBestRuns(SaveData& mine, const SaveData& stored) {
    const auto merge = [](BestRun& best, const BestRun& other) {
        best.bestTimeSeconds = std::max(best.bestTimeSeconds, other.bestTimeSeconds);
        best.mostKills = std::max(best.mostKills, other.mostKills);
        best.highestLevel = std::max(best.highestLevel, other.highestLevel);
        best.wins = std::max(best.wins, other.wins);
        best.runs = std::max(best.runs, other.runs);
        best.mostBosses = std::max(best.mostBosses, other.mostBosses);
    };
    for (int level = 0; level < kDifficultyCount; ++level) {
        merge(mine.best[level], stored.best[level]);
        merge(mine.endlessBest[level], stored.endlessBest[level]);
    }
}

NewBests recordRun(SaveData& data, const FinishedRun& run) {
    BestRun& best = data.bestFor(run.mode, run.difficulty);
    const bool endless = run.mode == RunMode::Endless;
    NewBests beaten;
    beaten.time = run.timeSeconds > best.bestTimeSeconds;
    beaten.kills = !endless && run.kills > best.mostKills;
    beaten.level = !endless && run.level > best.highestLevel;
    best.bestTimeSeconds = std::max(best.bestTimeSeconds, run.timeSeconds);
    best.mostKills = std::max(best.mostKills, run.kills);
    best.highestLevel = std::max(best.highestLevel, run.level);
    best.mostBosses = std::max(best.mostBosses, run.bossesKilled);
    if (run.won) best.wins++;
    best.runs++;
    return beaten;
}

SaveData& current() {
    static SaveData data;
    return data;
}

}  // namespace ls::save
