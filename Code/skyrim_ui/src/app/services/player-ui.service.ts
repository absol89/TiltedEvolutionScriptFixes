import { Injectable, OnDestroy } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import {
  combineLatest,
  debounceTime,
  map,
  Observable,
  ReplaySubject,
  share,
  Subscription,
} from 'rxjs';
import { Player } from '../models/player';
import { ChatMessage, MessageTypes } from './chat.service';
import { ClientService } from './client.service';
import { GroupService } from './group.service';
import { PlayerListService } from './player-list.service';
import {
  fontSizeToPixels,
  PlayerUiFilter,
  SettingService,
} from './setting.service';

export interface PlayerUiEntry extends Player {
  isMember: boolean;
  /** Whether the real name may be shown, after the eye override. */
  isNameRevealed: boolean;
}

/**
 * Applies the "Player UI filters" settings: stranger names (with per-username
 * overrides), invite filters, the mini chat preview, and the name tag options
 * forwarded to the game client.
 */
@Injectable({
  providedIn: 'root',
})
export class PlayerUiService implements OnDestroy {
  public readonly players$: Observable<PlayerUiEntry[]>;
  /** Players whose name is hidden behind the "Stranger" label. */
  public readonly hiddenPlayerIds$: Observable<Set<number>>;

  private readonly settings = this.settingService.settings;
  private nameTagSubscription: Subscription;

  constructor(
    private readonly settingService: SettingService,
    private readonly playerListService: PlayerListService,
    private readonly groupService: GroupService,
    private readonly clientService: ClientService,
    private readonly translocoService: TranslocoService,
  ) {
    this.players$ = combineLatest([
      this.playerListService.playerList,
      this.groupService.group,
      this.settings.strangerNames,
      this.settingService.nameOverrides,
    ]).pipe(
      map(([playerList, group, strangerNames, overrides]) => {
        const members = group?.members ?? [];
        return (playerList?.players ?? []).map(player => {
          const isMember = members.includes(player.id);
          return {
            ...player,
            isMember,
            // The eye wins over both defaults, so party members can be
            // hidden too.
            isNameRevealed:
              overrides[player.name] ?? (isMember || strangerNames),
          };
        });
      }),
      share({
        connector: () => new ReplaySubject(1),
        resetOnRefCountZero: false,
      }),
    );

    this.hiddenPlayerIds$ = this.players$.pipe(
      map(
        players =>
          new Set(
            players
              .filter(player => !player.isNameRevealed)
              .map(player => player.id),
          ),
      ),
    );

    this.onNameTagSettingsChanged();
  }

  ngOnDestroy() {
    this.nameTagSubscription.unsubscribe();
  }

  public toggleName(player: PlayerUiEntry) {
    this.settingService.setNameOverride(player.name, !player.isNameRevealed);
  }

  public acceptsPartyInvites(): boolean {
    return this.settings.partyInvites.getValue();
  }

  public acceptsTradeInvite(inviterId: number): boolean {
    return this.matchesFilter(
      this.settings.tradeInvites.getValue(),
      this.isPartyMember(inviterId),
    );
  }

  /** Whether a chat message may show in the in-game preview (F2 closed). */
  public showsInMiniChat(message: ChatMessage): boolean {
    const filter = this.settings.miniChat.getValue();
    if (filter !== PlayerUiFilter.PARTY) {
      return filter === PlayerUiFilter.ON;
    }

    if (
      message.type === MessageTypes.SYSTEM_MESSAGE ||
      message.type === MessageTypes.PARTY_CHAT
    ) {
      return true;
    }

    const members = this.groupService.group.getValue()?.members ?? [];
    const players = this.playerListService.playerList.getValue()?.players;
    return !!players?.some(
      player =>
        player.name === message.senderName && members.includes(player.id),
    );
  }

  private isPartyMember(playerId: number): boolean {
    return !!this.groupService.group.getValue()?.members.includes(playerId);
  }

  private matchesFilter(filter: PlayerUiFilter, isMember: boolean): boolean {
    return (
      filter === PlayerUiFilter.ON ||
      (filter === PlayerUiFilter.PARTY && isMember)
    );
  }

  private onNameTagSettingsChanged() {
    const filterIndex = (filter: PlayerUiFilter) =>
      Object.values(PlayerUiFilter).indexOf(filter);

    this.nameTagSubscription = combineLatest([
      this.players$,
      this.settings.nameTags,
      this.settings.nameTagLevel,
      this.settings.nameTagBackplate,
      this.settings.nameTagSize,
      this.settings.nameFadeStart,
      this.settings.nameInvisible,
      this.settings.partyFadeStart,
      this.settings.partyInvisible,
      this.translocoService.selectTranslate('COMPONENT.PARTY_LIST.STRANGER'),
    ])
      .pipe(debounceTime(100))
      .subscribe(
        ([
          players,
          nameTags,
          level,
          backplate,
          size,
          nameFadeStart,
          nameInvisible,
          partyFadeStart,
          partyInvisible,
          strangerLabel,
        ]) => {
          this.clientService.setNameTagSettings({
            nameTags: filterIndex(nameTags),
            level: filterIndex(level),
            backplate: filterIndex(backplate),
            fontSize: fontSizeToPixels[size],
            nameFadeStart: Number(nameFadeStart),
            nameInvisible: Number(nameInvisible),
            partyFadeStart: Number(partyFadeStart),
            partyInvisible: Number(partyInvisible),
            strangerLabel,
            revealedPlayerIds: players
              .filter(player => player.isNameRevealed)
              .map(player => player.id),
          });
        },
      );
  }
}
