#define _CRT_SECURE_NO_WARNINGS
#include "doctest.h"

#include "components/Audio/Layers/Layers.h"
#include "components/Audio/Music/Music.h"
#include "components/Audio/Router/Router.h"
#include "components/Audio/Sfx/Sfx.h"
#include "components/Audio/Synth/Synth.h"
#include "core/Sim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace ls;
using namespace ls::audio;

namespace {

bool allFinite(const synth::Samples& s) {
    for (float v : s)
        if (!std::isfinite(v)) return false;
    return true;
}

// The big renders are shared between tests, so the suite stays quick.
const sfx::SfxBank& sfxBank() {
    static sfx::SfxBank bank = [] {
        sfx::SfxBank b;
        sfx::renderAll(b);
        return b;
    }();
    return bank;
}

const music::Bank& musicBank() {
    static music::Bank bank = [] {
        music::Bank b;
        music::renderAll(b);
        return b;
    }();
    return bank;
}

SimEvent makeEvent(EventType type, uint8_t detail = 0, bool flag = false, float value = 0.0f) {
    SimEvent e;
    e.type = type;
    e.detail = detail;
    e.flag = flag;
    e.value = value;
    return e;
}

void writeWav(const std::string& path, const std::vector<int16_t>& samples, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t riff = 36 + dataBytes, fmtSize = 16, byteRate = static_cast<uint32_t>(rate) * 2, sampleRate = static_cast<uint32_t>(rate);
    const uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtSize, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&sampleRate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    std::fwrite(samples.data(), 2, samples.size(), f);
    std::fclose(f);
}

std::vector<int16_t> toPcm(const synth::Samples& s) {
    std::vector<int16_t> out(s.size());
    for (size_t i = 0; i < s.size(); ++i) out[i] = static_cast<int16_t>(std::lround(std::clamp(s[i], -1.0f, 1.0f) * 32767.0f));
    return out;
}

float db(float linear) { return 20.0f * std::log10(std::max(linear, 1e-6f)); }

// Mixes `blocks` blocks with the given targets, for listening files and level checks.
std::vector<int16_t> mixSeconds(float seconds, float intensity, bool boss, bool ramp = false) {
    layers::StemMixer mixer;
    mixer.setBank(&musicBank());
    mixer.setTargets(layers::targetsFor(intensity, boss));
    mixer.snapToTargets();
    const int total = static_cast<int>(seconds * static_cast<float>(music::kSampleRate));
    std::vector<int16_t> out(static_cast<size_t>(total));
    for (int done = 0; done < total; done += layers::StemMixer::kMaxBlock) {
        const int n = std::min(layers::StemMixer::kMaxBlock, total - done);
        if (ramp) {
            const float u = static_cast<float>(done) / static_cast<float>(total);
            mixer.setTargets(layers::targetsFor(u / 0.85f, u > 0.85f));
        }
        mixer.mix(out.data() + done, n);
    }
    return out;
}

}  // namespace

TEST_CASE("noise and the limiter behave") {
    synth::Noise a(5), b(5);
    for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next());
    synth::Noise c(9);
    for (int i = 0; i < 10000; ++i) {
        const float v = c.next();
        CHECK(v >= -1.0f);
        CHECK(v < 1.0f);
    }

    // A loud burst of overlapping sound must come out under the threshold.
    synth::Limiter limiter(0.9f, 0.1f, 48000);
    std::vector<float> loud(2 * 4800);
    for (size_t i = 0; i < loud.size(); ++i) loud[i] = 3.0f * std::sin(0.05f * static_cast<float>(i));
    limiter.process(loud.data(), static_cast<unsigned>(loud.size() / 2), 2);
    float peak = 0.0f;
    for (float v : loud) peak = std::max(peak, std::fabs(v));
    CHECK(peak <= 0.9f + 1e-5f);
    // Quiet sound is left alone once the gain has recovered.
    std::vector<float> quiet(2 * 48000, 0.1f);
    limiter.process(quiet.data(), static_cast<unsigned>(quiet.size() / 2), 2);
    CHECK(quiet.back() == doctest::Approx(0.1f).epsilon(0.01));
}

