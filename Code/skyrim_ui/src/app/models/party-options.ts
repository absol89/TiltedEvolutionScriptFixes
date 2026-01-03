export interface PartyOptions {
  syncFastTravelMarkers: boolean;
  showPartyMemberMarkers: boolean;
  syncDeadBodyLoot: boolean;
  lockPartyToLeaderCell: boolean;
}

export const DEFAULT_PARTY_OPTIONS: PartyOptions = {
  syncFastTravelMarkers: false,
  showPartyMemberMarkers: true,
  syncDeadBodyLoot: false,
  lockPartyToLeaderCell: false,
};
