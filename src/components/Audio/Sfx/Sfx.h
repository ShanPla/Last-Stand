#pragma once

#include "components/Audio/Synth/Synth.h"

#include <cstdint>

// Every sound effect in the game, built from the synth pieces. Each one is
// rendered once at startup into a mono buffer, then played back many times.
namespace ls::audio::sfx {

inline constexpr int kGemSteps = 10;  // Gem pickup is rendered at ten rising pitches.

enum class SfxId : uint8_t {
    FireRifle, FireShotgun, FireSmg, FireSniper, FireFlame, FireGrenade, FireRocket, FireMine, FireTesla,
    Reload, Dry,
    EnemyHit, KillSquelch, KillThud, KillPop, EliteKilled,
    PlayerHit, PlayerDied, PlayerDash,
    Gem0, Gem1, Gem2, Gem3, Gem4, Gem5, Gem6, Gem7, Gem8, Gem9,
    LevelUp, PickupHeal, PickupMagnet, PickupBomb, PickupAmmo,
    BombBlast, Explosion, Evolution, BossSpawned, BossPhase, BossKilled,
    UiMove, UiConfirm, UiBack, UiCardPick, UiReroll, UiPause,
    Count
};
inline constexpr int kSfxCount = static_cast<int>(SfxId::Count);

struct SfxInfo {
    const char* name;
    int voices;          // How many copies may play at once.
    float minInterval;   // Seconds between starts of this sound.
};
const SfxInfo& info(SfxId id);

inline SfxId gemSound(int step) {
    if (step < 0) step = 0;
    if (step >= kGemSteps) step = kGemSteps - 1;
    return static_cast<SfxId>(static_cast<int>(SfxId::Gem0) + step);
}

// Builds one sound. Same result every call.
synth::Samples renderSfx(SfxId id);

struct SfxBank {
    synth::Samples sounds[kSfxCount];
};
void renderAll(SfxBank& bank);

}  // namespace ls::audio::sfx