TEST_CASE("every sound effect is clean, audible and ends in silence") {
    const sfx::SfxBank& bank = sfxBank();
    for (int i = 0; i < sfx::kSfxCount; ++i) {
        const synth::Samples& s = bank.sounds[i];
        const std::string name = sfx::info(static_cast<sfx::SfxId>(i)).name;
        INFO(name);
        REQUIRE(!s.empty());
        CHECK(allFinite(s));
        const synth::Stats st = synth::measure(s);
        CHECK(st.seconds >= 0.05f);
        CHECK(st.seconds <= 5.0f);
        CHECK(st.peak <= 1.0f);
        CHECK(st.peak >= 0.1f);    // Not silent.
        CHECK(st.rms >= 0.01f);
        CHECK(std::fabs(st.dc) < 0.01f);
        // Starts and ends at (nearly) zero, so nothing clicks.
        CHECK(std::fabs(s.front()) < 0.01f);
        CHECK(std::fabs(s.back()) < 0.005f);
        for (size_t k = s.size() - 5; k < s.size(); ++k) CHECK(std::fabs(s[k]) < 0.01f);
    }
}

TEST_CASE("rendering a sound twice gives identical samples") {
    for (int i = 0; i < sfx::kSfxCount; ++i) {
        const synth::Samples again = sfx::renderSfx(static_cast<sfx::SfxId>(i));
        CHECK(again == sfxBank().sounds[i]);
    }
}

TEST_CASE("gem sounds rise in pitch") {
    // Count zero crossings: more per second means a higher pitch.
    float last = 0.0f;
    for (int step = 0; step < sfx::kGemSteps; ++step) {
        const synth::Samples& s = sfxBank().sounds[static_cast<int>(sfx::gemSound(step))];
        int crossings = 0;
        const size_t n = std::min<size_t>(s.size(), 2000);
        for (size_t i = 1; i < n; ++i)
            if ((s[i - 1] < 0.0f) != (s[i] < 0.0f)) ++crossings;
        CHECK(static_cast<float>(crossings) > last);
        last = static_cast<float>(crossings);
    }
}

TEST_CASE("the weapons all sound different") {
    const sfx::SfxId fire[] = {sfx::SfxId::FireRifle, sfx::SfxId::FireShotgun, sfx::SfxId::FireSmg, sfx::SfxId::FireSniper, sfx::SfxId::FireFlame,
                               sfx::SfxId::FireGrenade, sfx::SfxId::FireRocket, sfx::SfxId::FireMine, sfx::SfxId::FireTesla};
    for (size_t a = 0; a < 9; ++a)
        for (size_t b = a + 1; b < 9; ++b) CHECK(sfxBank().sounds[static_cast<int>(fire[a])] != sfxBank().sounds[static_cast<int>(fire[b])]);
}

TEST_CASE("music renders in small steps and is the same every time") {
    music::Bank first;
    music::Renderer renderer(first);
    int steps = 0;
    while (renderer.step()) ++steps;
    ++steps;  // The last call returned false but still did work.
    CHECK(steps == music::Renderer::stepsTotal());
    CHECK(renderer.done());
    for (int s = 0; s < music::kStemCount; ++s) CHECK(first.stems[s] == musicBank().stems[s]);
}

TEST_CASE("each music layer is full length, audible and not clipped") {
    const music::Bank& bank = musicBank();
    CHECK(music::kMainBars * 2 >= 60);  // The loop lasts over 60 seconds.
    for (int s = 0; s < music::kStemCount; ++s) {
        INFO(music::stemName(s));
        const std::vector<int16_t>& data = bank.stems[s];
        REQUIRE(data.size() == static_cast<size_t>(music::stemFrames(s)));
        double sum = 0.0, sq = 0.0;
        int peak = 0;
        for (int16_t v : data) {
            sum += v;
            sq += static_cast<double>(v) * v;
            peak = std::max(peak, std::abs(static_cast<int>(v)));
        }
        const double n = static_cast<double>(data.size());
        CHECK(peak < 32767);
        CHECK(std::sqrt(sq / n) / 32768.0 > 0.03);
        CHECK(std::fabs(sum / n) / 32768.0 < 0.005);
    }
}

