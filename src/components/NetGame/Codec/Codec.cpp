#include "components/NetGame/Codec/Codec.h"

#include <cmath>
#include <cstring>

namespace ls::netgame {

namespace {

// ---- Writing and reading bytes -----------------------------------------------------

// Writes into a fixed buffer. Once anything does not fit, every later write is ignored and
// full is set, so the caller checks once at the end instead of after every field.
struct Writer {
    uint8_t* data;
    int capacity;
    int size = 0;
    bool full = false;

    bool room(int bytes) {
        if (full || size + bytes > capacity) {
            full = true;
            return false;
        }
        return true;
    }
    void u8(uint32_t v) {
        if (room(1)) data[size++] = static_cast<uint8_t>(v);
    }
    void u16(uint32_t v) {
        if (!room(2)) return;
        data[size++] = static_cast<uint8_t>(v);
        data[size++] = static_cast<uint8_t>(v >> 8);
    }
    void u32(uint32_t v) {
        if (!room(4)) return;
        for (int i = 0; i < 4; ++i) data[size++] = static_cast<uint8_t>(v >> (8 * i));
    }
    void u64(uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }
    void i16(int v) { u16(static_cast<uint16_t>(static_cast<int16_t>(v))); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
};

// Reads from a packet of known length. Reading past the end gives zeros and sets bad.
struct Reader {
    const uint8_t* data;
    int size;
    int at = 0;
    bool bad = false;

