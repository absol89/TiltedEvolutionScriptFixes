#pragma once

#include <unordered_map>
#include <string>

#include <imgui.h>

#include <Events/UpdateEvent.h>

struct World;
struct PlayerCharacter;
struct Actor;
struct NiPoint3;
struct ImguiService;

/**
 * @brief Draws 3D-aware name tags above party members' heads.
 *
 * Minimal port: character name + level, no avatar. Tags only render for
 * party members within range and with line-of-sight (or very close).
 */
struct NameTagService
{
    NameTagService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~NameTagService() noexcept = default;

    TP_NOCOPYMOVE(NameTagService);

    // Tunables (kept few on purpose).
    static constexpr float kMaxDistance = 3000.f;
    static constexpr float kFadeDistance = 2000.f;
    static constexpr float kNearBypassDistance = 600.f;
    static constexpr float kVerticalOffset = 24.f;
    static constexpr float kForwardOffset = 10.f;
    static constexpr float kScale = 1.f;
    static constexpr float kVisibilityEpsilon = 1e-3f;

private:
    struct VisibilityInfo
    {
        bool Visible = true;
        uint64_t Tick = 0;
    };

    void OnDraw() noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    [[nodiscard]] bool ShouldRenderTag(uint32_t aPlayerId) const noexcept;
    [[nodiscard]] bool ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept;
    [[nodiscard]] NiPoint3 BuildAnchorPoint(Actor* apActor) const noexcept;
    [[nodiscard]] static ImU32 ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept;

    World& m_world;

    std::unordered_map<uint32_t, VisibilityInfo> m_visibility;

    entt::scoped_connection m_drawConnection;
    entt::scoped_connection m_updateConnection;
};
