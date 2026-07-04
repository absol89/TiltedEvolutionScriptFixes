#pragma once

#include <Forms/TESActorBase.h>

#include <Components/TESRaceForm.h>
#include <Components/BGSOverridePackCollection.h>

struct BGSColorForm;
struct BGSTextureSet;
struct TESClass;
struct TESCombatStyle;
struct TESObjectARMO;
struct BGSOutfit;
struct BGSHeadPart;
struct BGSRelationship;

struct TESNPC : TESActorBase
{
    static constexpr FormType Type = FormType::Npc;

    static TESNPC* Create(const String& acBuffer, uint32_t aChangeFlags) noexcept;

    TESNPC* GetTemplateBase() const noexcept
    {
        TESNPC* pTemplate = npcTemplate;

        while (pTemplate && pTemplate->IsTemporary())
            pTemplate = pTemplate->npcTemplate;

        return pTemplate;
    }

    static uint32_t GetLeveledPickFormId(uint32_t aTempNpcFormId) noexcept;

    // Best-effort pick recovery for temp bases the hooked resolver never saw
    // (some engine spawn paths bypass fn 14375, e.g. live cell attach): the
    // first static NPC in the template chain is the pick - unless it is the
    // placed shell itself, recognizable by templating off a leveled list.
    // Chain entries can be TESLevCharacter posing as TESNPC*, whose layout is
    // too small to hold npcTemplate - hence the formType guards.
    TESNPC* GetLeveledPick() const noexcept
    {
        TESNPC* pTemplate = npcTemplate;

        while (pTemplate && pTemplate->formType == FormType::Npc && pTemplate->IsTemporary())
            pTemplate = pTemplate->npcTemplate;

        if (!pTemplate || pTemplate->formType != FormType::Npc)
            return nullptr;

        TESNPC* pShellTemplate = pTemplate->npcTemplate;
        if (pShellTemplate && pShellTemplate->formType == FormType::LeveledCharacter)
            return nullptr;

        return pTemplate;
    }

    struct FaceMorphs
    {
        float option[19];
        uint32_t presets[4];

        void CopyFrom(const FaceMorphs& acRhs)
        {
            std::copy(std::begin(acRhs.option), std::end(acRhs.option), std::begin(option));
            std::copy(std::begin(acRhs.presets), std::end(acRhs.presets), std::begin(presets));
        }
    };

    struct HeadData
    {
        BGSColorForm* hairColor;
        BGSTextureSet* headTexture;
    };

    TESRaceForm raceForm;
    BGSOverridePackCollection overridePacks;
    void* unkDC;
    uint8_t unk0E0[0x24];
    uint8_t pad1B4[0x6];
    uint16_t unk10A;
    TESClass* npcClass;

    HeadData* headData;
    uintptr_t unk114;
    TESCombatStyle* combatStyle;
    size_t unk11C;
    TESRace* overlayRace;
    TESNPC* npcTemplate;
    float height;
    float weight;
    uintptr_t unk130;
    BSFixedString shortName;
    TESObjectARMO* farArmo;
    BGSOutfit* outfits[2];
    uintptr_t unk144;
    TESFaction* faction;

    BGSHeadPart** headparts;
    uint8_t headpartsCount;

#if TP_PLATFORM_64
    uint8_t pad241[5];
#else
    uint8_t pad151[3];
#endif

    struct Color
    {
        uint8_t red, green, blue;
    } color;

    GameArray<BGSRelationship*>* relationships;
    FaceMorphs* faceMorphs;
    uintptr_t unk160;

    BGSHeadPart* GetHeadPart(uint32_t aType);
    void Serialize(String* apSaveBuffer) const noexcept;
    void Deserialize(const String& acBuffer, uint32_t aChangeFlags) noexcept;
    void Initialize() noexcept;
};

static_assert(offsetof(TESNPC, npcClass) == 0x1C0);
static_assert(offsetof(TESNPC, color) == 0x246);
static_assert(offsetof(TESNPC, relationships) == 0x250);