TEST_CASE("each music layer loops without a jump") {
    for (int s = 0; s < music::kStemCount; ++s) {
        INFO(music::stemName(s));
        const std::vector<int16_t>& data = musicBank().stems[s];
        int maxStep = 0;
        for (size_t i = 1; i < data.size(); ++i) maxStep = std::max(maxStep, std::abs(static_cast<int>(data[i]) - data[i - 1]));
        const int seam = std::abs(static_cast<int>(data.front()) - data.back());
        // The join is no bigger a step than any other place in the loop.
        CHECK(seam <= std::max(maxStep, 400));
    }
}

TEST_CASE("layers are added as intensity rises, and the boss swaps them") {
    auto active = [](const layers::Targets& t, int from, int to) {
        int n = 0;
        for (int s = from; s < to; ++s)
            if (t.gain[s] > 0.5f) ++n;
        return n;
    };
    int previous = 0;
    for (int i = 0; i <= 100; ++i) {
        const layers::Targets t = layers::targetsFor(static_cast<float>(i) / 100.0f, false);
        const int now = active(t, 0, music::kBossBass);
        CHECK(now >= previous);
        previous = now;
        for (int s = music::kBossBass; s < music::kStemCount; ++s) CHECK(t.gain[s] == 0.0f);
        for (int s = 0; s < music::kStemCount; ++s) CHECK((t.gain[s] >= 0.0f && t.gain[s] <= 1.0f));
    }
    const layers::Targets calm = layers::targetsFor(0.0f, false);
    const layers::Targets full = layers::targetsFor(1.0f, false);
    CHECK(active(calm, 0, music::kBossBass) == 2);   // Pad and bass.
    CHECK(active(full, 0, music::kBossBass) == 6);   // Everything.
    CHECK(active(layers::targetsFor(0.4f, false), 0, music::kBossBass) == 4);  // Pad, bass, drums, arp.
    const layers::Targets boss = layers::targetsFor(0.2f, true);
    CHECK(active(boss, 0, music::kBossBass) == 0);
    CHECK(active(boss, music::kBossBass, music::kStemCount) == 4);
}

TEST_CASE("the music mix never clips, and layers fade without jumps") {
    for (int pass = 0; pass < 3; ++pass) {
        const std::vector<int16_t> mix = mixSeconds(20.0f, pass == 0 ? 0.0f : 1.0f, pass == 2);
        int peak = 0;
        for (int16_t v : mix) peak = std::max(peak, std::abs(static_cast<int>(v)));
        CHECK(peak < 32767);
        CHECK(peak > 3000);
    }
    layers::StemMixer mixer;
    mixer.setBank(&musicBank());
    mixer.setTargets(layers::targetsFor(0.0f, false));
    mixer.snapToTargets();
    std::vector<int16_t> block(layers::StemMixer::kMaxBlock);
    mixer.mix(block.data(), static_cast<int>(block.size()));
    mixer.setTargets(layers::targetsFor(1.0f, false));
    float before = mixer.gain(music::kLead);
    int blocks = 0;
    while (mixer.gain(music::kLead) < 1.0f && blocks < 100) {
        mixer.mix(block.data(), static_cast<int>(block.size()));
        const float after = mixer.gain(music::kLead);
        CHECK(after >= before);
        CHECK(after - before <= 0.6f * 4096.0f / 22050.0f + 1e-4f);  // Never faster than the fade rate.
        before = after;
        ++blocks;
    }
    CHECK(blocks > 5);   // A fade takes a while.
    CHECK(blocks < 30);  // But finishes within a few seconds.
}

TEST_CASE("the mixer plays on across the end of the loop") {
    layers::StemMixer mixer;
    mixer.setBank(&musicBank());
    mixer.setTargets(layers::targetsFor(1.0f, false));
    mixer.snapToTargets();
    const size_t length = static_cast<size_t>(music::stemFrames(music::kPad));
    mixer.setPosition(length - 2048);
    std::vector<int16_t> block(4096);
    mixer.mix(block.data(), 4096);
    CHECK(mixer.position() == 2048);
    // The biggest jump between neighbours anywhere except the join. A click at the join
    // has to be bigger than the music's own jumps. (The join must be left out of this
    // search, or its own jump would count as normal and the check could never fail.)
    const int joinStep = std::abs(static_cast<int>(block[2048]) - block[2047]);
    int maxStep = 0;
    for (size_t i = 1; i < block.size(); ++i) {
        if (i == 2048) continue;
        maxStep = std::max(maxStep, std::abs(static_cast<int>(block[i]) - block[i - 1]));
    }
    CHECK(joinStep <= std::max(maxStep, 600));

    // After the wrap the sound must be what the loop sounds like from its start.
    layers::StemMixer fresh;
    fresh.setBank(&musicBank());
    fresh.setTargets(layers::targetsFor(1.0f, false));
    fresh.snapToTargets();
    std::vector<int16_t> start(2048);
    fresh.mix(start.data(), 2048);
    int different = 0;
    for (int i = 0; i < 2048; ++i) {
        if (std::abs(static_cast<int>(block[2048 + i]) - start[i]) > 2) ++different;
    }
    CHECK(different == 0);
}

