#include <Games/Skyrim/RespawnOverrides.h>

#include <cstring>

#include <Primitives.h>

bool RespawnOverrides::GetRespawnPos(const TESObjectCELL* apCell, NiPoint3& aOutPos) noexcept
{
    if (!apCell)
        return false;

    const char* pCellEditorId = apCell->GetFormEditorID();
    if (!pCellEditorId)
        return false;

    if (std::strcmp(pCellEditorId, "Saarthal02") == 0)
    {
        aOutPos = glm::vec3(786.f, -286.f, 8.f);
        return true;
    }

    if (std::strcmp(pCellEditorId, "QASmoke") == 0)
    {
        aOutPos = glm::vec3(363.f, 2035.f, 7152.f);
        return true;
    }

    return false;
}
