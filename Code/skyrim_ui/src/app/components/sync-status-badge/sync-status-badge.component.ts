import { Component } from '@angular/core';
import { ClientService } from '../../services/client.service';

@Component({
  selector: 'app-sync-status-badge',
  templateUrl: './sync-status-badge.component.html',
  styleUrls: ['./sync-status-badge.component.scss'],
})
export class SyncStatusBadgeComponent {
  public readonly status$ = this.client.syncStatusChange.asObservable();

  public constructor(private readonly client: ClientService) {}
}
