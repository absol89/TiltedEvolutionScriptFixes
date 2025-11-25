#include <TiltedOnlinePCH.h>

#include <Services/NameTagService.h>

#include <Services/ImguiService.h>
#include <Services/PartyService.h>
#include <Services/OverlayService.h>

#include <Components.h>
#include <World.h>

#include <PlayerCharacter.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace
{
float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}
} // namespace

NameTagService::NameTagService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    auto& imgui = m_world.ctx().at<ImguiService>();
    m_drawConnection = imgui.OnDraw.connect<&NameTagService::OnDraw>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&NameTagService::OnUpdate>(this);
}

void NameTagService::OnDraw() noexcept
{
    auto* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
        return;

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    if (view.begin() == view.end())
        return;

    const auto& playerDirectory = m_world.GetPartyService().GetPlayers();

    auto& imguiSvc = m_world.ctx().at<ImguiService>();
    ImFont* pFont = imguiSvc.GetSkyrimFont();

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    if (pFont)
        ImGui::PushFont(pFont);

    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    const glm::vec3 localPos = {pLocalPlayer->position.x, pLocalPlayer->position.y, pLocalPlayer->position.z};
    const float baseFontSize = pFont ? pFont->FontSize : ImGui::GetFontSize();

    for (auto entity : view)
    {
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const auto& formComponent = view.get<FormIdComponent>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formComponent.Id));
        if (!pActor)
            continue;

        const ActorExtension* pExtension = pActor->GetExtension();
        if (!pExtension || pExtension->IsLocalPlayer())
            continue;

        if (!ShouldRenderTag(playerComponent.Id))
            continue;

        auto nameIt = playerDirectory.find(playerComponent.Id);
        std::string fallbackName;
        std::string_view displayName;
        if (nameIt != playerDirectory.end() && !nameIt->second.Name.empty())
            displayName = nameIt->second.Name.c_str();
        else
        {
            fallbackName = fmt::format("#{}", playerComponent.Id);
            displayName = fallbackName;
        }

        const NiPoint3 anchor = BuildAnchorPoint(pActor);

        ImVec2 screenPos;
        float depth = 0.f;
        if (!ProjectWorldPoint(anchor, screenPos, depth, viewport))
            continue;

        const glm::vec3 remotePos = {anchor.x, anchor.y, anchor.z};
        const float distance = glm::distance(localPos, remotePos);
        if (distance > m_style.MaxDistance)
            continue;

        const float distanceRatio = distance / std::max(m_style.MaxDistance, 1.f);
        const float scale = std::lerp(m_style.MaxScale, m_style.MinScale, Clamp01(distanceRatio));

        float alpha = 1.f;
        if (distance > m_style.FadeDistance)
        {
            const float fadeSpan = std::max(m_style.MaxDistance - m_style.FadeDistance, 1.f);
            alpha = 1.f - Clamp01((distance - m_style.FadeDistance) / fadeSpan);
        }

        const ImVec2 textSize = ImGui::CalcTextSize(displayName.data(), displayName.data() + displayName.size());
        const ImVec2 labelSize = ImVec2(textSize.x * scale, textSize.y * scale);
        const ImVec2 halfSize = ImVec2(labelSize.x * 0.5f, labelSize.y * 0.5f);
        const ImVec2 padding = ImVec2(m_style.PaddingX, m_style.PaddingY);
        ImVec2 topLeft = ImVec2(screenPos.x - (halfSize.x + padding.x), screenPos.y - (halfSize.y + padding.y));
        ImVec2 bottomRight = ImVec2(screenPos.x + (halfSize.x + padding.x), screenPos.y + (halfSize.y + padding.y));

        // Light 3D cue: slight horizontal offset based on how much the actor looks at the camera.
        const glm::vec3 actorForward = {std::sin(pActor->rotation.z), std::cos(pActor->rotation.z), 0.f};
        glm::vec3 toCameraVec = localPos - remotePos;
        if (glm::length2(toCameraVec) <= std::numeric_limits<float>::epsilon())
            toCameraVec = {0.f, 0.f, 1.f};
        const glm::vec3 toCamera = glm::normalize(toCameraVec);
        const glm::vec3 forwardNorm = glm::normalize(actorForward);
        const float facing = Clamp01((glm::dot(forwardNorm, toCamera) + 1.f) * 0.5f);
        const float skew = (1.f - facing) * (padding.x * 0.35f);
        topLeft.x -= skew;
        bottomRight.x -= skew;
        screenPos.x -= skew;

        const ImU32 bgColor = ColorWithAlpha(m_style.BackgroundColor, alpha);
        drawList->AddRectFilled(topLeft, bottomRight, bgColor, m_style.CornerRounding);

        const ImU32 borderColor = ColorWithAlpha(m_style.BorderColor, alpha);
        drawList->AddRect(topLeft, bottomRight, borderColor, m_style.CornerRounding);

        const ImVec2 textStart = ImVec2(screenPos.x - halfSize.x, screenPos.y - halfSize.y);
        const ImU32 textColor = ColorWithAlpha(m_style.TextColor, alpha);
        drawList->AddText(pFont ? pFont : ImGui::GetFont(), baseFontSize * scale, textStart, textColor, displayName.data(), displayName.data() + displayName.size());
    }

    if (pFont)
        ImGui::PopFont();
}

