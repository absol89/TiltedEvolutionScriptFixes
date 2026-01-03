import { Component } from '@angular/core';
import { Observable } from 'rxjs';
import { PartyOptions } from '../../models/party-options';
import { PartyOptionsService } from '../../services/party-options.service';

@Component({
  selector: 'app-party-options',
  templateUrl: './party-options.component.html',
  styleUrls: ['./party-options.component.scss'],
})
export class PartyOptionsComponent {
  public options$: Observable<PartyOptions>;
  public isLeader$: Observable<boolean>;
  public inParty$: Observable<boolean>;
  public isExpanded = true;

  public constructor(private readonly partyOptions: PartyOptionsService) {
    this.options$ = this.partyOptions.options$;
    this.isLeader$ = this.partyOptions.isLeader$;
    this.inParty$ = this.partyOptions.inParty$;
  }

  public toggleExpanded(): void {
    this.isExpanded = !this.isExpanded;
  }

  public updateOption(key: keyof PartyOptions, value: boolean): void {
    this.partyOptions.updateOption(key, value);
  }
}
