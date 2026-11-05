#include "components/Audio/Router/Router.h"

#include "data/Balance.h"

#include <algorithm>

namespace ls::audio::router {

using sfx::SfxId;

void Router::reset() {
    throttle_.reset();
    gems_.reset();
}

bool Router::start(SfxId id, SfxId slot, float now, float volume, float pitchVariation, PlayRequest& out) {
    if (!throttle_.allow(slot, now, sfx::info(slot).minInterval)) return false;
    out.id = id;
    out.volume = std::clamp(volume, 0.0f, 1.0f);
    out.pitch = 1.0f + noise_.range(-pitchVariation, pitchVariation);
    return true;
}

bool Router::route(const SimEvent& event, float now, PlayRequest& out) {
    switch (event.type) {
        case EventType::WeaponFired: {
            // Each weapon has its own sound. Combat Blades spin all the time, so they stay silent.
            static constexpr SfxId kFire[static_cast<int>(WeaponId::Count)] = {
                SfxId::FireRifle, SfxId::FireShotgun, SfxId::FireSmg, SfxId::FireSniper, SfxId::FireFlame,
                SfxId::FireGrenade, SfxId::FireRocket, SfxId::FireMine, SfxId::FireTesla, SfxId::Count};
            if (event.detail >= static_cast<uint8_t>(WeaponId::Count)) return false;
            const SfxId id = kFire[event.detail];
            if (id == SfxId::Count) return false;
            // The flame is a steady stream, so it must not wobble in pitch from one start to the next.
            const float variation = id == SfxId::FireFlame ? 0.0f : 0.04f;
            return start(id, id, now, 1.0f, variation, out);
        }
        case EventType::ReloadStarted: return start(SfxId::Reload, SfxId::Reload, now, 1.0f, 0.02f, out);
        case EventType::WeaponDry: return start(SfxId::Dry, SfxId::Dry, now, 1.0f, 0.02f, out);
        case EventType::EnemyHit: {
            // WeaponId::Count means no weapon was behind the hit, such as a bomb. The bomb has its own sound.
            if (event.detail >= static_cast<uint8_t>(WeaponId::Count)) return false;
            const WeaponId weapon = static_cast<WeaponId>(event.detail);
            // Blast damage is credited to the weapon that made it. The blast has its own
            // event and sound, so a hit from one stays quiet.
            if (weapon == WeaponId::GrenadeLauncher || weapon == WeaponId::Landmines || weapon == WeaponId::Rockets) return false;
            return start(SfxId::EnemyHit, SfxId::EnemyHit, now, event.flag ? 1.0f : 0.7f, 0.12f, out);
        }
        case EventType::Explosion: {
            const WeaponId weapon = static_cast<WeaponId>(event.detail);
            if (weapon == WeaponId::Rockets) return start(SfxId::Explosion, SfxId::Explosion, now, 0.6f, 0.08f, out);
            return start(SfxId::Explosion, SfxId::Explosion, now, 0.85f, 0.08f, out);
        }
        case EventType::EnemyKilled: {
            const EnemyType type = static_cast<EnemyType>(event.detail);
            SfxId id = SfxId::KillSquelch;
            float variation = 0.14f;
            if (type == EnemyType::Bloater) id = SfxId::KillPop;
            else if (type == EnemyType::Brute || type == EnemyType::Charger) id = SfxId::KillThud;
            else if (noise_.next01() < 0.2f) id = SfxId::KillThud;
            // All kill sounds share one rate limit.
            if (!throttle_.allow(SfxId::KillSquelch, now, sfx::info(SfxId::KillSquelch).minInterval)) return false;
            out.id = id;
            out.volume = 1.0f;
            out.pitch = (type == EnemyType::Crawler ? 1.25f : 1.0f) + noise_.range(-variation, variation);
            return true;
        }
        case EventType::EliteKilled: return start(SfxId::EliteKilled, SfxId::EliteKilled, now, 1.0f, 0.05f, out);
        case EventType::PlayerHit: return start(SfxId::PlayerHit, SfxId::PlayerHit, now, 0.75f + std::min(0.25f, event.value / 80.0f), 0.03f, out);
        case EventType::PlayerDied: return start(SfxId::PlayerDied, SfxId::PlayerDied, now, 1.0f, 0.0f, out);
        case EventType::ZombieUnstuck: return false;  // Only the balance tool counts these.
        case EventType::CarCrushed: {
            // Louder the closer it is to the player, and silent when it is far out of sight.
            const float loudness = 1.0f - event.value / balance::kCarCrushHeardWithin;
            if (loudness <= 0.0f) return false;
            return start(SfxId::CarCrush, SfxId::CarCrush, now, 0.45f + 0.55f * loudness, 0.07f, out);
        }
        case EventType::PlayerDashed: return start(SfxId::PlayerDash, SfxId::PlayerDash, now, 1.0f, 0.03f, out);
        case EventType::GemCollected: {
            if (!throttle_.allow(SfxId::Gem0, now, sfx::info(SfxId::Gem0).minInterval)) return false;
            out.id = sfx::gemSound(gems_.next(now));
            out.volume = 1.0f;
            out.pitch = 1.0f;
            return true;
        }
        case EventType::LevelUp: return start(SfxId::LevelUp, SfxId::LevelUp, now, 1.0f, 0.0f, out);
        case EventType::PickupCollected: {
            static constexpr SfxId kPickup[static_cast<int>(PickupType::Count)] = {SfxId::PickupHeal, SfxId::PickupMagnet, SfxId::PickupBomb, SfxId::PickupAmmo};
            if (event.detail >= static_cast<uint8_t>(PickupType::Count)) return false;
            const SfxId id = kPickup[event.detail];
            return start(id, id, now, 1.0f, 0.0f, out);
        }
        case EventType::BombBlast: return start(SfxId::BombBlast, SfxId::BombBlast, now, 1.0f, 0.0f, out);
        case EventType::EvolutionGained: return start(SfxId::Evolution, SfxId::Evolution, now, 1.0f, 0.0f, out);
        case EventType::BossSpawned: return start(SfxId::BossSpawned, SfxId::BossSpawned, now, 1.0f, 0.0f, out);
        case EventType::BossPhaseChanged: return start(SfxId::BossPhase, SfxId::BossPhase, now, 1.0f, 0.0f, out);
        case EventType::BossKilled: return start(SfxId::BossKilled, SfxId::BossKilled, now, 1.0f, 0.0f, out);
        case EventType::BossAttack: {
            // One warning sound, pitched apart for each attack, so they can be told apart by ear.
            float pitch = 1.0f;
            switch (static_cast<balance::BossAttack>(event.detail)) {
                case balance::BossAttack::Cross: pitch = 1.0f; break;
                case balance::BossAttack::Trail: pitch = 1.25f; break;
                case balance::BossAttack::Spiral: pitch = 0.8f; break;
                default: return false;  // The older attacks have no sound of their own.
            }
            if (!start(SfxId::BossWarn, SfxId::BossWarn, now, 1.0f, 0.0f, out)) return false;
            out.pitch = pitch;
            return true;
        }
    }
    return false;
}

bool Router::routeUi(UiSound sound, float now, PlayRequest& out) {
    static constexpr SfxId kUi[] = {SfxId::UiMove, SfxId::UiConfirm, SfxId::UiBack, SfxId::UiCardPick, SfxId::UiReroll, SfxId::UiPause};
    const int index = static_cast<int>(sound);
    if (index < 0 || index >= static_cast<int>(sizeof(kUi) / sizeof(kUi[0]))) return false;
    return start(kUi[index], kUi[index], now, 1.0f, 0.0f, out);
}

}  // namespace ls::audio::router
