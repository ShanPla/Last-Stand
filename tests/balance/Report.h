#pragma once

#include "Bot.h"
#include "RunResult.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ls::tools {

// All the finished games of one bot profile.
struct ProfileRuns {
    const BotProfile* profile = nullptr;
    std::vector<RunResult> runs;
};

// Plain text tables for one profile. The win correlation needs a decent number
// of wins and losses, so it is optional.
void printProfileReport(std::FILE* out, const ProfileRuns& group, bool withCorrelation);

// One line per run, for finding the odd ones out.
void printRunList(std::FILE* out, const ProfileRuns& group);

// Rows are weapons: results of runs built around that weapon (index = weapon id).
void printFocusTable(std::FILE* out, const BotProfile& profile, const std::vector<std::vector<RunResult>>& byWeapon);

// One line saying which difficulty the games were played on, and what it changes.
void printDifficultyHeader(std::FILE* out, Difficulty difficulty);

// The wave table by itself: how many zombies and how much HP arrive each second on this difficulty.
void printWaveAnalysis(std::FILE* out, Difficulty difficulty);

// Measured pressure around each breather, next to what the wave table says.
void printBreatherTable(std::FILE* out, const std::vector<ProfileRuns>& groups, Difficulty difficulty);

// One row per run, plus per-weapon and time-series summaries, into the folder. Every file
// starts with a difficulty column, because a later run on another level would overwrite it.
void writeCsv(const std::string& folder, const std::vector<ProfileRuns>& groups, Difficulty difficulty);

}  // namespace ls::tools
