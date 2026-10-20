import { Injectable } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject } from 'rxjs';
import { StoreService } from './store.service';

export enum FontSize {
  XS = 'xs',
  S = 's',
  M = 'm',
  L = 'l',
  XL = 'xl',
}

export enum ScreenCorner {
  TOP_LEFT = 0,
  TOP_RIGHT,
  BOTTOM_RIGHT,
  BOTTOM_LEFT,
}

export enum PlayerUiFilter {
  ON = 'on',
  PARTY = 'party',
  OFF = 'off',
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
  private readonly screenCornerValues = Object.values(
    ScreenCorner,
  ) as ScreenCorner[];
  private readonly autoHideTimeValues = autoHideTimerLengths;
  private readonly playerUiFilterValues = Object.values(PlayerUiFilter);

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
      this.screenCornerValues,
      ScreenCorner.TOP_LEFT,
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
    isDebugShown: new ToggleSetting(this.storeService, 'debug_isShown', false),
    debugAnchor: new SelectSetting(
      this.storeService,
      'debug_anchor',
      this.screenCornerValues,
      ScreenCorner.TOP_LEFT,
    ),
    strangerNames: new ToggleSetting(
      this.storeService,
      'playerUi_strangerNames',
      false,
    ),
    partyInvites: new ToggleSetting(
      this.storeService,
      'playerUi_partyInvites',
      true,
    ),
    tradeInvites: new SelectSetting(
      this.storeService,
      'playerUi_tradeInvites',
      this.playerUiFilterValues,
      PlayerUiFilter.ON,
    ),
    miniChat: new SelectSetting(
      this.storeService,
      'playerUi_miniChat',
      this.playerUiFilterValues,
      PlayerUiFilter.ON,
    ),
    nameTags: new SelectSetting(
      this.storeService,
      'playerUi_nameTags',
      this.playerUiFilterValues,
      PlayerUiFilter.ON,
    ),
    nameTagLevel: new SelectSetting(
      this.storeService,
      'playerUi_nameTagLevel',
      this.playerUiFilterValues,
      PlayerUiFilter.ON,
    ),
    nameTagBackplate: new SelectSetting(
      this.storeService,
      'playerUi_nameTagBackplate',
      this.playerUiFilterValues,
      PlayerUiFilter.ON,
    ),
    nameTagSize: new SelectSetting(
      this.storeService,
      'playerUi_nameTagSize',
      this.fontSizeValues,
      FontSize.M,
    ),
    nameFadeStart: new SliderSetting(
      this.storeService,
      'playerUi_nameFadeStart',
      2000,
    ),
    nameInvisible: new SliderSetting(
      this.storeService,
      'playerUi_nameInvisible',
      3000,
    ),
    partyFadeStart: new SliderSetting(
      this.storeService,
      'playerUi_partyFadeStart',
      2000,
    ),
    partyInvisible: new SliderSetting(
      this.storeService,
      'playerUi_partyInvisible',
      3000,
    ),
  };

  /**
   * Per-username overrides of the stranger name setting, set with the eye in
   * the player list. Keyed by username so the choice follows that name across
   * servers and applies to everyone using it.
   */
  public nameOverrides = new BehaviorSubject<Record<string, boolean>>(
    this.loadNameOverrides(),
  );

  constructor(
    private readonly storeService: StoreService,
    private readonly translocoService: TranslocoService,
  ) {
    this.settings.language.subscribe(lang =>
      translocoService.setActiveLang(lang),
    );
  }

  /** Whether this name was hidden with the eye in the player list. */
  public isNameHidden(name: string): boolean {
    return this.nameOverrides.getValue()[name] === false;
  }

  public setNameOverride(name: string, revealed: boolean) {
    const overrides = { ...this.nameOverrides.getValue(), [name]: revealed };
    this.storeService.set(
      'playerUi_nameOverrides',
      JSON.stringify(overrides),
    );
    this.nameOverrides.next(overrides);
  }

  private loadNameOverrides(): Record<string, boolean> {
    try {
      const parsed = JSON.parse(
        this.storeService.get('playerUi_nameOverrides', '{}'),
      );
      return parsed && typeof parsed === 'object' ? parsed : {};
    } catch {
      return {};
    }
  }
}