TEST_CASE("the router limits rates and picks the right sound") {
    router::Router r;
    router::PlayRequest req;
    // Thousands of kill events in one second only produce a few sounds.
    int played = 0;
    for (int i = 0; i < 5000; ++i) {
        const float now = static_cast<float>(i) * 0.0002f;
        if (r.route(makeEvent(EventType::EnemyKilled, static_cast<uint8_t>(EnemyType::Walker)), now, req)) ++played;
    }
    CHECK(played >= 15);
    CHECK(played <= 22);  // About one every 50 ms.

    router::Router r2;
    played = 0;
    for (int i = 0; i < 5000; ++i)
        if (r2.route(makeEvent(EventType::EnemyHit, static_cast<uint8_t>(WeaponId::Smg)), static_cast<float>(i) * 0.0002f, req)) ++played;
    CHECK(played <= 13);

    router::Router r3;
    CHECK(r3.route(makeEvent(EventType::WeaponFired, static_cast<uint8_t>(WeaponId::Rifle)), 1.0f, req));
    CHECK(req.id == sfx::SfxId::FireRifle);
    CHECK_FALSE(r3.route(makeEvent(EventType::WeaponFired, static_cast<uint8_t>(WeaponId::Blades)), 2.0f, req));
    CHECK_FALSE(r3.route(makeEvent(EventType::WeaponFired, 200), 3.0f, req));
    // A bomb hit is WeaponId::Count. It must be safe and silent.
    CHECK_FALSE(r3.route(makeEvent(EventType::EnemyHit, static_cast<uint8_t>(WeaponId::Count)), 4.0f, req));
    CHECK_FALSE(r3.route(makeEvent(EventType::EnemyHit, 255), 4.0f, req));
    // A blast has its own event, so that one explosion isn't one sound per zombie hit.
    CHECK_FALSE(r3.route(makeEvent(EventType::EnemyHit, static_cast<uint8_t>(WeaponId::Rockets)), 5.0f, req));
    CHECK(r3.route(makeEvent(EventType::Explosion, static_cast<uint8_t>(WeaponId::Landmines)), 5.0f, req));
    CHECK(req.id == sfx::SfxId::Explosion);
    CHECK(r3.route(makeEvent(EventType::ReloadStarted, static_cast<uint8_t>(WeaponId::Rifle)), 6.0f, req));
    CHECK(req.id == sfx::SfxId::Reload);
    CHECK(r3.route(makeEvent(EventType::WeaponDry, 0), 7.0f, req));
    CHECK(req.id == sfx::SfxId::Dry);
    CHECK(r3.route(makeEvent(EventType::PlayerDashed, 0), 8.0f, req));
    CHECK(req.id == sfx::SfxId::PlayerDash);

    // Each pickup type has its own sound, and a bad type is ignored.
    sfx::SfxId seen[4];
    for (int p = 0; p < 4; ++p) {
        CHECK(r3.route(makeEvent(EventType::PickupCollected, static_cast<uint8_t>(p)), 10.0f + static_cast<float>(p), req));
        seen[p] = req.id;
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) CHECK(seen[a] != seen[b]);
    CHECK_FALSE(r3.route(makeEvent(EventType::PickupCollected, 99), 20.0f, req));

    // Every event type with every detail value from 0 to 255 must not crash and not return a
    // bad id. Event types go up to 255 too, so a type added later is covered without
    // changing this loop, and a type that does not exist must be refused. Each call is 10
    // seconds after the last, so the rate limit never hides a bad answer.
    router::Router r4;
    float clock = 100.0f;
    int answered = 0;
    for (int type = 0; type < 256; ++type) {
        for (int detail = 0; detail < 256; ++detail) {
            const EventType eventType = static_cast<EventType>(type);
            clock += 10.0f;
            req = router::PlayRequest{};
            const bool accepted = r4.route(makeEvent(eventType, static_cast<uint8_t>(detail)), clock, req);
            CAPTURE(type);
            CAPTURE(detail);
            if (type > static_cast<int>(EventType::PlayerDashed)) CHECK_FALSE(accepted);
            // These three look the sound up in a table by detail, so a detail past the table is refused.
            if (eventType == EventType::WeaponFired && detail >= static_cast<int>(WeaponId::Count)) CHECK_FALSE(accepted);
            if (eventType == EventType::EnemyHit && detail >= static_cast<int>(WeaponId::Count)) CHECK_FALSE(accepted);
            if (eventType == EventType::PickupCollected && detail >= static_cast<int>(PickupType::Count)) CHECK_FALSE(accepted);
            if (accepted) {
                ++answered;
                CHECK(static_cast<int>(req.id) >= 0);
                CHECK(static_cast<int>(req.id) < sfx::kSfxCount);
                CHECK(req.volume >= 0.0f);
                CHECK(req.volume <= 1.0f);
                CHECK(req.pitch > 0.5f);
                CHECK(req.pitch < 2.0f);
            }
        }
    }
    CHECK(answered > 1000);  // The loop really got sounds back to check.

    // Every ui sound works.
    router::Router r5;
    for (int u = 0; u < 6; ++u) CHECK(r5.routeUi(static_cast<UiSound>(u), static_cast<float>(u), req));
    CHECK_FALSE(r5.routeUi(static_cast<UiSound>(40), 50.0f, req));
}