    bool has(int bytes) {
        if (bad || at + bytes > size) {
            bad = true;
            return false;
        }
        return true;
    }
    uint32_t u8() { return has(1) ? data[at++] : 0u; }
    uint32_t u16() {
        if (!has(2)) return 0;
        const uint32_t v = static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8);
        at += 2;
        return v;
    }
    uint32_t u32() {
        if (!has(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t low = u32();
        return low | (static_cast<uint64_t>(u32()) << 32);
    }
    int i16() { return static_cast<int16_t>(static_cast<uint16_t>(u16())); }
    float f32() {
        const uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        // A broken packet could hold a NaN or an infinity, which would spread through every
        // sum it touches. Such a number is read as 0.
        return std::isfinite(v) ? v : 0.0f;
    }
};

// ---- Rounding values to fewer bytes --------------------------------------------------

int roundToInt(float v) { return static_cast<int>(std::lround(v)); }
int clampInt(int v, int low, int high) { return v < low ? low : (v > high ? high : v); }

void putOffset(Writer& w, float value, float origin) { w.i16(clampInt(roundToInt((value - origin) * kPositionScale), -32767, 32767)); }
float getOffset(Reader& r, float origin) { return origin + static_cast<float>(r.i16()) / kPositionScale; }

void putPos(Writer& w, Vec2 pos, Vec2 origin) {
    putOffset(w, pos.x, origin.x);
    putOffset(w, pos.y, origin.y);
}
Vec2 getPos(Reader& r, Vec2 origin) {
    const float x = getOffset(r, origin.x);
    return Vec2{x, getOffset(r, origin.y)};
}

// A direction as one byte: 256 steps around the circle, about 1.4 degrees each.
void putAngle(Writer& w, Vec2 dir) {
    const float angle = std::atan2(dir.y, dir.x);
    w.u8(static_cast<uint32_t>(roundToInt(angle / kTau * 256.0f)) & 255u);
}
Vec2 getAngle(Reader& r) { return fromAngle(static_cast<float>(r.u8()) * kTau / 256.0f); }

// A time or other small positive amount as a whole number of 1/scale units, in one or two bytes.
void putUnit8(Writer& w, float v, float scale) { w.u8(static_cast<uint32_t>(clampInt(roundToInt(v * scale), 0, 255))); }
float getUnit8(Reader& r, float scale) { return static_cast<float>(r.u8()) / scale; }
void putUnit16(Writer& w, float v, float scale) { w.u16(static_cast<uint32_t>(clampInt(roundToInt(v * scale), 0, 65535))); }
float getUnit16(Reader& r, float scale) { return static_cast<float>(r.u16()) / scale; }

// A velocity in whole world units per second.
void putVelocity(Writer& w, Vec2 v) {
    w.i16(clampInt(roundToInt(v.x), -32767, 32767));
    w.i16(clampInt(roundToInt(v.y), -32767, 32767));
}
Vec2 getVelocity(Reader& r) {
    const float x = static_cast<float>(r.i16());
    return Vec2{x, static_cast<float>(r.i16())};
}

constexpr float kTimeScale = 1000.0f;    // Milliseconds, up to 65 seconds.
constexpr float kRadiusScale = 8.0f;     // Eighths of a world unit, up to about 8,000.

bool startMessage(Writer& w, MessageKind kind) {
    w.u8(static_cast<uint8_t>(kind));
    w.u8(kProtocolVersion);
    return !w.full;
}

bool openMessage(Reader& r, MessageKind kind) {
    const uint32_t got = r.u8();
    const uint32_t version = r.u8();
    return !r.bad && got == static_cast<uint8_t>(kind) && version == kProtocolVersion;
}

int finish(const Writer& w) { return w.full ? 0 : w.size; }
// A packet is only accepted when it was read exactly to its end: a longer one is not ours.
bool done(const Reader& r) { return !r.bad && r.at == r.size; }

template <typename E>
bool validEnum(uint32_t value, E count) { return value < static_cast<uint32_t>(count); }

// ---- Snapshot parts ------------------------------------------------------------------

void putPlayer(Writer& w, const Player& p) {
    w.f32(p.pos.x);
    w.f32(p.pos.y);
    putAngle(w, p.facing);
    putAngle(w, p.aim);
    putAngle(w, p.dashDir);
    w.u8((p.downed ? 1u : 0u) | (p.cardsOpen ? 2u : 0u));
    w.f32(p.hp);
    w.f32(p.maxHp);
    putUnit16(w, p.dashLeft, kTimeScale);
    putUnit16(w, p.dashCooldown, kTimeScale);
    putUnit16(w, p.dashInvulnerable, kTimeScale);
    putUnit16(w, p.invulnerable, kTimeScale);
    putUnit16(w, p.hitFlash, kTimeScale);
    putUnit16(w, p.reviveProgress, kTimeScale);
    putUnit16(w, p.cardShield, kTimeScale);
    w.u16(static_cast<uint32_t>(clampInt(p.level, 0, 65535)));
    w.u32(static_cast<uint32_t>(p.xp < 0 ? 0 : p.xp));
    w.u32(static_cast<uint32_t>(p.xpToNext < 0 ? 0 : p.xpToNext));
    w.f32(p.bladeAngle);
    w.f32(p.stats.damageMult);
    w.f32(p.stats.cooldownMult);
    w.f32(p.stats.areaMult);
    w.f32(p.stats.moveSpeed);
    w.f32(p.stats.pickupRange);
    w.f32(p.stats.armor);
    w.u8(static_cast<uint32_t>(clampInt(p.weaponCount, 0, kMaxHeldWeapons)));
    for (int i = 0; i < p.weaponCount && i < kMaxHeldWeapons; ++i) {
        const WeaponSlot& s = p.weapons[i];
        w.u8(static_cast<uint32_t>(s.id) | (s.evolved ? 0x80u : 0u));
        w.u8(s.level);
        w.u16(static_cast<uint32_t>(clampInt(s.magazine, 0, 65535)));
        w.u16(static_cast<uint32_t>(clampInt(s.reserve, 0, 65535)));
        putUnit16(w, s.reloadLeft, kTimeScale);
    }
    w.u8(static_cast<uint32_t>(clampInt(p.passiveCount, 0, kMaxHeldPassives)));
    for (int i = 0; i < p.passiveCount && i < kMaxHeldPassives; ++i) {
        w.u8(static_cast<uint32_t>(p.passives[i].id));
        w.u8(p.passives[i].level);
    }
    w.u8(static_cast<uint32_t>(clampInt(p.rerollsLeft, 0, 255)));
    w.u8(static_cast<uint32_t>(clampInt(p.pendingLevels, 0, 255)));
    w.u8(static_cast<uint32_t>(clampInt(p.offerCount, 0, kOffersPerLevel)));
    for (int i = 0; i < p.offerCount && i < kOffersPerLevel; ++i) {
        const Card& c = p.offers[i];
        w.u8(static_cast<uint32_t>(c.kind));
        w.u8(static_cast<uint32_t>(c.weapon));
        w.u8(static_cast<uint32_t>(c.passive));
        w.u8(c.newLevel);
    }
}

bool getPlayer(Reader& r, Player& p) {
    p = Player{};
    p.pos.x = r.f32();
    p.pos.y = r.f32();
    p.prevPos = p.pos;
    p.facing = getAngle(r);
    p.aim = getAngle(r);
    p.dashDir = getAngle(r);
    const uint32_t flags = r.u8();
    p.downed = (flags & 1u) != 0;
    p.cardsOpen = (flags & 2u) != 0;
    p.hp = r.f32();
    p.maxHp = r.f32();
    p.dashLeft = getUnit16(r, kTimeScale);
    p.dashCooldown = getUnit16(r, kTimeScale);
    p.dashInvulnerable = getUnit16(r, kTimeScale);
    p.invulnerable = getUnit16(r, kTimeScale);
    p.hitFlash = getUnit16(r, kTimeScale);
    p.reviveProgress = getUnit16(r, kTimeScale);
    p.cardShield = getUnit16(r, kTimeScale);
    p.level = static_cast<int>(r.u16());
    p.xp = static_cast<int>(r.u32() & 0x7FFFFFFFu);
    p.xpToNext = static_cast<int>(r.u32() & 0x7FFFFFFFu);
    p.bladeAngle = r.f32();
    p.stats.damageMult = r.f32();
    p.stats.cooldownMult = r.f32();
    p.stats.areaMult = r.f32();
    p.stats.moveSpeed = r.f32();
    p.stats.pickupRange = r.f32();
    p.stats.armor = r.f32();
    p.weaponCount = static_cast<int>(r.u8());
    if (p.weaponCount > kMaxHeldWeapons) return false;
    for (int i = 0; i < p.weaponCount; ++i) {
        WeaponSlot& s = p.weapons[i];
        const uint32_t idByte = r.u8();
        if (!validEnum(idByte & 0x7Fu, WeaponId::Count)) return false;
        s.id = static_cast<WeaponId>(idByte & 0x7Fu);
        s.evolved = (idByte & 0x80u) != 0;
        s.level = static_cast<uint8_t>(r.u8());
        s.magazine = static_cast<int>(r.u16());
        s.reserve = static_cast<int>(r.u16());
        s.reloadLeft = getUnit16(r, kTimeScale);
    }
    p.passiveCount = static_cast<int>(r.u8());
    if (p.passiveCount > kMaxHeldPassives) return false;
    for (int i = 0; i < p.passiveCount; ++i) {
        const uint32_t id = r.u8();
        if (!validEnum(id, PassiveId::Count)) return false;
        p.passives[i].id = static_cast<PassiveId>(id);
        p.passives[i].level = static_cast<uint8_t>(r.u8());
    }
    p.rerollsLeft = static_cast<int>(r.u8());
    p.pendingLevels = static_cast<int>(r.u8());
    p.offerCount = static_cast<int>(r.u8());
    if (p.offerCount > kOffersPerLevel) return false;
    for (int i = 0; i < p.offerCount; ++i) {
        Card& c = p.offers[i];
        const uint32_t kind = r.u8();
        const uint32_t weapon = r.u8();
        const uint32_t passive = r.u8();
        if (kind > static_cast<uint32_t>(CardKind::Heal) || !validEnum(weapon, WeaponId::Count) || !validEnum(passive, PassiveId::Count)) return false;
        c.kind = static_cast<CardKind>(kind);
        c.weapon = static_cast<WeaponId>(weapon);
        c.passive = static_cast<PassiveId>(passive);
        c.newLevel = static_cast<uint8_t>(r.u8());
    }
    return !r.bad;
}

// The fields that only some zombies use (an attack's state and timer, a locked direction, an
// HP bar) are sent only for those, which keeps a plain walker to 7 bytes.
bool needsExtra(const Enemy& e) {
    return e.elite || e.state != 0 || e.type == EnemyType::Spitter || e.type == EnemyType::Charger || e.type == EnemyType::Boss;
}

void putEnemy(Writer& w, const Enemy& e, Vec2 origin) {
    const bool extra = needsExtra(e);
    w.u16(shortId(e.id));
    w.u8(static_cast<uint32_t>(e.type) | (e.elite ? 0x10u : 0u) | (e.flash > 0.0f ? 0x20u : 0u) | (extra ? 0x40u : 0u));
    putPos(w, e.pos, origin);
    if (!extra) return;
    w.u8(e.state);
    putAngle(w, e.dir);
    putUnit16(w, e.timer, kTimeScale);
    putUnit8(w, e.maxHp > 0.0f ? clampf(e.hp / e.maxHp, 0.0f, 1.0f) : 0.0f, 255.0f);
    putUnit16(w, e.radius, kRadiusScale);
}

bool getEnemy(Reader& r, Enemy& e, Vec2 origin) {
    e = Enemy{};
    e.id = r.u16();
    const uint32_t flags = r.u8();
    if (!validEnum(flags & 0x0Fu, EnemyType::Count)) return false;
    e.type = static_cast<EnemyType>(flags & 0x0Fu);
    e.elite = (flags & 0x10u) != 0;
    // The hit flash is only on or off over the network. A short flash is enough to see a hit.
    e.flash = (flags & 0x20u) != 0 ? 0.06f : 0.0f;
    e.pos = getPos(r, origin);
    e.prevPos = e.pos;
    e.radius = balance::kEnemyStats[static_cast<int>(e.type)].radius;
    e.hp = 1.0f;
    e.maxHp = 1.0f;
    if ((flags & 0x40u) != 0) {
        e.state = static_cast<uint8_t>(r.u8());
        e.dir = getAngle(r);
        e.timer = getUnit16(r, kTimeScale);
        e.hp = getUnit8(r, 255.0f);
        e.radius = getUnit16(r, kRadiusScale);
    }
    return !r.bad;
}

void putZone(Writer& w, const Zone& z, Vec2 origin) {
    w.u8(static_cast<uint32_t>(z.kind));
    w.u8(static_cast<uint32_t>(z.source));
    w.u8((z.evolved ? 1u : 0u) | (z.hostile ? 2u : 0u) | (z.applied ? 4u : 0u) | (static_cast<uint32_t>(z.owner & 3u) << 4));
    putPos(w, z.a, origin);
    putPos(w, z.b, origin);
    putUnit16(w, z.radius, kRadiusScale);
    putUnit16(w, z.prevRadius, kRadiusScale);
    putUnit16(w, z.maxRadius, kRadiusScale);
    putUnit16(w, z.life, kTimeScale);
    putUnit16(w, z.maxLife, kTimeScale);
}

bool getZone(Reader& r, Zone& z, Vec2 origin) {
    z = Zone{};
    const uint32_t kind = r.u8();
    const uint32_t source = r.u8();
    if (kind > static_cast<uint32_t>(ZoneKind::BossLine) || !validEnum(source, WeaponId::Count)) return false;
    z.kind = static_cast<ZoneKind>(kind);
    z.source = static_cast<WeaponId>(source);
    const uint32_t flags = r.u8();
    z.evolved = (flags & 1u) != 0;
    z.hostile = (flags & 2u) != 0;
    z.applied = (flags & 4u) != 0;
    z.owner = static_cast<uint8_t>((flags >> 4) & 3u);
    z.a = getPos(r, origin);
    z.b = getPos(r, origin);
    z.radius = getUnit16(r, kRadiusScale);
    z.prevRadius = getUnit16(r, kRadiusScale);
    z.maxRadius = getUnit16(r, kRadiusScale);
    z.life = getUnit16(r, kTimeScale);
    z.maxLife = getUnit16(r, kTimeScale);
    return !r.bad;
}

void putEvent(Writer& w, const SimEvent& e, uint8_t age, Vec2 origin) {
    w.u8(static_cast<uint32_t>(e.type));
    w.u8(e.detail);
    w.u8(e.player);
    w.u8((e.flag ? 1u : 0u));
    w.u8(age);
    putPos(w, e.pos, origin);
    w.f32(e.value);
}

bool getEvent(Reader& r, SimEvent& e, uint8_t& age, Vec2 origin) {
    e = SimEvent{};
    const uint32_t type = r.u8();
    if (type > static_cast<uint32_t>(EventType::PlayerRevived)) return false;
    e.type = static_cast<EventType>(type);
    e.detail = static_cast<uint8_t>(r.u8());
    e.player = static_cast<uint8_t>(r.u8());
    if (e.player != kNoPlayer && e.player >= kMaxPlayers) return false;
    e.flag = (r.u8() & 1u) != 0;
    age = static_cast<uint8_t>(r.u8());
    e.pos = getPos(r, origin);
    e.value = r.f32();
    return !r.bad;
}

void putBullet(Writer& w, const EnemyBullet& b, Vec2 origin) {
    putPos(w, b.pos, origin);
    putVelocity(w, b.vel);
    putUnit8(w, b.radius, 8.0f);
}

void getBullet(Reader& r, EnemyBullet& b, Vec2 origin) {
    b = EnemyBullet{};
    b.pos = getPos(r, origin);
    b.prevPos = b.pos;
    b.vel = getVelocity(r);
    b.radius = getUnit8(r, 8.0f);
    b.life = 1.0f;
}

void putShot(Writer& w, const Projectile& p, Vec2 origin) {
    w.u8(static_cast<uint32_t>(p.kind) | (p.evolved ? 0x08u : 0u) | (static_cast<uint32_t>(p.owner & 3u) << 4));
    w.u8(static_cast<uint32_t>(p.source));
    putPos(w, p.pos, origin);
    putVelocity(w, p.vel);
    putUnit8(w, p.radius, 8.0f);
    putUnit16(w, p.life, kTimeScale);
}

bool getShot(Reader& r, Projectile& p, Vec2 origin) {
    p = Projectile{};
    const uint32_t flags = r.u8();
    const uint32_t source = r.u8();
    if ((flags & 7u) > static_cast<uint32_t>(ProjectileKind::Mine) || !validEnum(source, WeaponId::Count)) return false;
    p.kind = static_cast<ProjectileKind>(flags & 7u);
    p.evolved = (flags & 0x08u) != 0;
    p.owner = static_cast<uint8_t>((flags >> 4) & 3u);
    p.source = static_cast<WeaponId>(source);
    p.pos = getPos(r, origin);
    p.prevPos = p.pos;
    p.vel = getVelocity(r);
    p.radius = getUnit8(r, 8.0f);
    p.life = getUnit16(r, kTimeScale);
    return !r.bad;
}

void putGem(Writer& w, const Gem& g, Vec2 origin) {
    putPos(w, g.pos, origin);
    w.u8(static_cast<uint32_t>(clampInt(g.xp, 0, 255)));
}

void getGem(Reader& r, Gem& g, Vec2 origin) {
    g = Gem{};
    g.pos = getPos(r, origin);
    g.prevPos = g.pos;
    g.xp = static_cast<int>(r.u8());
}

// Writes a list of up to `count` items, each written by `put`, as many as fit. The count in
// front is written first and corrected at the end. An item that does not fit is taken back,
// so the packet always ends on a whole item. Returns how many were written.
template <typename Put>
int putList(Writer& w, int count, Put put) {
    const int countAt = w.size;
    w.u16(0);
    if (w.full) return 0;
    int written = 0;
    for (int i = 0; i < count; ++i) {
        const int before = w.size;
        put(i);
        if (w.full) {
            w.size = before;
            w.full = false;
            break;
        }
        written++;
    }
    w.data[countAt] = static_cast<uint8_t>(written);
    w.data[countAt + 1] = static_cast<uint8_t>(written >> 8);
    // Nothing more fits once one list has been cut, so the later lists are left empty.
    if (written < count) w.full = true;
    return written;
}

}  // namespace

uint8_t kindOf(const uint8_t* data, int size) {
    if (!data || size < 2 || data[1] != kProtocolVersion) return 0;
    return data[0];
}

InputSample quantizeInput(float moveX, float moveY, bool dash) {
    InputSample sample;
    sample.moveX = static_cast<int8_t>(clampInt(roundToInt(clampf(moveX, -1.0f, 1.0f) * 127.0f), -127, 127));
    sample.moveY = static_cast<int8_t>(clampInt(roundToInt(clampf(moveY, -1.0f, 1.0f) * 127.0f), -127, 127));
    sample.dash = dash;
    return sample;
}

float unquantizeMove(int8_t value) { return static_cast<float>(value) / 127.0f; }

// ---- Small messages --------------------------------------------------------------

int encodeRunStart(const RunStart& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::RunStart);
    w.u8(m.runNumber);
    w.u64(m.seed);
    w.u8(static_cast<uint32_t>(m.map));
    w.u8(static_cast<uint32_t>(m.difficulty));
    w.u8(static_cast<uint32_t>(m.mode));
    w.u8(static_cast<uint32_t>(m.playerCount));
    w.u8(static_cast<uint32_t>(m.yourSlot));
    for (uint8_t id : m.netIds) w.u8(id);
    return finish(w);
}

bool decodeRunStart(const uint8_t* data, int size, RunStart& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::RunStart)) return false;
    RunStart m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    m.seed = r.u64();
    const uint32_t map = r.u8();
    const uint32_t difficulty = r.u8();
    const uint32_t mode = r.u8();
    m.playerCount = static_cast<int>(r.u8());
    m.yourSlot = static_cast<int>(r.u8());
    for (uint8_t& id : m.netIds) id = static_cast<uint8_t>(r.u8());
    if (!done(r) || !validEnum(map, maps::MapId::Count) || !validEnum(difficulty, Difficulty::Count) || !validEnum(mode, RunMode::Count)) return false;
    if (m.playerCount < 2 || m.playerCount > kMaxPlayers || m.yourSlot < 0 || m.yourSlot >= m.playerCount) return false;
    m.map = static_cast<maps::MapId>(map);
    m.difficulty = static_cast<Difficulty>(difficulty);
    m.mode = static_cast<RunMode>(mode);
    out = m;
    return true;
}

