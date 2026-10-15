#pragma once

struct tTagDesc 
{
    CEntity* m_pEntity;
    uint8_t m_nAlpha;
    char _pad[3];
};

class CTagManager
{
public:
	static inline tTagDesc(&ms_tagDesc)[150] = *(tTagDesc(*)[150])0xA9A8C0;
    static inline int32_t& ms_numTags = *(int32_t*)0xA9AD70;
    static inline int32_t& ms_numTagged = *(int32_t*)0xA9AD74;

    // private build: a tag counts as tagged (ms_numTagged, GET_PERCENTAGE_TAGGED_IN_AREA) when its alpha is
    // above this, see `mov dl,0E4h` at 0x49CF04 (SetAlpha) and `cmp al,0E4h` at 0x49D127
    // (GetPercentageTaggedInArea); gta-reversed TagManager.h ALPHA_TAGGED
    static constexpr uint8_t ALPHA_TAGGED = 228;

    static void SetAlpha(CEntity* entity, uint8_t ucAlpha);
    static bool IsTag(const CEntity* entity);
    static CVector* GetTagPos(int32_t i);
};

class CTagSync
{
public:
    static void SyncCurrentState();
};