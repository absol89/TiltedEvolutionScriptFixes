#pragma once

#include <TiltedCore/TaskQueue.hpp>

struct UpdateEvent;

/**
 * @brief Dispatches events.
 */
struct RunnerService
{
    RunnerService(entt::dispatcher& aDispatcher) noexcept;
    ~RunnerService() noexcept = default;

    TP_NOCOPYMOVE(RunnerService);

    /**
     * @brief Executes all queued events every frame.
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;

    /**
     * @brief Queues an event for OnUpdate() to execute.
     */
    template <class T> void Trigger(T acEvent)
    {
        m_runner.Add([event = std::move(acEvent), this]() { m_dispatcher.trigger(std::move(event)); });
    }

    /**
     * @brief Queues a lambda for OnUpdate() to execute.
     */
    void Queue(std::function<void()> aFunctor) noexcept;

    /**
     * @brief Queues a lambda for the game's main thread, run at the start of the next frame.
     *
     * OnUpdate() runs inside the Papyrus VM update, on a job thread the main thread waits for. Game code
     * that waits for the main thread in turn, such as UI callbacks that start sleeping, deadlocks there.
     */
    static void QueueOnGameThread(std::function<void()> aFunctor) noexcept;

    /**
     * @brief Runs the lambdas queued with QueueOnGameThread(). Called by the main loop hook.
     */
    static void DrainGameThread() noexcept;

private:
    entt::dispatcher& m_dispatcher;
    TiltedPhoques::TaskQueue m_runner;
    static TiltedPhoques::TaskQueue s_gameThreadRunner;
};
