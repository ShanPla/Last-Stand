#include "components/Audio/Audio.h"

#include "components/Audio/Layers/Layers.h"
#include "components/Audio/Music/Music.h"
#include "components/Audio/Router/Router.h"
#include "components/Audio/Sfx/Sfx.h"
#include "components/Audio/Synth/Synth.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <algorithm>

// This file is the only part of the audio code that talks to raylib. All the
// sound is made by the pure code in the folders next to it. Before unlock() the
// audio device is never opened, because browsers refuse to start audio until the
// player has pressed something, and opening it early logs warnings.
#ifdef __EMSCRIPTEN__
// The browser's audio context is made inside raylib's sound library. This looks at it,
// and with resume set it asks a suspended one to start. A context only starts after the
// page has had a real click or key press, so this is tried again on later presses.
// Returns 1 when sound is running, 0 when it is not yet, and -1 when the context can't be found.
EM_JS(int, ls_js_audio_running, (int resume), {
    try {
        var devices = window.miniaudio && window.miniaudio.devices;
        if (!devices) return -1;
        var found = false;
        var running = false;
        for (var i = 0; i < devices.length; i++) {
            var context = devices[i] && devices[i].webaudio;
            if (!context) continue;
            found = true;
            if (context.state === 'running') running = true;
            else if (resume) context.resume();
        }
        return found ? (running ? 1 : 0) : -1;
    } catch (error) {
        return -1;
    }
});
#endif