int encodeInput(const InputMessage& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::Input);
    w.u8(m.runNumber);
    w.u32(m.newestSeq);
    const int count = clampInt(m.count, 0, balance::kNetInputRepeat);
    w.u8(static_cast<uint32_t>(count));
    for (int i = 0; i < count; ++i) {
        w.u8(static_cast<uint8_t>(m.samples[i].moveX));
        w.u8(static_cast<uint8_t>(m.samples[i].moveY));
        w.u8(m.samples[i].dash ? 1u : 0u);
    }
    return finish(w);
}

bool decodeInput(const uint8_t* data, int size, InputMessage& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::Input)) return false;
    InputMessage m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    m.newestSeq = r.u32();
    m.count = static_cast<int>(r.u8());
    if (m.count < 1 || m.count > balance::kNetInputRepeat) return false;
    for (int i = 0; i < m.count; ++i) {
        // -128 is never sent, so full speed is the same both ways.
        m.samples[i].moveX = static_cast<int8_t>(clampInt(static_cast<int8_t>(r.u8()), -127, 127));
        m.samples[i].moveY = static_cast<int8_t>(clampInt(static_cast<int8_t>(r.u8()), -127, 127));
        m.samples[i].dash = (r.u8() & 1u) != 0;
    }
    if (!done(r)) return false;
    out = m;
    return true;
}

