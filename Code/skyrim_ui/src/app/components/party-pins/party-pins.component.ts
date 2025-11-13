import { Component, OnDestroy } from '@angular/core';
import { Subscription } from 'rxjs';
import { PartyPin } from '../../models/party-pin';
import { ClientService } from '../../services/client.service';
import { PlayerListService } from '../../services/player-list.service';

@Component({
  selector: 'app-party-pins',
  templateUrl: './party-pins.component.html',
  styleUrls: ['./party-pins.component.scss'],
})
export class PartyPinsComponent implements OnDestroy {
  pins: Array<PartyPin & { name?: string }> = [];
  private sub: Subscription;

  constructor(
    private readonly client: ClientService,
    private readonly playerList: PlayerListService,
  ) {
    this.sub = this.client.partyPinsChange.subscribe(pins => {
      // Attach names lazily using current player list if available
      this.pins = pins.map(p => ({
        ...p,
        name: this.playerList.getPlayerById(p.id)?.name,
      }));
    });
  }

  ngOnDestroy(): void {
    this.sub.unsubscribe();
  }
}