void NameTagService::OnUpdate(const UpdateEvent&) noexcept
{
    auto* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
    {
        m_visibility.clear();
        return;
    }

    const uint64_t tick = m_world.GetTick();
    size_t total = 0;
    size_t resolved = 0;
    size_t visibleCount = 0;

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    for (auto entity : view)
    {
        ++total;
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const auto& formComponent = view.get<FormIdComponent>(entity);
        Actor* pActor = Cast<Actor>(TESForm::GetById(formComponent.Id));
        if (!pActor)
            continue;
        ++resolved;

        const float dx = pLocalPlayer->position.x - pActor->position.x;
        const float dy = pLocalPlayer->position.y - pActor->position.y;
        const float dz = pLocalPlayer->position.z - pActor->position.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        bool isVisible = ComputeLineOfSight(pLocalPlayer, pActor);
        if (!isVisible && distance <= m_style.NearBypassDistance)
            isVisible = true;

        VisibilityInfo& info = m_visibility[playerComponent.Id];
        info.Visible = isVisible;
        info.Tick = tick;
        if (info.Visible)
            ++visibleCount;
    }

    constexpr uint64_t kMaxAgeMs = 2000;
    for (auto it = m_visibility.begin(); it != m_visibility.end();)
    {
        if (tick - it->second.Tick > kMaxAgeMs)
            it = m_visibility.erase(it);
        else
            ++it;
    }

    static uint64_t s_lastLogTick = 0;
    if (tick - s_lastLogTick > 1000)
    {
        spdlog::info("[NameTagService] update candidates={} resolved={} visible={}", total, resolved, visibleCount);
        s_lastLogTick = tick;
    }
}

bool NameTagService::ShouldRenderTag(uint32_t aPlayerId) const noexcept
{
    const uint64_t tick = m_world.GetTick();
    auto it = m_visibility.find(aPlayerId);
    if (it == m_visibility.end())
        return true; // not evaluated yet, optimistically show tag

    constexpr uint64_t kStaleThresholdMs = 1500;
    if (tick - it->second.Tick > kStaleThresholdMs)
        return true;

    return it->second.Visible;
}

bool NameTagService::ComputeLineOfSight(PlayerCharacter* apLocalPlayer, Actor* apRemoteActor) const noexcept
{
    if (!apLocalPlayer || !apRemoteActor)
        return false;

    if (apRemoteActor->IsDead())
        return false;

    if (apLocalPlayer->HasLineOfSight(apRemoteActor))
        return true;

    if (apRemoteActor->HasLineOfSight(apLocalPlayer))
        return true;

    return false;
}

bool NameTagService::ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept
{
    NiPoint3 projected{};
    if (!HUDMenuUtils::WorldPtToScreenPt3(aWorldPoint, projected))
        return false;

    if (projected.z <= m_style.VisibilityEpsilon || projected.z >= 1.f)
        return false;

    aScreenPos.x = projected.x * aViewport.x;
    aScreenPos.y = (1.f - projected.y) * aViewport.y;
    aDepth = projected.z;

    return true;
}

NiPoint3 NameTagService::BuildAnchorPoint(Actor* apActor) const noexcept
{
    NiPoint3 anchor = apActor->position;
    anchor.z += apActor->GetHeight() + m_style.VerticalOffset;

    const float yaw = apActor->rotation.z;
    anchor.x += std::sin(yaw) * m_style.ForwardOffset;
    anchor.y += std::cos(yaw) * m_style.ForwardOffset;

    return anchor;
}

ImU32 NameTagService::ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept
{
    ImVec4 adjusted = aColor;
    adjusted.w *= Clamp01(aAlpha);
    return ImGui::ColorConvertFloat4ToU32(adjusted);
}