int encodeCardPick(const CardPick& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::CardPick);
    w.u8(m.runNumber);
    w.u8(static_cast<uint8_t>(m.card));
    w.u8(m.reroll ? 1u : 0u);
    return finish(w);
}

bool decodeCardPick(const uint8_t* data, int size, CardPick& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::CardPick)) return false;
    CardPick m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    m.card = static_cast<int8_t>(r.u8());
    m.reroll = (r.u8() & 1u) != 0;
    if (!done(r) || m.card < -1 || m.card >= kOffersPerLevel) return false;
    out = m;
    return true;
}

int encodeCrushed(const CrushedMessage& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::Crushed);
    w.u8(m.runNumber);
    w.u16(static_cast<uint32_t>(clampInt(m.firstIndex, 0, kMaxCrushedCars)));
    const int count = clampInt(m.count, 0, kMaxCrushedPerMessage);
    w.u8(static_cast<uint32_t>(count));
    for (int i = 0; i < count; ++i) {
        w.u32(static_cast<uint32_t>(m.cars[i].texelX));
        w.u32(static_cast<uint32_t>(m.cars[i].texelY));
    }
    return finish(w);
}

bool decodeCrushed(const uint8_t* data, int size, CrushedMessage& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::Crushed)) return false;
    CrushedMessage m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    m.firstIndex = static_cast<int>(r.u16());
    m.count = static_cast<int>(r.u8());
    if (m.count > kMaxCrushedPerMessage || m.firstIndex + m.count > kMaxCrushedCars) return false;
    for (int i = 0; i < m.count; ++i) {
        m.cars[i].texelX = static_cast<int>(r.u32());
        m.cars[i].texelY = static_cast<int>(r.u32());
    }
    if (!done(r)) return false;
    out = m;
    return true;
}

