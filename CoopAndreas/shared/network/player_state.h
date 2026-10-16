#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "serialize.h"

// Private build: a player's personal state for co-op saves (client/src/CPlayerStateSave.h). Plain data without game
// types, so the server and the standalone policy test (tests/player_state_test.cpp) compile it too.

// The nickname rule Packets::System::PlayerConnected applies when it reads a name (shared/network/packets/system.h);
// the server and the host know a player by this form of the coopandreas.ini nickname.
inline bool IsAllowedPlayerNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '[' ||
           c == ']';
}

inline void SanitizePlayerName(const char* name, char (&out)[Config::MAX_NICKNAME_LENGTH + 1])
{
    memset(out, 0, sizeof(out));
    for (int i = 0; name != nullptr && i < Config::MAX_NICKNAME_LENGTH && name[i] != '\0'; i++)
    {
        out[i] = IsAllowedPlayerNameChar(name[i]) ? name[i] : '_';
    }
}

// 1 to MAX_NICKNAME_LENGTH allowed characters
inline bool IsValidPlayerName(const char* name)
{
    if (name == nullptr || name[0] == '\0')
    {
        return false;
    }
    for (int i = 0; i <= Config::MAX_NICKNAME_LENGTH; i++)
    {
        if (name[i] == '\0')
        {
            return true;
        }
        if (i == Config::MAX_NICKNAME_LENGTH || !IsAllowedPlayerNameChar(name[i]))
        {
            return false;
        }
    }
    return false;
}

// FNV-1a, for the save file fingerprint and the sidecar checksum (not a security measure)
inline uint64_t Fnv1a64(const void* data, size_t size, uint64_t hash = 14695981039346656037ull)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

inline uint32_t Fnv1a32(const void* data, size_t size, uint32_t hash = 2166136261u)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

// What a co-op save keeps of a player other than the host. The values are gta_sa.exe 1.0 US ones; the client reads and
// writes them (CPlayerStateSave.cpp), this struct only carries and checks them.
struct SPlayerState
{
    static constexpr int NUM_WEAPON_SLOTS = 13;      // CPed::m_aWeapons
    static constexpr int MAX_WEAPON_TYPE = 46;       // WEAPON_PARACHUTE, the last type a ped carries in a slot
    static constexpr int32_t MAX_AMMO = 99999;       // CPed::GiveWeapon 0x5E6080 caps the total (cmp eax,1869Fh)
    static constexpr int32_t MAX_MONEY = 999999999;
    static constexpr float MAX_HEALTH_OR_ARMOUR = 1000.0f;
    // the abilities below stop at 1000 (gta-reversed CStats::IsStatCapped; gambling isn't in that list, kept to 1000 too)
    static constexpr float MAX_STAT_VALUE = 1000.0f;
    static constexpr int FIGHTING_STYLE_FIRST = 4;   // eFightingStyle STYLE_STANDARD
    static constexpr int FIGHTING_STYLE_LAST = 16;   // eFightingStyle STYLE_ELBOWS
    static constexpr int NUM_CLOTHES_MODELS = 10;    // CPedClothesDesc::m_anModelKeys
    static constexpr int NUM_CLOTHES_TEXTURES = 18;  // CPedClothesDesc::m_anTextureKeys

    // Personal float stats (eStats ids below 0x52, CStats::GetStatValue 0x558E40): fat, stamina, muscle, max health,
    // sex appeal, the 11 weapon skills (69-79), appearance (follows the clothes worn, CShopping::IncrementStat) and
    // gambling skill. The respect stats are separate (below).
    static constexpr uint16_t FLOAT_STAT_IDS[] = {
        21, 22, 23, 24, 25, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81};
    // Personal int stats: driving, flying, lung capacity, bike and cycling skill
    static constexpr uint16_t INT_STAT_IDS[] = {160, 223, 225, 229, 230};
    static constexpr int NUM_FLOAT_STATS = static_cast<int>(sizeof(FLOAT_STAT_IDS) / sizeof(FLOAT_STAT_IDS[0]));
    static constexpr int NUM_INT_STATS = static_cast<int>(sizeof(INT_STAT_IDS) / sizeof(INT_STAT_IDS[0]));

