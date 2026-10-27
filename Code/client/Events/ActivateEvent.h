#pragma once

struct TESObjectREFR;
struct Actor;
struct TESBoundObject;

/**
 * @brief In-game activation (i.e. interacting with an object).
 *
 * Handled after a queue, when the game may have deleted the object or the activator: check that ObjectId and
 * ActivatorId still name them (TESForm::GetById) before using the pointers.
 */
struct ActivateEvent
{
    ActivateEvent(TESObjectREFR* apObject, Actor* apActivator, TESBoundObject* apObjectToGet, int32_t aCount, bool aDefaultProcessing, uint8_t aUnk1, TESObjectREFR::OpenState aPreActivationOpenState, bool aActivateFlag = false)
        : pObject(apObject)
        , pActivator(apActivator)
        , ObjectId(apObject ? apObject->formID : 0)
        , ActivatorId(apActivator ? apActivator->formID : 0)
        , pObjectToGet(apObjectToGet)
        , Count(aCount)
        , DefaultProcessing(aDefaultProcessing)
        , Unk1(aUnk1)
        , PreActivationOpenState(aPreActivationOpenState)
        , ActivateFlag(aActivateFlag)
    {
    }

    TESObjectREFR* pObject;
    Actor* pActivator;
    uint32_t ObjectId;
    uint32_t ActivatorId;
    TESBoundObject* pObjectToGet;
    int32_t Count;
    bool DefaultProcessing;
    uint8_t Unk1;
    TESObjectREFR::OpenState PreActivationOpenState;
    bool ActivateFlag;
};