int encodePause(const PauseMessage& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::Pause);
    w.u8(m.runNumber);
    w.u8((m.paused ? 1u : 0u) | (m.away ? 2u : 0u));
    return finish(w);
}

bool decodePause(const uint8_t* data, int size, PauseMessage& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::Pause)) return false;
    PauseMessage m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    const uint32_t flags = r.u8();
    if (!done(r)) return false;
    m.paused = (flags & 1u) != 0;
    m.away = (flags & 2u) != 0;
    out = m;
    return true;
}

int encodeRunEnd(const RunEnd& m, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    startMessage(w, MessageKind::RunEnd);
    w.u8(m.runNumber);
    w.u8(static_cast<uint32_t>(m.phase));
    w.u32(m.tick);
    w.u32(static_cast<uint32_t>(m.kills < 0 ? 0 : m.kills));
    w.u16(static_cast<uint32_t>(clampInt(m.bossesKilled, 0, 65535)));
    const int count = clampInt(m.playerCount, 0, kMaxPlayers);
    w.u8(static_cast<uint32_t>(count));
    for (int slot = 0; slot < count; ++slot) {
        w.u16(static_cast<uint32_t>(clampInt(m.levels[slot], 0, 65535)));
        for (float damage : m.damageByWeapon[slot]) w.f32(damage);
    }
    return finish(w);
}

