import { Component, OnDestroy } from '@angular/core';
import { Subscription, combineLatest } from 'rxjs';
import { PartyPin } from '../../models/party-pin';
import { ClientService } from '../../services/client.service';
import { PlayerListService } from '../../services/player-list.service';
import { SettingService } from '../../services/setting.service';

@Component({
  selector: 'app-party-pins',
  templateUrl: './party-pins.component.html',
  styleUrls: ['./party-pins.component.scss'],
})
export class PartyPinsComponent implements OnDestroy {
  pins: PartyPin[] = [];
  readonly defaultAvatar = 'assets/images/group/avatar-placeholder.png';
  showMarkerAvatar = true;
  pinSize = 36;
  pinLabelOffset = 24;
  private sub: Subscription;

  constructor(
    private readonly client: ClientService,
    private readonly playerList: PlayerListService,
    private readonly settingService: SettingService,
  ) {
    this.sub = combineLatest([
      this.client.partyPinsChange,
      this.playerList.playerList.asObservable(),
      this.settingService.settings.partyPinShowAvatar,
      this.settingService.settings.partyPinScale,
    ]).subscribe(([pins, list, showAvatar, scale]) => {
      const players = list?.players ?? [];
      const playerMap = new Map(players.map(player => [player.id, player]));
      this.showMarkerAvatar = showAvatar;
      const resolvedScale = Math.max(0.6, Math.min(1.8, scale));
      this.pinSize = Math.round(36 * resolvedScale);
      this.pinLabelOffset = Math.round(this.pinSize * 0.66);

      this.pins = pins.map(pin => {
        const player = playerMap.get(pin.id);
        const resolvedName =
          player?.displayName ?? pin.name ?? player?.name ?? '';
        const resolvedAvatar =
          pin.avatar !== undefined ? pin.avatar : player?.avatar;

        return {
          ...pin,
          name: resolvedName.length > 0 ? resolvedName : undefined,
          avatar:
            resolvedAvatar && resolvedAvatar.length > 0
              ? resolvedAvatar
              : this.defaultAvatar,
        };
      });
    });
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}
