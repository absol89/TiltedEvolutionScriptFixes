#include <Services/PluginService.h>

// Looked up by SKSE plugins with GetProcAddress(GetModuleHandleW(nullptr), SkyrimTogetherAPI::kRequestPluginAPIName).
// ExeLoader::Load restores the export directory so the main module still exposes it.
extern "C" __declspec(dllexport) void* SkyrimTogetherRequestPluginAPI(SkyrimTogetherAPI::InterfaceVersion aVersion)
{
    if (aVersion != SkyrimTogetherAPI::InterfaceVersion::V1)
        return nullptr;

    return static_cast<SkyrimTogetherAPI::IVSkyrimTogether1*>(&PluginService::Get());
}

// Keep scope registration out of the V1 vtable so existing compiled plugins retain their ABI.
extern "C" __declspec(dllexport) SkyrimTogetherAPI::ChannelHandle SkyrimTogetherRegisterScopedChannel(
    const char* apName, SkyrimTogetherAPI::ChannelScope aScope, SkyrimTogetherAPI::MessageCallback aOnMessage,
    SkyrimTogetherAPI::PartyChangedCallback aOnPeersChanged)
{
    return PluginService::Get().RegisterScopedChannel(apName, aScope, aOnMessage, aOnPeersChanged);
}

// Also outside the V1 vtable, for the same reason.
extern "C" __declspec(dllexport) uint32_t SkyrimTogetherGetPartyLeaderId()
{
    return PluginService::Get().GetPartyLeaderId();
}
