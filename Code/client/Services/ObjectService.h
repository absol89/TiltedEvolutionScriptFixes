#pragma once

#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct ServerTimeSettings;
struct DisconnectedEvent;
struct World;
struct ActivateEvent;
struct TransportService;
struct NotifyActivate;
struct LockChangeEvent;
struct NotifyLockChange;
struct CellChangeEvent;
struct ScriptAnimationEvent;
struct WaveCommandEvent;
struct AssignObjectsResponse;
struct NotifyScriptAnimation;
struct NotifyRemoveObjects;
struct UpdateEvent;
struct NotifyEquipmentChanges;
struct CharacterSpawnRequest;

/**
 * @brief Handles objects in the environment.
 */
class ObjectService final : public BSTEventSink<TESActivateEvent>
{
public:
    ObjectService(World&, entt::dispatcher&, TransportService&);

private:
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnCellChange(const CellChangeEvent&) noexcept;
    void OnAssignObjectsResponse(const AssignObjectsResponse&) noexcept;
    void OnRemoveObjects(const NotifyRemoveObjects&) noexcept;
    void OnActivate(const ActivateEvent&) noexcept;
    void OnActivateNotify(const NotifyActivate&) noexcept;
    void OnLockChange(const LockChangeEvent&) noexcept;
    void OnLockChangeNotify(const NotifyLockChange&) noexcept;
    void OnScriptAnimationEvent(const ScriptAnimationEvent&) noexcept;
    void OnWaveCommand(const WaveCommandEvent&) noexcept;
    void OnNotifyScriptAnimation(const NotifyScriptAnimation&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnNotifyEquipmentChanges(const NotifyEquipmentChanges&) noexcept;
    void OnCharacterSpawnRequest(const CharacterSpawnRequest&) noexcept;
    void RetrySubGraphEvent(uint32_t aServerId, const char* acpReason) noexcept;

    BSTEventResult OnEvent(const TESActivateEvent*, const EventDispatcher<TESActivateEvent>*) override;

    entt::entity CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept;

    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_onActivateConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_lockChangeNotifyConnection;
    entt::scoped_connection m_assignObjectConnection;
    entt::scoped_connection m_removeObjectsConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_scriptAnimationNotifyConnection;
    entt::scoped_connection m_waveCommandConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_equipmentChangesConnection;
    entt::scoped_connection m_characterSpawnConnection;

    // The last subgraph event of each remote actor, by server id. A skin armor that isn't equipped or loaded yet
    // when it arrives can't take it, so it's tried again until the armor takes it. It's sent once per body: sending it
    // again would restart what it plays, such as a skin fading in.
    struct SubGraphReplay
    {
        String EventName;
        double Delay = -1.0;   // Until the next try, negative when the event was taken or given up on.
        double TimeLeft = 0.0; // Before giving up.
    };
    Map<uint32_t, SubGraphReplay> m_subGraphReplays;
};
