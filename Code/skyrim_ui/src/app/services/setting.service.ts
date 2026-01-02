import { Injectable } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject } from 'rxjs';
import { environment } from '../../environments/environment';
import { NametagMode } from '../models/nametag-mode.enum';
import { StoreService } from './store.service';

export enum FontSize {
  XS = 'xs',
  S = 's',
  M = 'm',
  L = 'l',
  XL = 'xl',
}

export enum PartyAnchor {
  TOP_LEFT = 0,
  TOP_RIGHT,
  BOTTOM_RIGHT,
  BOTTOM_LEFT,
}

export enum PartyLayout {
  CLASSIC = 'classic',
  COMPACT = 'compact',
}

export enum PlayerNamePreference {
  USERNAME = 'username',
  ACTOR = 'actor',
}

export const autoHideTimerLengths = [1, 3, 5];

export const fontSizeToPixels: Record<FontSize, number> = {
  [FontSize.XS]: 10,
  [FontSize.S]: 12,
  [FontSize.M]: 16,
  [FontSize.L]: 20,
  [FontSize.XL]: 26,
};

class Setting<T> extends BehaviorSubject<T> {
  constructor(
    private readonly storeService: StoreService,
    private storeKey: string,
    defaultValue: T,
  ) {
    super(defaultValue);
  }

  public next(value: T) {
    this.storeService.set(this.storeKey, value);
    super.next(value);
  }
}

class SliderSetting extends Setting<number> {
  constructor(
    storeService: StoreService,
    storeKey: string,
    defaultValue: number,
  ) {
    const initialValue = storeService.getFloat(storeKey, defaultValue);
    super(storeService, storeKey, initialValue);
  }
}

class ToggleSetting extends Setting<boolean> {
  constructor(
    storeService: StoreService,
    storeKey: string,
    defaultValue: boolean,
  ) {
    const initialValue = storeService.getBool(storeKey, defaultValue);
    super(storeService, storeKey, initialValue);
  }
}

class SelectSetting<T extends string | number> extends Setting<T> {
  constructor(
    storeService: StoreService,
    storeKey: string,
    private options: T[],
    defaultValue: T,
  ) {
    const storedValue =
      typeof defaultValue === 'number'
        ? storeService.getFloat(storeKey, defaultValue)
        : storeService.get(storeKey, defaultValue);
    const initialValue = options.includes(storedValue)
      ? storedValue
      : defaultValue;
    super(storeService, storeKey, initialValue);
  }

  public next(value: T) {
    if (this.options.includes(value)) {
      super.next(value);
    }
  }
}

@Injectable({
  providedIn: 'root',
})
export class SettingService {
  private readonly languageValues: string[] = Object.values(
    this.translocoService.getAvailableLangs(),
  ).map(lang => lang.id);
  private readonly fontSizeValues = Object.values(FontSize);
  private readonly partyAnchorValues = Object.values(
    PartyAnchor,
  ) as PartyAnchor[];
  private readonly partyLayoutValues = Object.values(PartyLayout);
  private readonly playerNamePreferenceValues =
    Object.values(PlayerNamePreference);
  private readonly autoHideTimeValues = autoHideTimerLengths;
  private readonly nametagModeValues: NametagMode[] = [
    NametagMode.Detailed,
    NametagMode.Basic,
    NametagMode.Hidden,
    NametagMode.Normal,
  ];

  public settings = {
    volume: new SliderSetting(this.storeService, 'audio_volume', 0.5),
    muted: new ToggleSetting(this.storeService, 'audio_muted', false),
    language: new SelectSetting(
      this.storeService,
      'language',
      this.languageValues,
      this.translocoService.getDefaultLang(),
    ),
    fontSize: new SelectSetting(
      this.storeService,
      'font_size',
      this.fontSizeValues,
      FontSize.M,
    ),
    nametagMode: new SelectSetting(
      this.storeService,
      'nametag_mode',
      this.nametagModeValues,
      NametagMode.Normal,
    ),
    playerNamePreference: new SelectSetting(
      this.storeService,
      'player_name_preference',
      this.playerNamePreferenceValues,
      PlayerNamePreference.USERNAME,
    ),
    isPartyShown: new ToggleSetting(this.storeService, 'party_isShown', true),
    autoHideParty: new ToggleSetting(
      this.storeService,
      'party_autoHide',
      false,
    ),
    autoHideTime: new SelectSetting(
      this.storeService,
      'party_autoHideTime',
      this.autoHideTimeValues,
      this.autoHideTimeValues[0],
    ),
    partyAnchor: new SelectSetting(
      this.storeService,
      'party_anchor',
      this.partyAnchorValues,
      PartyAnchor.TOP_LEFT,
    ),
    partyAnchorOffsetX: new SliderSetting(
      this.storeService,
      'party_anchor_offset_x',
      0,
    ),
    partyAnchorOffsetY: new SliderSetting(
      this.storeService,
      'party_anchor_offset_y',
      3,
    ),
    partyScale: new SliderSetting(this.storeService, 'party_scale', 1),
    partyLayout: new SelectSetting(
      this.storeService,
      'party_layout',
      this.partyLayoutValues,
      PartyLayout.CLASSIC,
    ),
    partyShowAvatar: new ToggleSetting(
      this.storeService,
      'party_show_avatar',
      true,
    ),
    partyShowName: new ToggleSetting(
      this.storeService,
      'party_show_name',
      true,
    ),
    partyShowLevel: new ToggleSetting(
      this.storeService,
      'party_show_level',
      true,
    ),
    partyShowHealth: new ToggleSetting(
      this.storeService,
      'party_show_health',
      true,
    ),
    partyPinShowAvatar: new ToggleSetting(
      this.storeService,
      'party_pin_show_avatar',
      true,
    ),
    partyPinScale: new SliderSetting(this.storeService, 'party_pin_scale', 1),
    isDebugShown: new ToggleSetting(this.storeService, 'debug_isShown', false),
  };

  constructor(
    private readonly storeService: StoreService,
    private readonly translocoService: TranslocoService,
  ) {
    this.settings.language.subscribe(lang =>
      translocoService.setActiveLang(lang),
    );
    this.settings.nametagMode.subscribe(mode => this.pushNametagMode(mode));
    this.settings.playerNamePreference.subscribe(pref =>
      this.pushPlayerNamePreference(pref),
    );
  }

  private pushNametagMode(mode: NametagMode): void {
    if (!environment.game) {
      return;
    }

    const api = (globalThis as any).skyrimtogether;
    if (api && typeof api.setNameTagMode === 'function') {
      api.setNameTagMode(mode);
    }
  }

  private pushPlayerNamePreference(pref: PlayerNamePreference): void {
    if (!environment.game) {
      return;
    }

    const api = (globalThis as any).skyrimtogether;
    if (api && typeof api.setPlayerNamePreference === 'function') {
      api.setPlayerNamePreference(pref);
    }
  }

  public resolvePlayerName(
    player?: { name?: string; actorName?: string },
    fallback = '',
  ): string {
    const preference = this.settings.playerNamePreference.getValue();
    if (
      preference === PlayerNamePreference.ACTOR &&
      player?.actorName &&
      player.actorName.length > 0
    ) {
      return player.actorName;
    }

    if (player?.name && player.name.length > 0) {
      return player.name;
    }

    if (player?.actorName && player.actorName.length > 0) {
      return player.actorName;
    }

    return fallback;
  }
}
