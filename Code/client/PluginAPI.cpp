#include <Services/PluginService.h>

// Looked up by SKSE plugins with GetProcAddress(GetModuleHandleW(nullptr), SkyrimTogetherAPI::kRequestPluginAPIName).
// ExeLoader::Load restores the export directory so the main module still exposes it. Not exported as
// "RequestPluginAPI" any more (see kRequestPluginAPIName): Floating Subtitles, finding no NPCs Names Distributor, took
// this interface for that mod's and called its destructor, freeing the service while the World update still used it.
extern "C" __declspec(dllexport) void* SkyrimTogetherRequestPluginAPI(SkyrimTogetherAPI::InterfaceVersion aVersion)
{
    switch (aVersion)
    {
    case SkyrimTogetherAPI::InterfaceVersion::V1: return static_cast<SkyrimTogetherAPI::IVSkyrimTogether1*>(&PluginService::Get());
    case SkyrimTogetherAPI::InterfaceVersion::V2: return static_cast<SkyrimTogetherAPI::IVSkyrimTogether2*>(&PluginService::Get());
    }

    return nullptr;
}
