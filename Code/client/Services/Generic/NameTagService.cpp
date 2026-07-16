#include <TiltedOnlinePCH.h>

#include <Services/NameTagService.h>

#include <Services/ImguiService.h>
#include <Services/PartyService.h>

#include <World.h>

#include <PlayerCharacter.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Skyrim/Misc/BSFixedString.h>

#include <fmt/format.h>

namespace
{
float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}

float SmoothStep(float aEdge0, float aEdge1, float aValue) noexcept
{
    if (aEdge0 == aEdge1)
        return aValue >= aEdge1 ? 1.f : 0.f;

    const float t = Clamp01((aValue - aEdge0) / (aEdge1 - aEdge0));
    return t * t * (3.f - 2.f * t);
}
} // namespace

NameTagService::NameTagService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    auto& imgui = m_world.ctx().at<ImguiService>();
    m_drawConnection = imgui.OnDraw.connect<&NameTagService::OnDraw>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&NameTagService::OnUpdate>(this);
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

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const auto& formComponent = view.get<FormIdComponent>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formComponent.Id));
        if (!pActor)
            continue;

        const float dx = pLocalPlayer->position.x - pActor->position.x;
        const float dy = pLocalPlayer->position.y - pActor->position.y;
        const float dz = pLocalPlayer->position.z - pActor->position.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        bool isVisible = !pActor->IsDead();
        if (!isVisible && distance <= kNearBypassDistance)
            isVisible = true;

        VisibilityInfo& info = m_visibility[playerComponent.Id];
        info.Visible = isVisible;
        info.Tick = tick;
    }

    constexpr uint64_t kMaxAgeMs = 2000;
    for (auto it = m_visibility.begin(); it != m_visibility.end();)
    {
        if (tick - it->second.Tick > kMaxAgeMs)
            it = m_visibility.erase(it);
        else
            ++it;
    }
}

