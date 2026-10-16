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
    // gambling skill. Respect is left out: it comes from the host's missions and territories.
    static constexpr uint16_t FLOAT_STAT_IDS[] = {
        21, 22, 23, 24, 25, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81};
    // Personal int stats: driving, flying, lung capacity, bike and cycling skill
    static constexpr uint16_t INT_STAT_IDS[] = {160, 223, 225, 229, 230};
    static constexpr int NUM_FLOAT_STATS = static_cast<int>(sizeof(FLOAT_STAT_IDS) / sizeof(FLOAT_STAT_IDS[0]));
    static constexpr int NUM_INT_STATS = static_cast<int>(sizeof(INT_STAT_IDS) / sizeof(INT_STAT_IDS[0]));

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
            freeHealthCare != other.freeHealthCare)
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