    // Respect. The game computes the total (float stat 64) from components in every frame it isn't paused:
    // CStats::UpdateRespectStat (0x55BC50, the first call of CGameLogic::Update 0x442AD0) adds
    //   mission respect (224) * 1000 / max(mission respect total (228), 1) * 0.36, respect (68) * 0.4,
    //   girlfriend respect (65), clothes respect (66) and muscle (23) * 0.03 each, min(money * 1e-7, 1) * 50,
    //   max(CGangWars::TerritoryUnderControlPercentage - 0.2, 0) * 62.5 and CTagManager::GetPercentageTagged() * 0.5,
    // at least 0 and 1000 under the max respect cheat, and stores the total whenever its whole part changes.
    // Kept here, as the player earns them in their own game: mission respect (award_player_mission_respect 0998, with
    // the host's mission-pass rewards the mission mirror replays), respect (+1.5 per cop or dealer and +0.75 per enemy
    // gang member killed, -5 per Grove member, -2 per recruit someone else kills: CDarkel 0x43DCD0 / 0x43D210; gang war
    // results: CGangWars 0x446400 / 0x446610) and clothes respect (follows the clothes bought, CShopping::Buy 0x49BF70,
    // like appearance). Muscle and money are kept above. Not kept: the girlfriends' respect (GFAGNT keeps it in step
    // with MAIN's girlfriend globals, which no player state carries), territories and tags (the host's world), the
    // mission respect total (the constant 1339 MAIN sets in every game, 0997) and the cheat. The total follows.
    static constexpr uint16_t STAT_ID_CLOTHES_RESPECT = 66;   // float, capped at 1000 (CStats::IsStatCapped 0x559630)
    static constexpr uint16_t STAT_ID_RESPECT = 68;           // float, not capped
    static constexpr uint16_t STAT_ID_MISSION_RESPECT = 224;  // int, not capped; the whole story awards 1339
    static constexpr float MAX_RESPECT = 100000.0f;           // sanity limits for the uncapped two
    static constexpr int32_t MAX_MISSION_RESPECT = 100000;

    // Serialize layouts: LAYOUT_V1 is the state as CAPS v1 files hold it (without respect, read only), LAYOUT_V2 the
    // packets' and CAPS v2 files' layout
    static constexpr int LAYOUT_V1 = 1;
    static constexpr int LAYOUT_V2 = 2;
    static constexpr int LAYOUT_CURRENT = LAYOUT_V2;

    struct SWeapon
    {
        uint8_t type = 0;  // eWeaponType, 0 = empty slot
        uint32_t totalAmmo = 0;
        uint32_t ammoInClip = 0;
    };

    SWeapon weapons[NUM_WEAPON_SLOTS]{};
    uint8_t activeSlot = 0;
    float health = 100.0f;
    float armour = 0.0f;
    int32_t money = 0;  // CPlayerInfo::m_nMoney, the displayed money follows it
    uint8_t fightingStyle = FIGHTING_STYLE_FIRST;
    uint8_t allowedAttackMoves = 0;
    // CPlayerInfo perks
    uint8_t maxHealth = 100;
    uint8_t maxArmour = 100;
    bool neverGetsTired = false;
    bool fastReload = false;
    bool fireProof = false;
    bool getOutOfJailFree = false;
    bool freeHealthCare = false;
    float floatStats[NUM_FLOAT_STATS]{};
    int32_t intStats[NUM_INT_STATS]{};
    // CPedClothesDesc keys (clothes, haircut, tattoos); fat and muscle come from the stats when the player is rebuilt
    uint32_t clothesModelKeys[NUM_CLOTHES_MODELS]{};
    uint32_t clothesTextureKeys[NUM_CLOTHES_TEXTURES]{};
    // false only for a state read from a CAPS v1 file (the respect values are 0 then): a restore of it leaves the
    // player's respect as it is
    bool hasRespect = true;
    float respect = 0.0f;         // STAT_ID_RESPECT
    float clothesRespect = 0.0f;  // STAT_ID_CLOTHES_RESPECT
    int32_t missionRespect = 0;   // STAT_ID_MISSION_RESPECT

