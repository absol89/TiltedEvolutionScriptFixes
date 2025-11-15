import { Component, OnInit, NgZone } from '@angular/core';
import { Observable, BehaviorSubject } from 'rxjs';
import { ClientService } from '../../services/client.service';

@Component({
  selector: 'app-death-screen',
  templateUrl: './death-screen.component.html',
  styleUrls: ['./death-screen.component.scss'],
})
export class DeathScreenComponent implements OnInit {
  isVisible$: Observable<boolean>;
  secondsRemaining$: Observable<number>;
  isRespawnButtonEnabled$: Observable<boolean>;

  private visibilitySubject = new BehaviorSubject<boolean>(false);
  private timerSubject = new BehaviorSubject<number>(0);
  private buttonEnabledSubject = new BehaviorSubject<boolean>(false);

  constructor(private readonly clientService: ClientService, private readonly ngZone: NgZone) {
    // Initialize with false/0 values
    this.isVisible$ = this.visibilitySubject.asObservable();
    this.secondsRemaining$ = this.timerSubject.asObservable();
    this.isRespawnButtonEnabled$ = this.buttonEnabledSubject.asObservable();
  }

  ngOnInit(): void {
    // Track when death screen is shown - capture the initial timer value
    this.clientService.deathScreenChange.subscribe((secondsRemaining: number) => {
      this.ngZone.run(() => {
        this.timerSubject.next(secondsRemaining);
        this.visibilitySubject.next(true);
        this.buttonEnabledSubject.next(false);
      });
    });

    // Track timer updates
    this.clientService.deathTimerChange.subscribe((secondsRemaining: number) => {
      this.ngZone.run(() => {
        this.timerSubject.next(secondsRemaining);
      });
    });

    // Track respawn button enabled
    this.clientService.respawnButtonEnabledChange.subscribe(() => {
      this.ngZone.run(() => {
        this.buttonEnabledSubject.next(true);
      });
    });

    // Track death screen hidden
    this.clientService.deathScreenHiddenChange.subscribe(() => {
      this.ngZone.run(() => {
        this.visibilitySubject.next(false);
        this.timerSubject.next(0);
        this.buttonEnabledSubject.next(false);
      });
    });
  }

  onRespawnClicked(): void {
    try {
      this.clientService.respawnButtonClicked();
    } catch (error) {
      console.error('Error calling respawn button:', error);
    }
  }
}