TEST_CASE("gem pitch rises while gems come quickly and resets after a gap") {
    router::Router r;
    router::PlayRequest req;
    const SimEvent gem = makeEvent(EventType::GemCollected, 0, false, 1.0f);
    int previous = -1;
    float now = 10.0f;
    for (int i = 0; i < sfx::kGemSteps; ++i) {
        REQUIRE(r.route(gem, now, req));
        const int step = static_cast<int>(req.id) - static_cast<int>(sfx::SfxId::Gem0);
        CHECK(step == i);
        CHECK(step > previous);
        previous = step;
        now += 0.1f;
    }
    // It stays at the top rather than going past the last step.
    for (int i = 0; i < 5; ++i) {
        REQUIRE(r.route(gem, now, req));
        CHECK(static_cast<int>(req.id) - static_cast<int>(sfx::SfxId::Gem0) == sfx::kGemSteps - 1);
        now += 0.1f;
    }
    // Two gems in the same instant: the second is skipped, not played.
    CHECK_FALSE(r.route(gem, now - 0.1f + 0.005f, req));
    // After a pause the tune starts again from the bottom.
    now += 0.8f;
    REQUIRE(r.route(gem, now, req));
    CHECK(req.id == sfx::SfxId::Gem0);
    now += 0.1f;
    REQUIRE(r.route(gem, now, req));
    CHECK(req.id == sfx::SfxId::Gem1);
}

