#include <Services/CalendarService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/TimeSkipRequestEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyTimeSkip.h>
#include <Messages/RequestTimeSkip.h>
#include <Messages/ServerTimeSettings.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>

constexpr float kTransitionSpeed = 5.f;
// How long the clock waits for a confirmed sleep or wait to start before following the shared time again.
constexpr uint64_t kTimeSkipStartTimeoutMs = 3000;

bool CalendarService::s_gameClockLocked = false;

static bool IsPlayerSleepingOrWaiting() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer && pPlayer->IsSleepingOrWaiting();
}

// While the player sleeps or waits the game runs its own clock, so the hourly updates see the right time.
bool CalendarService::AllowGameTick() noexcept
{
    return !s_gameClockLocked || IsPlayerSleepingOrWaiting();
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_transport(aTransport)
{
    m_timeUpdateConnection = aDispatcher.sink<ServerTimeSettings>().connect<&CalendarService::OnTimeUpdate>(this);
    m_timeSkipRequestConnection = aDispatcher.sink<TimeSkipRequestEvent>().connect<&CalendarService::OnTimeSkipRequest>(this);
    m_notifyTimeSkipConnection = aDispatcher.sink<NotifyTimeSkip>().connect<&CalendarService::OnNotifyTimeSkip>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::HandleUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CalendarService::OnDisconnected>(this);
    EventDispatcherManager::Get()->loadGameEvent.RegisterSink(this);
}

CalendarService::~CalendarService() noexcept
{
    EventDispatcherManager::Get()->loadGameEvent.UnRegisterSink(this);
}

BSTEventResult CalendarService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    SleepWaitMenu::CancelPendingConfirm();
    return BSTEventResult::kOk;
}

void CalendarService::OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept
{
    spdlog::info("[TimeSkipDiag] server time {} day {} scale {} received; game hour {}, local online time {}, sleeping {}", acMessage.timeModel.Time,
                 acMessage.timeModel.Day, acMessage.timeModel.TimeScale, TimeData::Get()->GameHour->f, m_onlineTime.m_timeModel.Time,
                 IsPlayerSleepingOrWaiting());

    // disable the game clock
    ToggleGameClock(false);
    m_onlineTime.m_timeModel.TimeScale = acMessage.timeModel.TimeScale;
    m_onlineTime.m_timeModel.Time = acMessage.timeModel.Time;

    if (m_world.GetServerSettings().SyncPlayerCalendar)
    {
        m_onlineTime.m_timeModel.Day = acMessage.timeModel.Day;
        m_onlineTime.m_timeModel.Month = acMessage.timeModel.Month;
        m_onlineTime.m_timeModel.Year = acMessage.timeModel.Year;
    }
    else
    {
        // Each player keeps their own date. A shared sleep or wait still moves it on by the skipped hours, otherwise
        // a skip past midnight would put the player back on the day they went to sleep.
        DateTime localDate(m_offlineTime.m_timeModel);
        if (m_timeSkipHours > 0.f)
            localDate.AddHours(m_timeSkipHours);

        m_onlineTime.m_timeModel.Day = localDate.m_timeModel.Day;
        m_onlineTime.m_timeModel.Month = localDate.m_timeModel.Month;
        m_onlineTime.m_timeModel.Year = localDate.m_timeModel.Year;
    }
    m_timeSkipHours = 0.f;
}

void CalendarService::OnTimeSkipRequest(const TimeSkipRequestEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected() || !SleepWaitMenu::IsRequestPending(acEvent.RequestId))
        return;

    m_timeSkipPending = true;
    m_timeSkipRequestId = acEvent.RequestId;
    m_timeSkipStartTick = 0;
    spdlog::info("[TimeSkipDiag] confirmed the sleep/wait menu for {} hours, game hour {}", acEvent.Hours, TimeData::Get()->GameHour->f);

    RequestTimeSkip request;
    request.Hours = acEvent.Hours;
    m_transport.Send(request);
}

void CalendarService::OnNotifyTimeSkip(const NotifyTimeSkip& acMessage) noexcept
{
    spdlog::info("[TimeSkipDiag] notify ready {}/{}, hours {}, pending {}, menu open {}", acMessage.ReadyCount, acMessage.RequiredCount, acMessage.Hours,
                 m_timeSkipPending, SleepWaitMenu::IsOpen());

    if (acMessage.ReadyCount < acMessage.RequiredCount)
    {
        if (acMessage.ReadyCount > 0)
            Utils::ShowHudMessage(String(fmt::format("Waiting for players to sleep or wait ({}/{})", acMessage.ReadyCount, acMessage.RequiredCount)));
        return;
    }

    m_timeSkipEndDays = TimeData::Get()->GameDaysPassed->f + acMessage.Hours / 24.f;
    // The server sends the skipped time right after this message.
    m_timeSkipHours = acMessage.Hours;

    if (m_timeSkipPending)
    {
        m_timeSkipPending = false;
        // The sleep starts on a subsequent native menu update. The skipped time arrives right after this message, so
        // without holding the clock the game hour would jump ahead first and the sleep would then add the hours again.
        m_timeSkipStartTick = m_world.GetTick();
        SleepWaitMenu::Confirm(acMessage.Hours, m_timeSkipRequestId);
    }
}