    // 19-21 (rocket, heat-seeking rocket, freefall bomb) are projectiles, never in a ped's weapon slots
    static bool IsGivableWeaponType(int type)
    {
        return type >= 1 && type <= MAX_WEAPON_TYPE && (type < 19 || type > 21);
    }

    static bool IsFiniteInRange(float value, float min, float max)
    {
        return std::isfinite(value) && value >= min && value <= max;
    }

    bool IsValid() const
    {
        for (const SWeapon& weapon : weapons)
        {
            if (weapon.type == 0)
            {
                if (weapon.totalAmmo != 0 || weapon.ammoInClip != 0)
                {
                    return false;
                }
                continue;
            }
            if (!IsGivableWeaponType(weapon.type) || weapon.totalAmmo > static_cast<uint32_t>(MAX_AMMO) ||
                weapon.ammoInClip > weapon.totalAmmo)
            {
                return false;
            }
        }
        if (activeSlot >= NUM_WEAPON_SLOTS)
        {
            return false;
        }
        if (!IsFiniteInRange(health, 0.0f, MAX_HEALTH_OR_ARMOUR) || health <= 0.0f ||
            !IsFiniteInRange(armour, 0.0f, MAX_HEALTH_OR_ARMOUR))
        {
            return false;
        }
        if (money < -MAX_MONEY || money > MAX_MONEY)
        {
            return false;
        }
        if (fightingStyle < FIGHTING_STYLE_FIRST || fightingStyle > FIGHTING_STYLE_LAST || maxHealth == 0)
        {
            return false;
        }
        for (float value : floatStats)
        {
            if (!IsFiniteInRange(value, 0.0f, MAX_STAT_VALUE))
            {
                return false;
            }
        }
        for (int32_t value : intStats)
        {
            if (value < 0 || value > static_cast<int32_t>(MAX_STAT_VALUE))
            {
                return false;
            }
        }
        if (hasRespect)
        {
            if (!IsFiniteInRange(respect, 0.0f, MAX_RESPECT) ||
                !IsFiniteInRange(clothesRespect, 0.0f, MAX_STAT_VALUE) || missionRespect < 0 ||
                missionRespect > MAX_MISSION_RESPECT)
            {
                return false;
            }
        }
        else if (respect != 0.0f || clothesRespect != 0.0f || missionRespect != 0)
        {
            return false;  // one form for "not known"
        }
        return true;
    }

    bool operator==(const SPlayerState& other) const
    {
        for (int i = 0; i < NUM_WEAPON_SLOTS; i++)
        {
            if (weapons[i].type != other.weapons[i].type || weapons[i].totalAmmo != other.weapons[i].totalAmmo ||
                weapons[i].ammoInClip != other.weapons[i].ammoInClip)
            {
                return false;
            }
        }
        if (activeSlot != other.activeSlot || health != other.health || armour != other.armour ||
            money != other.money || fightingStyle != other.fightingStyle ||
            allowedAttackMoves != other.allowedAttackMoves || maxHealth != other.maxHealth ||
            maxArmour != other.maxArmour || neverGetsTired != other.neverGetsTired || fastReload != other.fastReload ||
            fireProof != other.fireProof || getOutOfJailFree != other.getOutOfJailFree ||
            freeHealthCare != other.freeHealthCare || hasRespect != other.hasRespect || respect != other.respect ||
            clothesRespect != other.clothesRespect || missionRespect != other.missionRespect)
        {
            return false;
        }
        return memcmp(floatStats, other.floatStats, sizeof(floatStats)) == 0 &&
               memcmp(intStats, other.intStats, sizeof(intStats)) == 0 &&
               memcmp(clothesModelKeys, other.clothesModelKeys, sizeof(clothesModelKeys)) == 0 &&
               memcmp(clothesTextureKeys, other.clothesTextureKeys, sizeof(clothesTextureKeys)) == 0;
    }

    bool operator!=(const SPlayerState& other) const { return !(*this == other); }