TEST_CASE("the router has its own random numbers, and routing sounds does not change the game") {
    // Its pitch variation comes from its own seed: the same seed gives the same sounds,
    // and another seed gives different ones.
    const auto pitches = [](uint32_t seed) {
        router::Router r(seed);
        router::PlayRequest req;
        std::vector<float> out;
        for (int i = 0; i < 40; ++i) {
            if (r.route(makeEvent(EventType::EnemyHit, static_cast<uint8_t>(WeaponId::Smg)), static_cast<float>(i), req)) out.push_back(req.pitch);
        }
        return out;
    };
    const std::vector<float> first = pitches(77);
    CHECK(first.size() == 40);
    CHECK(first == pitches(77));
    CHECK(first != pitches(78));

    // Two games with the same seed and a player with huge health, so neither dies and no
    // health has to be topped up (a top-up would hide a router that lowers health). One
    // game sends every event to a router, as the real game does. If routing touched the
    // game's state or random numbers, they would part. The played game is also compared
    // before and after its own routing, so the change shows at once.
    auto played = std::make_unique<GameState>();
    auto silent = std::make_unique<GameState>();
    for (GameState* game : {played.get(), silent.get()}) {
        sim::newRun(*game, 77);
        game->player.maxHp = 5000.0f;
        game->player.hp = 5000.0f;
    }
    router::Router r;
    router::PlayRequest req;
    int routed = 0;
    int changedByRouting = -1;
    int partedAt = -1;
    for (int i = 0; i < 60 * 30; ++i) {
        sim::step(*played, InputFrame{});
        sim::step(*silent, InputFrame{});
        const uint64_t hashBefore = sim::stateHash(*played);
        const uint64_t rngBefore = played->rng.state;
        for (const SimEvent& event : played->events) {
            if (r.route(event, static_cast<float>(i) / 60.0f, req)) ++routed;
        }
        if (changedByRouting < 0 && (sim::stateHash(*played) != hashBefore || played->rng.state != rngBefore)) changedByRouting = i;
        if (partedAt < 0 && sim::stateHash(*played) != sim::stateHash(*silent)) partedAt = i;
        played->events.clear();
        silent->events.clear();
    }
    CHECK(routed > 20);
    CHECK_MESSAGE(changedByRouting == -1, "routing changed the game at step " << changedByRouting);
    CHECK_MESSAGE(partedAt == -1, "the routed game parted from the silent one at step " << partedAt);
    CHECK(played->rng.state == silent->rng.state);
    CHECK(played->player.hp == silent->player.hp);
    CHECK(sim::stateHash(*played) == sim::stateHash(*silent));
}

TEST_CASE("the router's answers depend only on its own seed and the events, never on the game") {
    // Record the events of one game. Then play them through three routers with the same
    // seed: one with no game around it, and two while other games run, with other seeds.
    // A router that read the game's random numbers or state to pick a pitch would answer
    // differently when the game around it is different.
    struct Recorded {
        int step;
        SimEvent event;
    };
    std::vector<Recorded> recorded;
    {
        auto game = std::make_unique<GameState>();
        sim::newRun(*game, 77);
        game->player.maxHp = 5000.0f;
        game->player.hp = 5000.0f;
        for (int i = 0; i < 60 * 30; ++i) {
            sim::step(*game, InputFrame{});
            for (const SimEvent& event : game->events) recorded.push_back(Recorded{i, event});
            game->events.clear();
        }
    }
    REQUIRE(recorded.size() > 50);

    struct Answer {
        bool played;
        int id;
        float volume;
        float pitch;
    };
    const auto replay = [&](uint64_t gameSeed) {
        std::unique_ptr<GameState> game;
        if (gameSeed != 0) {
            game = std::make_unique<GameState>();
            sim::newRun(*game, gameSeed);
            game->player.maxHp = 5000.0f;
            game->player.hp = 5000.0f;
        }
        router::Router r(4242);
        router::PlayRequest req;
        std::vector<Answer> answers;
        size_t next = 0;
        for (int i = 0; i < 60 * 30; ++i) {
            if (game) {
                sim::step(*game, InputFrame{});
                game->events.clear();
            }
            for (; next < recorded.size() && recorded[next].step == i; ++next) {
                req = router::PlayRequest{};
                const bool played = r.route(recorded[next].event, static_cast<float>(i) / 60.0f, req);
                answers.push_back(Answer{played, static_cast<int>(req.id), req.volume, req.pitch});
            }
        }
        return answers;
    };
    const std::vector<Answer> alone = replay(0);
    for (const uint64_t gameSeed : {1ull, 2ull}) {
        const std::vector<Answer> beside = replay(gameSeed);
        REQUIRE(beside.size() == alone.size());
        int different = 0;
        for (size_t k = 0; k < alone.size(); ++k) {
            if (alone[k].played != beside[k].played || alone[k].id != beside[k].id || alone[k].volume != beside[k].volume ||
                alone[k].pitch != beside[k].pitch) {
                ++different;
            }
        }
        CHECK_MESSAGE(different == 0, "answers that changed: " << different << ", while a game with seed " << gameSeed << " was running");
    }
}

