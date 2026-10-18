#pragma once
#include <cstdint>

class CObject;

// Private build: placeholder until OB-1 (the object mirror, docs/design/mission-objects.md 10): no object is owned, no copy.
class CMirrorObjects
{
public:
    // the host: an object of its mission the mirror sends (its object pool handle)
    static bool IsOwnedObject(int32_t hostHandle)
    {
        (void)hostHandle;
        return false;
    }
    // the other games: this game's copy of the host's object, or nullptr
    static CObject* ResolveObjectCopy(int32_t hostHandle)
    {
        (void)hostHandle;
        return nullptr;
    }
};
