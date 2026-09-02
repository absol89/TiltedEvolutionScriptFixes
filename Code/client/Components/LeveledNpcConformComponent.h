#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

// Marks the one-frame disable used to rebuild a leveled actor's 3D and
// animation graph. Removal listeners must not treat that disable as an unload.
struct LeveledNpcConformComponent
{
};