void CalendarService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    SleepWaitMenu::CancelPendingConfirm();
    m_timeSkipRequestId = 0;
    m_timeSkipPending = false;
    m_timeSkipStartTick = 0;
    m_timeSkipEndDays = 0.f;
    m_timeSkipHours = 0.f;

    // signal a time transition
    m_fadeTimer = 0.f;
    ToggleGameClock(true);
}

float CalendarService::TimeInterpolate(const TimeModel& aFrom, TimeModel& aTo) const
{
    const auto t = aTo.Time - aFrom.Time;
    if (t < 0.f)
    {
        const auto v = t + 24.f;
        // interpolate on the time difference, not the time
        const auto x = TiltedPhoques::Lerp(0.f, v, m_fadeTimer / kTransitionSpeed) + aFrom.Time;

        return TiltedPhoques::Mod(x, 24.f);
    }

    return TiltedPhoques::Lerp(aFrom.Time, aTo.Time, m_fadeTimer / kTransitionSpeed);
}

void CalendarService::ToggleGameClock(bool aEnable)
{
    auto* pGameTime = TimeData::Get();
    m_offlineTime.m_timeModel.Day = pGameTime->GameDay->f;
    m_offlineTime.m_timeModel.Month = pGameTime->GameMonth->f;
    m_offlineTime.m_timeModel.Year = pGameTime->GameYear->f;
    m_offlineTime.m_timeModel.Time = pGameTime->GameHour->f;
    m_offlineTime.m_timeModel.TimeScale = pGameTime->TimeScale->f;

    s_gameClockLocked = !aEnable;
}

void CalendarService::HandleUpdate(const UpdateEvent& aEvent) noexcept
{
    if (m_timeSkipPending && !SleepWaitMenu::IsRequestPending(m_timeSkipRequestId))
    {
        m_timeSkipPending = false;
        m_timeSkipRequestId = 0;
        spdlog::info("[TimeSkipDiag] sleep/wait menu closed before the skip started, cancelling the request");
        m_transport.Send(RequestTimeSkip{});
    }

    if (s_gameClockLocked)
    {
        const auto updateDelta = static_cast<float>(aEvent.Delta);
        auto* pGameTime = TimeData::Get();

        if (!m_lastTick)
            m_lastTick = m_world.GetTick();

        const auto now = m_world.GetTick();

        // we got disconnected or the client got ahead of us
        if (now < m_lastTick)
            return;

        const auto delta = now - m_lastTick;
        m_lastTick = now;

        m_onlineTime.Update(delta);

        const bool cSleeping = IsPlayerSleepingOrWaiting();
        if (cSleeping != m_wasSleeping)
        {
            m_wasSleeping = cSleeping;
            spdlog::info("[TimeSkipDiag] {} sleeping or waiting: game hour {}, online time {} day {}", cSleeping ? "started" : "stopped",
                         pGameTime->GameHour->f, m_onlineTime.m_timeModel.Time, m_onlineTime.m_timeModel.Day);
        }

        // The OK callback can autosave and re-enter the VM before setting the sleep timer. Do not expire
        // the handoff or apply the server's skipped clock while that native callback is still running.
        if (SleepWaitMenu::IsConfirming())
            return;

        if (cSleeping)
        {
            m_timeSkipStartTick = 0;
            m_timeSkipRequestId = 0;
            return;
        }

        // A confirmed sleep or wait hasn't started yet: keep the game clock where it is until it does.
        if (m_timeSkipStartTick)
        {
            if (now - m_timeSkipStartTick < kTimeSkipStartTimeoutMs)
                return;

            spdlog::warn("[TimeSkipDiag] the confirmed sleep or wait didn't start within {} ms", kTimeSkipStartTimeoutMs);
            SleepWaitMenu::CancelPendingConfirm(m_timeSkipRequestId);
            m_timeSkipRequestId = 0;
            m_timeSkipStartTick = 0;
        }

        // An interrupted skip still counts in full for timers, like the shared clock that already moved on.
        if (m_timeSkipEndDays > 0.f)
        {
            pGameTime->GameDaysPassed->f = std::max(pGameTime->GameDaysPassed->f, m_timeSkipEndDays);
            m_timeSkipEndDays = 0.f;
        }

        pGameTime->TimeScale->f = m_onlineTime.m_timeModel.TimeScale;
        pGameTime->GameDay->f = m_onlineTime.m_timeModel.Day;
        pGameTime->GameMonth->f = m_onlineTime.m_timeModel.Month;
        pGameTime->GameYear->f = m_onlineTime.m_timeModel.Year;
        pGameTime->GameDaysPassed->f += m_onlineTime.GetDeltaTime(delta) * (1.f / 24.f);

        // time transition in
        if (m_fadeTimer < kTransitionSpeed)
        {
            pGameTime->GameHour->f = TimeInterpolate(m_offlineTime.m_timeModel, m_onlineTime.m_timeModel);
            m_fadeTimer += updateDelta;
        }
        else
            pGameTime->GameHour->f = m_onlineTime.m_timeModel.Time;
    }
}
