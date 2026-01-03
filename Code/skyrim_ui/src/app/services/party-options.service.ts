import { Injectable, OnDestroy } from '@angular/core';
import { BehaviorSubject, Subscription } from 'rxjs';
import { environment } from '../../environments/environment';
import { PartyInfo } from '../models/party-info';
import { DEFAULT_PARTY_OPTIONS, PartyOptions } from '../models/party-options';
import { ClientService } from './client.service';
import { StoreService } from './store.service';

@Injectable({
  providedIn: 'root',
})
export class PartyOptionsService implements OnDestroy {
  public options$ = new BehaviorSubject<PartyOptions>(DEFAULT_PARTY_OPTIONS);
  public isLeader$ = new BehaviorSubject<boolean>(false);
  public inParty$ = new BehaviorSubject<boolean>(false);

  private storedOptions = DEFAULT_PARTY_OPTIONS;
  private partyInfoSubscription: Subscription;
  private partyLeftSubscription: Subscription;
  private partyOptionsSubscription: Subscription;

  constructor(
    private readonly clientService: ClientService,
    private readonly storeService: StoreService,
  ) {
    this.storedOptions = this.loadStoredOptions();
    this.options$.next(this.storedOptions);

    this.partyOptionsSubscription =
      this.clientService.partyOptionsChange.subscribe(options => {
        this.options$.next(options);
      });

    this.partyInfoSubscription = this.clientService.partyInfoChange.subscribe(
      partyInfo => this.handlePartyInfo(partyInfo),
    );

    this.partyLeftSubscription = this.clientService.partyLeftChange.subscribe(
      () => this.handlePartyLeft(),
    );
  }

  public ngOnDestroy(): void {
    this.partyInfoSubscription.unsubscribe();
    this.partyLeftSubscription.unsubscribe();
    this.partyOptionsSubscription.unsubscribe();
  }

  public updateOption(key: keyof PartyOptions, value: boolean): void {
    const nextOptions = { ...this.options$.getValue(), [key]: value };
    this.options$.next(nextOptions);
    this.storeOptions(nextOptions);

    if (this.isLeader$.getValue()) {
      this.pushPartyOptions(nextOptions);
    }
  }

  private handlePartyInfo(partyInfo: PartyInfo): void {
    this.inParty$.next(true);

    const localId = this.clientService.localPlayerId;
    const isLeader = localId !== undefined && localId === partyInfo.leaderId;
    const wasLeader = this.isLeader$.getValue();

    this.isLeader$.next(isLeader);

    if (isLeader && !wasLeader) {
      const stored = this.loadStoredOptions();
      this.options$.next(stored);
      this.pushPartyOptions(stored);
    }
  }

  private handlePartyLeft(): void {
    this.inParty$.next(false);
    this.isLeader$.next(false);
    this.options$.next(this.storedOptions);
  }

  private loadStoredOptions(): PartyOptions {
    const raw = this.storeService.get('party_options', '');
    if (!raw) {
      return { ...DEFAULT_PARTY_OPTIONS };
    }

    try {
      const parsed = JSON.parse(raw);
      return {
        syncFastTravelMarkers:
          typeof parsed.syncFastTravelMarkers === 'boolean'
            ? parsed.syncFastTravelMarkers
            : DEFAULT_PARTY_OPTIONS.syncFastTravelMarkers,
        showPartyMemberMarkers:
          typeof parsed.showPartyMemberMarkers === 'boolean'
            ? parsed.showPartyMemberMarkers
            : DEFAULT_PARTY_OPTIONS.showPartyMemberMarkers,
        syncDeadBodyLoot:
          typeof parsed.syncDeadBodyLoot === 'boolean'
            ? parsed.syncDeadBodyLoot
            : DEFAULT_PARTY_OPTIONS.syncDeadBodyLoot,
      };
    } catch {
      return { ...DEFAULT_PARTY_OPTIONS };
    }
  }

  private storeOptions(options: PartyOptions): void {
    this.storedOptions = { ...options };
    this.storeService.set('party_options', JSON.stringify(options));
  }

  private pushPartyOptions(options: PartyOptions): void {
    if (!environment.game) {
      return;
    }

    const api = (globalThis as any).skyrimtogether;
    if (api && typeof api.setPartyOptions === 'function') {
      api.setPartyOptions({
        syncFastTravelMarkers: options.syncFastTravelMarkers,
        showPartyMemberMarkers: options.showPartyMemberMarkers,
        syncDeadBodyLoot: options.syncDeadBodyLoot,
      });
    }
  }
}