bool decodeRunEnd(const uint8_t* data, int size, RunEnd& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::RunEnd)) return false;
    RunEnd m;
    m.runNumber = static_cast<uint8_t>(r.u8());
    const uint32_t phase = r.u8();
    m.tick = r.u32();
    m.kills = static_cast<int>(r.u32() & 0x7FFFFFFFu);
    m.bossesKilled = static_cast<int>(r.u16());
    m.playerCount = static_cast<int>(r.u8());
    if (m.playerCount > kMaxPlayers || (phase != static_cast<uint32_t>(RunPhase::Won) && phase != static_cast<uint32_t>(RunPhase::Dead))) return false;
    m.phase = static_cast<RunPhase>(phase);
    for (int slot = 0; slot < m.playerCount; ++slot) {
        m.levels[slot] = static_cast<int>(r.u16());
        for (float& damage : m.damageByWeapon[slot]) damage = r.f32();
    }
    if (!done(r)) return false;
    out = m;
    return true;
}

// ---- Snapshot --------------------------------------------------------------------

int encodeSnapshot(const Snapshot& s, uint8_t* out, int capacity) {
    Writer w{out, capacity};
    const SnapshotHeader& h = s.header;
    startMessage(w, MessageKind::Snapshot);
    w.u8(h.runNumber);
    w.u32(h.tick);
    w.u8(static_cast<uint32_t>(h.phase));
    w.u8((h.bossSpawned ? 1u : 0u) | (h.hostPaused ? 2u : 0u) | (h.hostAway ? 4u : 0u));
    w.u32(static_cast<uint32_t>(h.kills < 0 ? 0 : h.kills));
    w.u8(h.bossPhase);
    putUnit16(w, h.bossHpFraction, 65535.0f);
    w.u16(static_cast<uint32_t>(clampInt(h.bossesKilled, 0, 65535)));
    w.u32(h.ackSeq);
    w.u16(static_cast<uint32_t>(clampInt(h.crushedCount, 0, kMaxCrushedCars)));
    w.f32(h.origin.x);
    w.f32(h.origin.y);
    const int players = clampInt(h.playerCount, 0, kMaxPlayers);
    w.u8(static_cast<uint32_t>(players));
    for (int i = 0; i < players; ++i) putPlayer(w, s.players[i]);
    if (w.full) return 0;

    const Vec2 o = h.origin;
    putList(w, s.pickupCount, [&](int i) {
        putPos(w, s.pickups[i].pos, o);
        w.u8(static_cast<uint32_t>(s.pickups[i].type));
    });
    putList(w, s.zoneCount, [&](int i) { putZone(w, s.zones[i], o); });
    putList(w, s.eventCount, [&](int i) { putEvent(w, s.events[i], s.eventAge[i], o); });
    putList(w, s.enemyBulletCount, [&](int i) { putBullet(w, s.enemyBullets[i], o); });
    putList(w, s.enemyCount, [&](int i) { putEnemy(w, s.enemies[i], o); });
    putList(w, s.projectileCount, [&](int i) { putShot(w, s.projectiles[i], o); });
    putList(w, s.gemCount, [&](int i) { putGem(w, s.gems[i], o); });
    // A list cut short leaves the lists after it out entirely, counts and all. The reader
    // takes a list that is missing at the very end as an empty one.
    return w.size;
}