    // Bit-packed for packets and for the sidecar file. Nothing invalid is written, and reading fails on anything
    // invalid, so a handler or the sidecar reader only ever sees a state that passed IsValid().
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        return SerializeLayout(stream, LAYOUT_CURRENT);
    }

    // An older layout is only read (from an older sidecar file): its missing fields read as "not known"
    template <typename Stream>
    bool SerializeLayout(Stream& stream, int layout)
    {
        if (layout < LAYOUT_V1 || layout > LAYOUT_CURRENT || (Stream::IsWriting && layout != LAYOUT_CURRENT))
        {
            return false;
        }
        if (Stream::IsWriting && !IsValid())
        {
            return false;
        }

        for (SWeapon& weapon : weapons)
        {
            if (!SerializeRanged(stream, weapon.type, 0, MAX_WEAPON_TYPE))
            {
                return false;
            }
            if (weapon.type != 0)
            {
                if (!SerializeRanged(stream, weapon.totalAmmo, 0, MAX_AMMO) ||
                    !SerializeRanged(stream, weapon.ammoInClip, 0, MAX_AMMO))
                {
                    return false;
                }
            }
            else if (Stream::IsReading)
            {
                weapon.totalAmmo = 0;
                weapon.ammoInClip = 0;
            }
        }
        if (!SerializeRanged(stream, activeSlot, 0, NUM_WEAPON_SLOTS - 1))
        {
            return false;
        }
        serialize_float(stream, health);
        serialize_float(stream, armour);
        if (!SerializeBitsOf(stream, money, 32) ||
            !SerializeRanged(stream, fightingStyle, FIGHTING_STYLE_FIRST, FIGHTING_STYLE_LAST) ||
            !SerializeBitsOf(stream, allowedAttackMoves, 8) || !SerializeBitsOf(stream, maxHealth, 8) ||
            !SerializeBitsOf(stream, maxArmour, 8))
        {
            return false;
        }
        serialize_bool(stream, neverGetsTired);
        serialize_bool(stream, fastReload);
        serialize_bool(stream, fireProof);
        serialize_bool(stream, getOutOfJailFree);
        serialize_bool(stream, freeHealthCare);
        for (float& value : floatStats)
        {
            serialize_float(stream, value);
        }
        for (int32_t& value : intStats)
        {
            if (!SerializeRanged(stream, value, 0, static_cast<int32_t>(MAX_STAT_VALUE)))
            {
                return false;
            }
        }
        for (uint32_t& key : clothesModelKeys)
        {
            if (!SerializeBitsOf(stream, key, 32))
            {
                return false;
            }
        }
        for (uint32_t& key : clothesTextureKeys)
        {
            if (!SerializeBitsOf(stream, key, 32))
            {
                return false;
            }
        }

        // LAYOUT_V2: the respect block
        if (layout >= LAYOUT_V2)
        {
            serialize_bool(stream, hasRespect);
        }
        else if (Stream::IsReading)
        {
            hasRespect = false;
        }
        if (hasRespect)
        {
            serialize_float(stream, respect);
            serialize_float(stream, clothesRespect);
            if (!SerializeRanged(stream, missionRespect, 0, MAX_MISSION_RESPECT))
            {
                return false;
            }
        }
        else if (Stream::IsReading)
        {
            respect = 0.0f;
            clothesRespect = 0.0f;
            missionRespect = 0;
        }

        if (Stream::IsReading && !IsValid())
        {
            return false;
        }
        return true;
    }

private:
    template <typename Stream, typename T>
    static bool SerializeRanged(Stream& stream, T& field, int32_t min, int32_t max)
    {
        int32_t value = static_cast<int32_t>(field);
        if (Stream::IsWriting && (value < min || value > max))
        {
            return false;  // the bit packer would write it truncated
        }
        serialize_int(stream, value, min, max);
        if (Stream::IsReading)
        {
            field = static_cast<T>(value);
        }
        return true;
    }

    template <typename Stream, typename T>
    static bool SerializeBitsOf(Stream& stream, T& field, int bits)
    {
        uint64_t value = static_cast<uint32_t>(field);
        serialize_bits(stream, value, bits);
        if (Stream::IsReading)
        {
            field = static_cast<T>(static_cast<uint32_t>(value));
        }
        return true;
    }
};