namespace ls::audio {

namespace {

constexpr int kMaxVoices = 4;               // Most copies of one sound that can overlap.
constexpr int kStreamBlock = 4096;          // Frames per stream refill: about 0.19 s at 22050 Hz.
constexpr int kMaxStartsPerFrame = 12;      // A hard cap on new sounds per frame, on top of the per-sound limits.
constexpr float kSfxLevel = 0.8f;           // Leaves room so many overlapping sounds do not need the limiter.
constexpr float kMusicLevel = 0.55f;        // Music sits under the effects.
constexpr float kDuckedGain = 0.25f;        // Music volume while paused or in a menu.
constexpr double kRenderBudgetSeconds = 0.0025;  // Music rendering time allowed in one frame.
constexpr double kLoadBudgetSeconds = 0.003;  // Sound loading time allowed in one frame.
constexpr double kStallSeconds = 0.35;      // A frame gap this long means the tab was hidden or the game stalled.
constexpr double kDeviceRetrySeconds = 2.0; // After the device fails to open, wait this long before trying again.

bool g_inited = false;
bool g_unlocked = false;  // The audio device is open.
bool g_running = false;   // Sound can really be heard: the browser's audio context is running.
double g_nextDeviceTry = 0.0;
bool g_musicReady = false;
bool g_streamPlaying = false;
int g_nextToLoad = 0;  // Next sound to turn into playable voices.

float g_clock = 0.0f;  // Seconds of audio time. Only moves forward in small steps, so a hidden tab never causes a burst.
double g_lastFrameTime = 0.0;
int g_startsThisFrame = 0;

float g_musicVolume = 1.0f;
float g_effectsVolume = 1.0f;
// The game starts on the title screen, which is a menu, so music starts ducked. The app
// starts its own ducked flag the same way, so the first menu frame sends no change.
float g_duckTarget = kDuckedGain;
float g_duck = kDuckedGain;
float g_fade = 0.0f;  // Music fades in after it is ready, and again after a stall.
float g_intensity = 0.0f;
bool g_boss = false;

sfx::SfxBank g_sfxBank;
music::Bank g_musicBank;
music::Renderer* g_renderer = nullptr;
layers::StemMixer g_mixer;
router::Router g_router;
synth::Limiter g_limiter(0.9f, 0.15f, 48000);

Sound g_voices[sfx::kSfxCount][kMaxVoices];
int g_voiceCount[sfx::kSfxCount];
int g_nextVoice[sfx::kSfxCount];
AudioStream g_stream;
int16_t g_block[kStreamBlock];

// Runs on the audio side for everything that is about to be heard. When many
// sounds overlap, it turns the volume down just enough to stop clipping.
void limiterCallback(void* buffer, unsigned frameCount) { g_limiter.process(static_cast<float*>(buffer), frameCount, 2); }

void playRequest(const router::PlayRequest& request) {
    if (g_startsThisFrame >= kMaxStartsPerFrame) return;
    const int index = static_cast<int>(request.id);
    const int count = g_voiceCount[index];
    if (count <= 0) return;
    Sound* voices = g_voices[index];
    // Take a free copy if there is one. Otherwise cut the oldest, which is cheaper than refusing.
    int pick = g_nextVoice[index];
    for (int k = 0; k < count; ++k) {
        const int candidate = (g_nextVoice[index] + k) % count;
        if (!IsSoundPlaying(voices[candidate])) {
            pick = candidate;
            break;
        }
    }
    g_nextVoice[index] = (pick + 1) % count;
    SetSoundVolume(voices[pick], request.volume * g_effectsVolume * kSfxLevel);
    SetSoundPitch(voices[pick], std::clamp(request.pitch, 0.5f, 2.0f));
    PlaySound(voices[pick]);
    ++g_startsThisFrame;
}

// Spreads music rendering over many frames, so startup never freezes.
void pumpRender() {
    if (g_musicReady || !g_renderer) return;
    const double start = GetTime();
    while (g_renderer->step()) {
        if (GetTime() - start >= kRenderBudgetSeconds) return;
    }
    g_mixer.setBank(&g_musicBank);
    delete g_renderer;  // Frees the scratch buffer.
    g_renderer = nullptr;
    g_musicReady = true;
}

void refillStream() {
    g_mixer.mix(g_block, kStreamBlock);
    UpdateAudioStream(g_stream, g_block, kStreamBlock);
}

float streamVolume() { return g_musicVolume * g_duck * g_fade * kMusicLevel; }

void startStream() {
    g_mixer.setTargets(layers::targetsFor(g_intensity, g_boss));
    g_mixer.snapToTargets();
    // The fade starts from silence, and the volume is set before the stream starts. The
    // mixer thread can read the volume the moment playing begins, and raylib's default
    // is full volume, which would be heard as a pop.
    g_fade = 0.0f;
    SetAudioStreamVolume(g_stream, streamVolume());
    // Play first: starting a stream marks its buffers as empty, which would discard data written before it.
    PlayAudioStream(g_stream);
    refillStream();
    refillStream();
    g_streamPlaying = true;
}

// Turns one rendered sound into playable voices. Done a few per frame after
// unlock, because converting every sound at once would freeze a frame.
void loadSound(int i) {
    synth::Samples& samples = g_sfxBank.sounds[i];
    Wave wave{};
    wave.frameCount = static_cast<unsigned>(samples.size());
    wave.sampleRate = synth::kSampleRate;
    wave.sampleSize = 32;  // 32 bit float.
    wave.channels = 1;
    wave.data = samples.data();
    const Sound source = LoadSoundFromWave(wave);
    g_voices[i][0] = source;
    // Aliases share the source's data, so extra voices cost almost no memory.
    const int wanted = std::min(kMaxVoices, sfx::info(static_cast<sfx::SfxId>(i)).voices);
    int made = IsSoundValid(source) ? 1 : 0;
    while (made > 0 && made < wanted) {
        g_voices[i][made] = LoadSoundAlias(source);
        ++made;
    }
    g_nextVoice[i] = 0;
    // The device now holds its own converted copy, so the original is not needed.
    synth::Samples().swap(samples);
    g_voiceCount[i] = made;  // Set last: a sound is skipped until it is fully loaded.
}

void loadSomeSounds() {
    const double start = GetTime();
    while (g_nextToLoad < sfx::kSfxCount) {
        loadSound(g_nextToLoad++);
        if (GetTime() - start >= kLoadBudgetSeconds) return;
    }
}

}  // namespace

void init() {
    if (g_inited) return;
    g_inited = true;
    // Both are fast, pure computation. Nothing here touches the audio device.
    sfx::renderAll(g_sfxBank);
    g_renderer = new music::Renderer(g_musicBank);
    g_lastFrameTime = GetTime();
}

bool unlock(bool pressed) {
    if (!g_inited) return false;
    if (g_running) return true;
    if (!g_unlocked) {
        // Open the device. If there is none yet (no output, or the browser refused), try again
        // on a later press, but not more than once in a while, instead of giving up for the
        // whole session. A failed attempt prints a warning, so it is never repeated every frame.
        if (!pressed || GetTime() < g_nextDeviceTry) return false;
        InitAudioDevice();
        if (!IsAudioDeviceReady()) {
            g_nextDeviceTry = GetTime() + kDeviceRetrySeconds;
            return false;
        }
        SetAudioStreamBufferSizeDefault(kStreamBlock);
        g_stream = LoadAudioStream(music::kSampleRate, 16, 1);
        AttachAudioMixedProcessor(limiterCallback);
        g_unlocked = true;
    }
#ifdef __EMSCRIPTEN__
    // An open device is not enough in a browser: the context starts suspended, and a press
    // that does not count as user activation (Esc, a gamepad button) leaves it that way.
    // Ask it to start on every press, and look at it every frame until it runs.
    g_running = ls_js_audio_running(pressed ? 1 : 0) != 0;
#else
    (void)pressed;
    g_running = true;
#endif
    return g_running;
}

bool isRunning() { return g_running; }
float duckLevel() { return g_duck; }

void update(float dtReal, float intensity, bool boss) {
    if (!g_inited) return;
    const double now = GetTime();
    const double gap = now - g_lastFrameTime;
    g_lastFrameTime = now;
    const float dt = std::clamp(dtReal, 0.0f, 0.1f);
    g_clock += dt;
    g_startsThisFrame = 0;
    g_intensity = intensity;
    g_boss = boss;

    pumpRender();
    if (g_unlocked) loadSomeSounds();
    if (!g_running || !g_musicReady) return;

    // After a long gap (hidden tab, or a stall) the stream has run dry. Restart it
    // cleanly and fade in, instead of playing stale data.
    if (g_streamPlaying && gap > kStallSeconds) {
        StopAudioStream(g_stream);
        g_streamPlaying = false;
    }
    if (!g_streamPlaying) startStream();

    g_duck += (g_duckTarget - g_duck) * std::min(1.0f, dt * 6.0f);
    g_fade = std::min(1.0f, g_fade + dt / 2.0f);
    g_mixer.setTargets(layers::targetsFor(intensity, boss));
    // Two refills a frame is plenty: a refill is only needed about five times a second.
    for (int i = 0; i < 2 && IsAudioStreamProcessed(g_stream); ++i) refillStream();
    SetAudioStreamVolume(g_stream, streamVolume());
}

void onEvent(const SimEvent& event) {
    if (!g_running) return;
    router::PlayRequest request;
    if (g_router.route(event, g_clock, request)) playRequest(request);
}

void playUi(UiSound sound) {
    if (!g_running) return;
    router::PlayRequest request;
    if (g_router.routeUi(sound, g_clock, request)) playRequest(request);
}

void setMusicVolume(float volume) { g_musicVolume = std::clamp(volume, 0.0f, 1.0f); }
void setEffectsVolume(float volume) { g_effectsVolume = std::clamp(volume, 0.0f, 1.0f); }
void setDucked(bool ducked) { g_duckTarget = ducked ? kDuckedGain : 1.0f; }

}  // namespace ls::audio
