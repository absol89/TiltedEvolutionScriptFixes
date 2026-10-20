import { ChangeDetectionStrategy, Component } from '@angular/core';
import { faEye, faEyeSlash } from '@fortawesome/free-solid-svg-icons';
import { Observable, ReplaySubject, share } from 'rxjs';
import { map } from 'rxjs/operators';
import { ClientService } from 'src/app/services/client.service';
import { GroupService } from 'src/app/services/group.service';
import { PlayerListService } from 'src/app/services/player-list.service';
import {
  PlayerUiEntry,
  PlayerUiService,
} from 'src/app/services/player-ui.service';

@Component({
  selector: 'app-player-list',
  templateUrl: './player-list.component.html',
  styleUrls: ['./player-list.component.scss'],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class PlayerListComponent {
  readonly faEye = faEye;
  readonly faEyeSlash = faEyeSlash;

  playerList$: Observable<PlayerUiEntry[]>;
  playerListLength$: Observable<number>;
  isPartyLeader$: Observable<boolean>;

  constructor(
    private readonly playerListService: PlayerListService,
    private readonly playerUiService: PlayerUiService,
    private readonly clientService: ClientService,
    private readonly groupService: GroupService,
  ) {
    this.playerList$ = this.playerUiService.players$;
    this.playerListLength$ = this.playerList$.pipe(
      map(players => players?.length ?? 0),
    );
    this.isPartyLeader$ = this.groupService.group.asObservable().pipe(
      map(
        group =>
          group.isEnabled && group.owner == this.clientService.localPlayerId,
      ),
      share({
        connector: () => new ReplaySubject(1),
        resetOnRefCountZero: true,
      }),
    );
  }

  public toggleName(player: PlayerUiEntry) {
    this.playerUiService.toggleName(player);
  }

  public sendPartyInvite(inviteeId: number) {
    this.playerListService.sendPartyInvite(inviteeId);
  }
}
