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

#include <imgui_internal.h>

#include <string>

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

bool HasAllGlyphs(ImFont* apFont, const String& acText) noexcept
{
    const char* pText = acText.c_str();
    const char* pEnd = pText + acText.size();
    while (pText < pEnd)
    {
        unsigned int codepoint = 0;
        pText += ImTextCharFromUtf8(&codepoint, pText, pEnd);
        if (codepoint < 0x80)
            continue;
        if (!apFont->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)))
            return false;
    }
    return true;
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

void NameTagService::SetSettings(Settings aSettings) noexcept
{
    aSettings.NameFadeStart = std::max(aSettings.NameFadeStart, 0.f);
    aSettings.NameInvisible = std::max(aSettings.NameInvisible, aSettings.NameFadeStart + 1.f);
    aSettings.PartyFadeStart = std::max(aSettings.PartyFadeStart, 0.f);
    aSettings.PartyInvisible = std::max(aSettings.PartyInvisible, aSettings.PartyFadeStart + 1.f);
    aSettings.FontSize = std::clamp(aSettings.FontSize, 8.f, 48.f);

    m_settings = std::move(aSettings);
}

void NameTagService::OnDraw() noexcept
{
    if (m_settings.NameTags == Filter::kOff)
        return;

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

    const auto& partyService = m_world.GetPartyService();
    const auto& partyMembers = partyService.GetPartyMembers();

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    ImFont* font = m_world.ctx().at<ImguiService>().GetNameTagFont();
    if (!font)
        font = ImGui::GetFont();

    // The label comes translated from the UI; fall back to English when the
    // tag font lacks its glyphs (e.g. CJK).
    const char* strangerLabel = m_settings.StrangerLabel.c_str();
    if (!HasAllGlyphs(font, m_settings.StrangerLabel))
        strangerLabel = "Stranger";

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

        const bool isPartyMember = std::find(partyMembers.begin(), partyMembers.end(), playerComponent.Id) != partyMembers.end();
        if (!IsEnabledFor(m_settings.NameTags, isPartyMember))
            continue;

        if (!ShouldRenderTag(playerComponent.Id))
            continue;

        // Name from the party UI's player list (keyed by player id).
        const bool isNameRevealed = m_settings.RevealedPlayerIds.count(playerComponent.Id) > 0;
        std::string displayName = strangerLabel;
        if (isNameRevealed)
        {
            displayName = "#" + std::to_string(playerComponent.Id);
            const auto& players = partyService.GetPlayers();
            auto it = players.find(playerComponent.Id);
            if (it != players.end() && !it->second.empty())
                displayName = it->second.c_str();
        }

        const NiPoint3 anchor = BuildAnchorPoint(pActor);

        ImVec2 screenPos;
        float depth = 0.f;
        if (!ProjectWorldPoint(anchor, screenPos, depth, viewport))
            continue;

        const float fadeStart = isPartyMember ? m_settings.PartyFadeStart : m_settings.NameFadeStart;
        const float invisible = isPartyMember ? m_settings.PartyInvisible : m_settings.NameInvisible;

        const glm::vec3 remotePos = {anchor.x, anchor.y, anchor.z};
        const float distance = glm::distance(localPos, remotePos);
        if (distance > invisible)
            continue;

        float alpha = 1.f;
        if (distance > fadeStart)
        {
            const float fadeT = Clamp01((distance - fadeStart) / (invisible - fadeStart));
            alpha = 1.f - SmoothStep(0.f, 1.f, fadeT);
        }
        alpha *= Clamp01(1.f - std::max(depth - 0.95f, 0.f) * 4.f);
        if (alpha <= 0.01f)
            continue;

        const bool showLevel = IsEnabledFor(m_settings.Level, isPartyMember);
        const bool showBackplate = IsEnabledFor(m_settings.Backplate, isPartyMember);

        const uint16_t level = pActor->GetLevel();
        const std::string levelText = !showLevel ? std::string() : level > 0 ? ("Lv. " + std::to_string(level)) : std::string("Lv. --");

        const float scale = m_settings.FontSize / 16.f;
        const float nameFontSize = m_settings.FontSize * kFontSizeScale;
        const float levelFontSize = nameFontSize * 0.75f;
        const float lineSpacing = showLevel ? 4.f * scale : 0.f;
        const ImVec2 nameTextSize = font->CalcTextSizeA(nameFontSize, FLT_MAX, 0.f, displayName.c_str(), displayName.c_str() + displayName.size());
        const ImVec2 levelTextSize = showLevel ? font->CalcTextSizeA(levelFontSize, FLT_MAX, 0.f, levelText.c_str(), levelText.c_str() + levelText.size()) : ImVec2(0.f, 0.f);

        const float paddingX = 14.f * scale;
        const float paddingY = 6.f * scale;
        const float textColumnWidth = std::max(nameTextSize.x, levelTextSize.x);
        const float textColumnHeight = nameTextSize.y + lineSpacing + levelTextSize.y;
        const float totalWidth = textColumnWidth + paddingX * 2.f;
        const float totalHeight = textColumnHeight + paddingY * 2.f;

        ImVec2 topLeft(screenPos.x - totalWidth * 0.5f, screenPos.y - totalHeight * 0.5f);
        ImVec2 bottomRight(screenPos.x + totalWidth * 0.5f, screenPos.y + totalHeight * 0.5f);

        if (showBackplate)
        {
            const ImU32 bgColor = ColorWithAlpha(ImVec4(0.f, 0.f, 0.f, 0.65f), alpha);
            drawList->AddRectFilled(topLeft, bottomRight, bgColor, 6.f * scale);

            const ImU32 borderColor = ColorWithAlpha(ImVec4(1.f, 1.f, 1.f, 0.35f), alpha);
            drawList->AddRect(topLeft, bottomRight, borderColor, 6.f * scale);
        }

        // Without a backplate a soft shadow keeps the text readable.
        auto drawText = [&](float aSize, ImVec2 aPos, const ImVec4& aColor, const std::string& acText, bool aItalic) {
            const int firstVertex = drawList->VtxBuffer.Size;

            if (!showBackplate)
            {
                const ImU32 shadowColor = ColorWithAlpha(ImVec4(0.f, 0.f, 0.f, 0.8f), alpha);
                drawList->AddText(font, aSize, ImVec2(aPos.x + 1.f, aPos.y + 1.f), shadowColor, acText.c_str(), acText.c_str() + acText.size());
            }
            drawList->AddText(font, aSize, aPos, ColorWithAlpha(aColor, alpha), acText.c_str(), acText.c_str() + acText.size());

            // ImGui has no italic style: shear the glyph quads around the baseline.
            if (aItalic)
            {
                const float baseline = aPos.y + aSize;
                for (int i = firstVertex; i < drawList->VtxBuffer.Size; ++i)
                    drawList->VtxBuffer[i].pos.x += (baseline - drawList->VtxBuffer[i].pos.y) * kItalicShear;
            }
        };

        const ImVec2 namePos(screenPos.x - nameTextSize.x * 0.5f, topLeft.y + paddingY);
        const ImVec4 nameColor = isNameRevealed ? ImVec4(0.95f, 0.95f, 0.95f, 1.f) : ImVec4(0.75f, 0.75f, 0.75f, 1.f);
        drawText(nameFontSize, namePos, nameColor, displayName, !isNameRevealed);

        if (showLevel)
        {
            const ImVec2 levelPos(screenPos.x - levelTextSize.x * 0.5f, namePos.y + nameTextSize.y + lineSpacing);
            drawText(levelFontSize, levelPos, ImVec4(0.85f, 0.92f, 1.f, 0.95f), levelText, false);
        }
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

bool NameTagService::IsEnabledFor(Filter aFilter, bool aIsPartyMember) noexcept
{
    return aFilter == Filter::kOn || (aFilter == Filter::kParty && aIsPartyMember);
}

ImU32 NameTagService::ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept
{
    ImVec4 adjusted = aColor;
    adjusted.w *= Clamp01(aAlpha);
    return ImGui::ColorConvertFloat4ToU32(adjusted);
}
