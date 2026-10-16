#pragma once

#include <cstddef>
#include <string>

// Private build: which special characters (models 290-299, MODEL_SPECIAL01-MODEL_SPECIAL10) a PED_SPAWN may name. Pure
// logic without ENet, so tests/special_actor_test.cpp checks it; PacketHandlers/peds.cpp applies it before the server
// creates and relays the ped.
//
// The other games load the named model with CStreaming::RequestSpecialModel (CNetworkPed's constructor), which takes
// the name's position in the IMG's extra objects directory without checking that the name was found there (0x409D10;
// gta-reversed Streaming.cpp: VERIFY(FindItem), a debug check), so only real special characters may pass. Upstream's
// check took strncasecmp's "differs" for "matches", so every name passed.
//
// The names: every one the co-op scripts load, the 023C commands (Streaming.LoadSpecialCharacter) in scm/main.txt and
// the scripts it includes, which hold no other special character loading. Each is a .dff in gta3.img that no IDE
// defines (IDE models stay out of the extra objects directory, CStreaming::LoadCdDirectory). Spelled as the client
// sends them: its RequestSpecialChar hook (client/src/Hooks/PedHooks.cpp) keeps the name upper-cased and cut at 7
// characters. A script that loads another special character needs its name here (the test compares the list with the
// scripts); until then the server window names what it refused.
namespace SpecialActorPolicy
{
// Packets::Peds::PedSpawn::specialModelName holds 8 bytes, the terminator included
constexpr size_t MAX_NAME_LENGTH = 7;

constexpr const char* ALLOWED_NAMES[] = {"ANDRE", "BBTHIN", "CAT", "CESAR", "COPGRL2", "CROGRL2", "DWAYNE", "EMMET",
    "FORELLI", "GANGRL2", "GUNGRL2", "HERN", "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL2",
    "NURGRL2", "OGLOC", "PAUL", "PULASKI", "ROSE", "RYDER2", "RYDER3", "SINDACO", "SMOKE", "SMOKEV", "SWEET", "TBONE",
    "TENPEN", "TORINO", "TRUTH", "WUZIMU", "ZERO"};

inline char ToUpperAscii(char c)
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// the length of the name in a field of fieldSize bytes; fieldSize when the field has no terminator
inline size_t BoundedLength(const char* field, size_t fieldSize)
{
    size_t length = 0;
    while (length < fieldSize && field[length] != '\0')
    {
        length++;
    }
    return length;
}

// field: the PED_SPAWN's specialModelName as received, fieldSize its size in bytes. The whole name must equal a listed
// one, ignoring case; an empty name, a field without a terminator, a prefix or a longer form of a listed name doesn't.
inline bool IsAllowed(const char* field, size_t fieldSize)
{
    const size_t length = BoundedLength(field, fieldSize);
    if (length == 0 || length == fieldSize || length > MAX_NAME_LENGTH)
    {
        return false;
    }

    for (const char* name : ALLOWED_NAMES)
    {
        size_t i = 0;
        while (i < length && name[i] != '\0' && ToUpperAscii(field[i]) == name[i])
        {
            i++;
        }
        if (i == length && name[i] == '\0')
        {
            return true;
        }
    }
    return false;
}

template <size_t N>
inline bool IsAllowed(const char (&field)[N])
{
    return IsAllowed(field, N);
}

// The field as the server window shows it: up to the terminator (or the field's end, then followed by "..."), printable
// ASCII kept and every other byte as '?', so a client can't put control sequences on the console.
inline std::string ForLog(const char* field, size_t fieldSize)
{
    const size_t length = BoundedLength(field, fieldSize);
    std::string text;
    for (size_t i = 0; i < length; i++)
    {
        const unsigned char c = static_cast<unsigned char>(field[i]);
        text += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    if (length == fieldSize)
    {
        text += "...";
    }
    return text;
}
}  // namespace SpecialActorPolicy
