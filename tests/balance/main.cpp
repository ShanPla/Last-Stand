// ls_balance: plays many seeded games with bots and prints a balance report.
// It is a development tool and is not shipped. Run it with --help for options.

#include "Bot.h"
#include "GameRunner.h"
#include "Parallel.h"
#include "Report.h"
#include "SoloTest.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace ls;
using namespace ls::tools;

namespace {

struct Options {
    int seeds = 30;
    uint64_t firstSeed = 1000;
    int threads = 0;  // 0 means one per hardware thread.
    bool novice = true;
    bool average = true;
    bool skilled = true;
    bool games = true;
    bool solo = true;
    bool focus = false;
    bool listRuns = false;
    long long traceSeed = -1;  // Play one game and print what happens around the player.
    bool correlation = true;
    std::string csvFolder;  // Empty means no CSV.
};

void printHelp() {
    std::printf(
        "ls_balance: bot games and a solo weapon test for tuning Last Stand.\n\n"
        "  --seeds N            games per profile (default 30)\n"
        "  --first-seed S       first seed; seeds run S, S+1, ... (default 1000)\n"
        "  --difficulty D       easy, normal or hard (default normal)\n"
        "  --profile NAME       novice, average, skilled or all (default all)\n"
        "  --threads N          worker threads (default: all cores)\n"
        "  --runs               list every run on one line\n"
        "  --trace SEED         play one game of the chosen profile and print the player's surroundings\n"
        "  --focus              also play Skilled runs built around each weapon in turn\n"
        "  --focus-only         only those focus runs\n"
        "  --solo-only          only the solo weapon test\n"
        "  --no-solo            skip the solo weapon test\n"
        "  --csv [FOLDER]       also write CSV files (default folder build/balance)\n"
        "  --help               this text\n\n"
        "The same seeds and settings always give the same numbers, for any thread count.\n");
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return false;
        } else if (arg == "--seeds" && hasValue) {
            options.seeds = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--difficulty" && hasValue) {
            const std::string name = argv[++i];
            setGameDifficulty(name == "easy" ? Difficulty::Easy : (name == "hard" ? Difficulty::Hard : Difficulty::Normal));
        } else if (arg == "--first-seed" && hasValue) {
            options.firstSeed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--threads" && hasValue) {
            options.threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--profile" && hasValue) {
            const std::string name = argv[++i];
            options.novice = name == "novice" || name == "all";
            options.average = name == "average" || name == "all";
            options.skilled = name == "skilled" || name == "all";
            if (!options.novice && !options.average && !options.skilled) {
                std::fprintf(stderr, "Unknown profile: %s\n", name.c_str());
                return false;
            }
        } else if (arg == "--trace" && hasValue) {
            options.traceSeed = std::atoll(argv[++i]);
        } else if (arg == "--runs") {
            options.listRuns = true;
        } else if (arg == "--focus") {
            options.focus = true;
        } else if (arg == "--focus-only") {
            options.focus = true;
            options.games = false;
            options.solo = false;
        } else if (arg == "--solo-only") {
            options.games = false;
        } else if (arg == "--no-solo") {
            options.solo = false;
        } else if (arg == "--csv") {
            options.csvFolder = (hasValue && argv[i + 1][0] != '-') ? argv[++i] : "build/balance";
        } else {
            std::fprintf(stderr, "Unknown or incomplete option: %s (try --help)\n", arg.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) return 1;
    if (options.threads == 0) options.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };

    if (options.traceSeed >= 0) {
        const BotProfile& profile = options.skilled ? skilled() : (options.average ? average() : novice());
        playGame(profile, static_cast<uint64_t>(options.traceSeed), -1, stdout);
        return 0;
    }

    std::vector<ProfileRuns> groups;
    if (options.games) {
        std::vector<const BotProfile*> profiles;
        if (options.novice) profiles.push_back(&novice());
        if (options.average) profiles.push_back(&average());
        if (options.skilled) profiles.push_back(&skilled());
        for (const BotProfile* profile : profiles) {
            groups.push_back({profile, std::vector<RunResult>(static_cast<size_t>(options.seeds))});
        }

        std::fprintf(stderr, "Playing %zu games on %d threads...\n", groups.size() * static_cast<size_t>(options.seeds), options.threads);
        const size_t perProfile = static_cast<size_t>(options.seeds);
        parallelFor(groups.size() * perProfile, options.threads, [&](size_t job) {
            ProfileRuns& group = groups[job / perProfile];
            const size_t index = job % perProfile;
            group.runs[index] = playGame(*group.profile, options.firstSeed + index);
        });
        std::fprintf(stderr, "Games done after %.0f s.\n", elapsed());

        std::printf("Last Stand balance report: %d seeds per profile, first seed %llu\n", options.seeds,
                    static_cast<unsigned long long>(options.firstSeed));
        printWaveAnalysis(stdout);
        for (const ProfileRuns& group : groups) {
            printProfileReport(stdout, group, options.correlation && group.profile == &skilled());
        }
        printBreatherTable(stdout, groups);
        if (options.listRuns) {
            for (const ProfileRuns& group : groups) printRunList(stdout, group);
        }
        if (!options.csvFolder.empty()) writeCsv(options.csvFolder, groups);
    }

    if (options.focus) {
        const size_t perWeapon = static_cast<size_t>(options.seeds);
        std::vector<std::vector<RunResult>> byWeapon(kWeaponCount, std::vector<RunResult>(perWeapon));
        std::fprintf(stderr, "Playing %zu focus games...\n", perWeapon * kWeaponCount);
        parallelFor(perWeapon * kWeaponCount, options.threads, [&](size_t job) {
            const int weapon = static_cast<int>(job / perWeapon);
            byWeapon[static_cast<size_t>(weapon)][job % perWeapon] =
                playGame(skilled(), options.firstSeed + job % perWeapon, weapon);
        });
        printFocusTable(stdout, skilled(), byWeapon);
        std::fprintf(stderr, "Focus games done after %.0f s.\n", elapsed());
    }

    if (options.solo) {
        std::fprintf(stderr, "Running the solo weapon test...\n");
        runSoloTest(stdout, options.threads, options.csvFolder);
        std::fprintf(stderr, "Solo test done after %.0f s.\n", elapsed());
    }
    return 0;
}