// Writes the sounds as .wav files for listening, and prints a level table.
// It only runs when LS_AUDIO_WAV_DIR names a folder, so normal test runs write nothing.
TEST_CASE("listening files and level table") {
    const char* dir = std::getenv("LS_AUDIO_WAV_DIR");
    if (!dir) return;
    const std::string base = dir;
    {
        // How long rendering takes on this machine, per layer. The browser is slower than this.
        using Clock = std::chrono::steady_clock;
        sfx::SfxBank timedSfx;
        const auto s0 = Clock::now();
        sfx::renderAll(timedSfx);
        std::printf("\nsfx render: %.1f ms\n", std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
        music::Bank timedMusic;
        music::Renderer renderer(timedMusic);
        double perStem[music::kStemCount] = {};
        double worstStep = 0.0;
        bool more = true;
        while (more) {
            const int stem = renderer.stem();
            const auto t0 = Clock::now();
            more = renderer.step();
            perStem[stem] += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            worstStep = std::max(worstStep, std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
        double total = 0.0;
        for (int s = 0; s < music::kStemCount; ++s) {
            std::printf("render %-12s %7.1f ms\n", music::stemName(s), perStem[s]);
            total += perStem[s];
        }
        std::printf("music render total: %.1f ms, slowest single step: %.2f ms\n", total, worstStep);
    }
    std::printf("\n%-16s %8s %8s %9s %9s\n", "sound", "seconds", "peak", "rms dBFS", "dc");
    for (int i = 0; i < sfx::kSfxCount; ++i) {
        const synth::Samples& s = sfxBank().sounds[i];
        const synth::Stats st = synth::measure(s);
        const std::string name = sfx::info(static_cast<sfx::SfxId>(i)).name;
        std::printf("%-16s %8.2f %8.3f %9.1f %9.5f\n", name.c_str(), static_cast<double>(st.seconds), static_cast<double>(st.peak), static_cast<double>(db(st.rms)), static_cast<double>(st.dc));
        writeWav(base + "/sfx_" + name + ".wav", toPcm(s), synth::kSampleRate);
    }
    std::printf("\n%-16s %8s %8s %9s\n", "music layer", "seconds", "peak", "rms dBFS");
    for (int s = 0; s < music::kStemCount; ++s) {
        const std::vector<int16_t>& data = musicBank().stems[s];
        synth::Samples f(data.size());
        for (size_t i = 0; i < data.size(); ++i) f[i] = static_cast<float>(data[i]) / 32768.0f;
        const synth::Stats st = synth::measure(f);
        std::printf("%-16s %8.2f %8.3f %9.1f\n", music::stemName(s), static_cast<double>(st.seconds), static_cast<double>(st.peak), static_cast<double>(db(st.rms)));
        writeWav(base + "/music_stem_" + music::stemName(s) + ".wav", data, music::kSampleRate);
    }
    struct Mix {
        const char* name;
        float intensity;
        bool boss;
    };
    const Mix mixes[] = {{"calm", 0.05f, false}, {"building", 0.4f, false}, {"intense", 1.0f, false}, {"boss", 1.0f, true}};
    std::printf("\n%-16s %8s %8s %9s\n", "music mix", "seconds", "peak", "rms dBFS");
    for (const Mix& m : mixes) {
        const std::vector<int16_t> mix = mixSeconds(64.0f, m.intensity, m.boss);
        synth::Samples f(mix.size());
        for (size_t i = 0; i < mix.size(); ++i) f[i] = static_cast<float>(mix[i]) / 32768.0f;
        const synth::Stats st = synth::measure(f);
        std::printf("%-16s %8.2f %8.3f %9.1f\n", m.name, static_cast<double>(st.seconds), static_cast<double>(st.peak), static_cast<double>(db(st.rms)));
        writeWav(base + "/music_mix_" + m.name + ".wav", mix, music::kSampleRate);
    }
    // Intensity climbing over a minute, then the boss: to hear the layers come in.
    writeWav(base + "/music_mix_ramp_then_boss.wav", mixSeconds(64.0f, 0.0f, false, true), music::kSampleRate);
    // The loop played twice, to hear the join.
    std::vector<int16_t> twice = mixSeconds(128.0f, 1.0f, false);
    writeWav(base + "/music_mix_loop_twice.wav", twice, music::kSampleRate);
}
