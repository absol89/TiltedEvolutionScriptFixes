#include <Games/Skyrim/RespawnOverrides.h>

#include <cstring>

#include <Games/Primitives.h>

bool RespawnOverrides::GetRespawnPos(TESObjectCELL* apCell, NiPoint3& aOutPos) noexcept
{
    if (!apCell)
        return false;

    const char* pCellEditorId = apCell->GetFormEditorID();
    if (!pCellEditorId)
        return false;

//	Please add new respawns in alphabetical order and round the floats down without any decimals

//	You can find cell names on en.uesp.net/wiki if you found an interior cell with a bad respawn

//	and get suitable x, y, z start positions using console commands with format: player.getpos x

    if (std::strcmp(pCellEditorId, "QASmoke") == 0)
    {
        aOutPos = glm::vec3(363.f, 2035.f, 7152.f);
        return true;
    }
	
	if (std::strcmp(pCellEditorId, "Saarthal02") == 0)
    {
        aOutPos = glm::vec3(888.f, -269.f, -4.f);
        return true;
    }
	
	if (std::strcmp(pCellEditorId, "ShimmermistCave02") == 0)
    {
        aOutPos = glm::vec3(5009.f, 57.f, -1319.f);
        return true;
    }
	
	if (std::strcmp(pCellEditorId, "StillbornCave01") == 0)
    {
        aOutPos = glm::vec3(1501.f, -699.f, 69.f);
        return true;
    }
	
	if (std::strcmp(pCellEditorId, "SwindlersDen01") == 0)
    {
        aOutPos = glm::vec3(1470.f, -1651.f, 1969.f);
        return true;
    }

//	Add modded or non-special edition cell names in this section below, before the return false;
	
//	If the game is unable to match the cell name to this list it will not return an override pos
    return false;
}