void NameTagService::OnDraw() noexcept
{
    auto* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
        return;

    // Don't draw nametags while menus that cover the screen are open.
    if (auto* pUI = UI::Get())
    {
        auto menuOpen = [pUI](const char* acName) {
            return acName && pUI->GetMenuOpen(BSFixedString(acName));
        };

        if (menuOpen("Dialogue Menu") || menuOpen("InventoryMenu") || menuOpen("StatsMenu") ||
            menuOpen("SkillsMenu") || menuOpen("MagicMenu") || menuOpen("MapMenu") || menuOpen("TweenMenu") ||
            menuOpen("FavoritesMenu"))
        {
            return;
        }
    }

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    if (view.begin() == view.end())
        return;

    const auto& partyMembers = m_world.GetPartyService().GetPartyMembers();
    const auto& actorNames = m_world.GetPartyService();

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    ImFont* font = ImGui::GetFont();
    const float baseFontSize = font ? font->FontSize : 16.f;

    const glm::vec3 localPos = {pLocalPlayer->position.x, pLocalPlayer->position.y, pLocalPlayer->position.z};

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

        // Only show tags for party members.
        bool isPartyMember = false;
        for (size_t i = 0; i < partyMembers.size(); ++i)
        {
            if (partyMembers[i] == playerComponent.Id)
            {
                isPartyMember = true;
                break;
            }
        }
        if (!isPartyMember)
            continue;

        if (!ShouldRenderTag(playerComponent.Id))
            continue;

        // Character name (fallback to username already shown in party UI).
        std::string displayName = "#" + std::to_string(playerComponent.Id);
        if (const String* pActorName = actorNames.GetActorName(playerComponent.Id))
        {
            if (!pActorName->empty())
                displayName = *pActorName;
        }

        const NiPoint3 anchor = BuildAnchorPoint(pActor);

        ImVec2 screenPos;
        float depth = 0.f;
        if (!ProjectWorldPoint(anchor, screenPos, depth, viewport))
            continue;

        const glm::vec3 remotePos = {anchor.x, anchor.y, anchor.z};
        const float distance = glm::distance(localPos, remotePos);
        if (distance > kMaxDistance)
            continue;

        float alpha = 1.f;
        if (distance > kFadeDistance)
        {
            const float fadeSpan = std::max(kMaxDistance - kFadeDistance, 1.f);
            const float fadeT = Clamp01((distance - kFadeDistance) / fadeSpan);
            alpha = 1.f - SmoothStep(0.f, 1.f, fadeT);
        }
        alpha *= Clamp01(1.f - std::max(depth - 0.95f, 0.f) * 4.f);
        if (alpha <= 0.01f)
            continue;

        const uint16_t level = pActor->GetLevel();
        const std::string levelText = level > 0 ? fmt::format("Lv. {}", level) : std::string("Lv. --");

        const float nameFontSize = baseFontSize * kScale;
        const float levelFontSize = nameFontSize * 0.75f;
        const ImVec2 nameTextSize = font ? font->CalcTextSizeA(nameFontSize, FLT_MAX, 0.f, displayName.c_str(), displayName.c_str() + displayName.size()) : ImVec2(static_cast<float>(displayName.size()) * nameFontSize * 0.5f, nameFontSize);
        const ImVec2 levelTextSize = font ? font->CalcTextSizeA(levelFontSize, FLT_MAX, 0.f, levelText.c_str(), levelText.c_str() + levelText.size()) : ImVec2(static_cast<float>(levelText.size()) * levelFontSize * 0.5f, levelFontSize);

        const float paddingX = 14.f * kScale;
        const float paddingY = 6.f * kScale;
        const float textColumnWidth = std::max(nameTextSize.x, levelTextSize.x);
        const float textColumnHeight = nameTextSize.y + 4.f * kScale + levelTextSize.y;
        const float totalWidth = textColumnWidth + paddingX * 2.f;
        const float totalHeight = textColumnHeight + paddingY * 2.f;

        ImVec2 topLeft(screenPos.x - totalWidth * 0.5f, screenPos.y - totalHeight * 0.5f);
        ImVec2 bottomRight(screenPos.x + totalWidth * 0.5f, screenPos.y + totalHeight * 0.5f);

        const ImU32 bgColor = ColorWithAlpha(ImVec4(0.f, 0.f, 0.f, 0.65f), alpha);
        drawList->AddRectFilled(topLeft, bottomRight, bgColor, 6.f);

        const ImU32 borderColor = ColorWithAlpha(ImVec4(1.f, 1.f, 1.f, 0.35f), alpha);
        drawList->AddRect(topLeft, bottomRight, borderColor, 6.f);

        ImVec2 namePos(topLeft.x + paddingX, topLeft.y + paddingY + (textColumnHeight - nameTextSize.y - 4.f * kScale - levelTextSize.y) * 0.5f);
        ImVec2 levelPos(namePos.x, namePos.y + nameTextSize.y + 4.f * kScale);

        const ImU32 textColor = ColorWithAlpha(ImVec4(0.95f, 0.95f, 0.95f, 1.f), alpha);
        if (font)
            drawList->AddText(font, nameFontSize, namePos, textColor, displayName.c_str(), displayName.c_str() + displayName.size());
        else
            drawList->AddText(namePos, textColor, displayName.c_str());

        const ImU32 levelColor = ColorWithAlpha(ImVec4(0.85f, 0.92f, 1.f, 0.95f), alpha);
        if (font)
            drawList->AddText(font, levelFontSize, levelPos, levelColor, levelText.c_str(), levelText.c_str() + levelText.size());
        else
            drawList->AddText(levelPos, levelColor, levelText.c_str());
    }
}

bool NameTagService::ShouldRenderTag(uint32_t aPlayerId) const noexcept
{
    const uint64_t tick = m_world.GetTick();
    auto it = m_visibility.find(aPlayerId);
    if (it == m_visibility.end())
        return true;

    constexpr uint64_t kStaleThresholdMs = 1500;
    if (tick - it->second.Tick > kStaleThresholdMs)
        return true;

    return it->second.Visible;
}

bool NameTagService::ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept
{
    NiPoint3 projected{};
    if (!HUDMenuUtils::WorldPtToScreenPt3(aWorldPoint, projected))
        return false;

    if (projected.z <= kVisibilityEpsilon || projected.z >= 1.f)
        return false;

    aScreenPos.x = projected.x * aViewport.x;
    aScreenPos.y = (1.f - projected.y) * aViewport.y;
    aDepth = projected.z;

    return true;
}

NiPoint3 NameTagService::BuildAnchorPoint(Actor* apActor) const noexcept
{
    NiPoint3 anchor = apActor->position;
    anchor.z += apActor->GetHeight() + kVerticalOffset;

    const float yaw = apActor->rotation.z;
    anchor.x += std::sin(yaw) * kForwardOffset;
    anchor.y += std::cos(yaw) * kForwardOffset;

    return anchor;
}

ImU32 NameTagService::ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept
{
    ImVec4 adjusted = aColor;
    adjusted.w *= Clamp01(aAlpha);
    return ImGui::ColorConvertFloat4ToU32(adjusted);
}
