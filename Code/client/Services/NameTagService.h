#pragma once

#include <unordered_map>

#include <entt/entt.hpp>
#include <imgui.h>

#include <Events/UpdateEvent.h>

struct World;
struct PlayerCharacter;
struct Actor;
struct NiPoint3;

/**
 * @brief Draws 3D-aware name tags above remote players.
 *
 * The service is intentionally data-driven via Style so that future UI/UX
 * customisation can plug into it without having to touch the rendering logic.
 */
struct NameTagService
{
    struct Style
    {
        ImVec4 BackgroundColor{0.f, 0.f, 0.f, 0.65f};
        ImVec4 BorderColor{1.f, 1.f, 1.f, 0.35f};
        ImVec4 TextColor{0.95f, 0.95f, 0.95f, 1.f};
        float VerticalOffset = 24.f;
        float ForwardOffset = 10.f;
        float MinScale = 0.7f;
        float MaxScale = 1.35f;
        float MaxDistance = 3500.f;
        float FadeDistance = 2500.f;
        float PaddingX = 14.f;
        float PaddingY = 6.f;
        float CornerRounding = 6.f;
        float VisibilityEpsilon = 1e-3f;
        float NearBypassDistance = 600.f;
    };

    NameTagService(World& aWorld, [[maybe_unused]] entt::dispatcher& aDispatcher) noexcept;
    ~NameTagService() noexcept = default;

    TP_NOCOPYMOVE(NameTagService);

    [[nodiscard]] Style& GetStyle() noexcept { return m_style; }
    void SetStyle(const Style& aStyle) noexcept { m_style = aStyle; }

private:
    struct VisibilityInfo
    {
        bool Visible = true;
        uint64_t Tick = 0;
    };

    void OnDraw() noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    [[nodiscard]] bool ShouldRenderTag(uint32_t aPlayerId) const noexcept;
    [[nodiscard]] bool ComputeLineOfSight(PlayerCharacter* apLocalPlayer, Actor* apRemoteActor) const noexcept;
    [[nodiscard]] bool ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept;
    [[nodiscard]] NiPoint3 BuildAnchorPoint(Actor* apActor) const noexcept;
    [[nodiscard]] static ImU32 ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept;

    World& m_world;
    Style m_style{};

    std::unordered_map<uint32_t, VisibilityInfo> m_visibility;

    entt::scoped_connection m_drawConnection;
    entt::scoped_connection m_updateConnection;
};
