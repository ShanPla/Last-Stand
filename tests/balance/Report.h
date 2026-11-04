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

// The wave table by itself: how many zombies and how much HP arrive each second.
void printWaveAnalysis(std::FILE* out);

// Measured pressure around each breather, next to what the wave table says.
void printBreatherTable(std::FILE* out, const std::vector<ProfileRuns>& groups);

// One row per run, plus per-weapon and time-series summaries, into the folder.
void writeCsv(const std::string& folder, const std::vector<ProfileRuns>& groups);

}  // namespace ls::tools