bool decodeSnapshot(const uint8_t* data, int size, Snapshot& out) {
    Reader r{data, size};
    if (!openMessage(r, MessageKind::Snapshot)) return false;
    SnapshotHeader& h = out.header;
    h = SnapshotHeader{};
    h.runNumber = static_cast<uint8_t>(r.u8());
    h.tick = r.u32();
    const uint32_t phase = r.u8();
    if (phase > static_cast<uint32_t>(RunPhase::Dead)) return false;
    h.phase = static_cast<RunPhase>(phase);
    const uint32_t flags = r.u8();
    h.bossSpawned = (flags & 1u) != 0;
    h.hostPaused = (flags & 2u) != 0;
    h.hostAway = (flags & 4u) != 0;
    h.kills = static_cast<int>(r.u32() & 0x7FFFFFFFu);
    h.bossPhase = static_cast<uint8_t>(r.u8());
    h.bossHpFraction = getUnit16(r, 65535.0f);
    h.bossesKilled = static_cast<int>(r.u16());
    h.ackSeq = r.u32();
    h.crushedCount = static_cast<int>(r.u16());
    h.origin.x = r.f32();
    h.origin.y = r.f32();
    h.playerCount = static_cast<int>(r.u8());
    if (r.bad || h.playerCount > kMaxPlayers || h.crushedCount > kMaxCrushedCars) return false;
    for (int i = 0; i < h.playerCount; ++i) {
        if (!getPlayer(r, out.players[i])) return false;
    }
    const Vec2 o = h.origin;

    // Each list: a count that must fit the array, then that many items. A list missing at the
    // very end (a snapshot cut short) is read as empty.
    const auto count = [&](int capacity, int& n) {
        n = 0;
        if (r.at == r.size) return true;
        n = static_cast<int>(r.u16());
        return !r.bad && n <= capacity;
    };
    if (!count(kMaxPickups, out.pickupCount)) return false;
    for (int i = 0; i < out.pickupCount; ++i) {
        out.pickups[i].pos = getPos(r, o);
        const uint32_t type = r.u8();
        if (!validEnum(type, PickupType::Count)) return false;
        out.pickups[i].type = static_cast<PickupType>(type);
    }
    if (!count(balance::kNetMaxZones, out.zoneCount)) return false;
    for (int i = 0; i < out.zoneCount; ++i) {
        if (!getZone(r, out.zones[i], o)) return false;
    }
    if (!count(balance::kNetMaxEvents, out.eventCount)) return false;
    for (int i = 0; i < out.eventCount; ++i) {
        if (!getEvent(r, out.events[i], out.eventAge[i], o)) return false;
    }
    if (!count(balance::kNetMaxEnemyBullets, out.enemyBulletCount)) return false;
    for (int i = 0; i < out.enemyBulletCount; ++i) getBullet(r, out.enemyBullets[i], o);
    if (!count(balance::kNetMaxEnemies, out.enemyCount)) return false;
    for (int i = 0; i < out.enemyCount; ++i) {
        if (!getEnemy(r, out.enemies[i], o)) return false;
    }
    if (!count(balance::kNetMaxProjectiles, out.projectileCount)) return false;
    for (int i = 0; i < out.projectileCount; ++i) {
        if (!getShot(r, out.projectiles[i], o)) return false;
    }
    if (!count(balance::kNetMaxGems, out.gemCount)) return false;
    for (int i = 0; i < out.gemCount; ++i) getGem(r, out.gems[i], o);
    return done(r);
}

}  // namespace ls::netgame
