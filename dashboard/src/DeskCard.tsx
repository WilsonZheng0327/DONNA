import {type DeskRecord, type DeskStatus} from './types';

function timeAgo(ms: number): string {
  if (ms < 0) ms = 0;
  const s = Math.floor(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m`;
  return `${Math.floor(m / 60)}h`;
}

function formatTime(epochSeconds: number): string {
  const d = new Date(epochSeconds * 1000);
  const p = (n: number) => String(n).padStart(2, '0');
  return `${p(d.getHours())}:${p(d.getMinutes())}`;
}

// Rough usable-signal buckets for LoRa at SF9 (sensitivity ≈ −123 dBm).
function rssiBars(rssi: number): number {
  if (rssi > -85) return 4;
  if (rssi > -100) return 3;
  if (rssi > -112) return 2;
  return 1;
}

const STATUS_LABEL: Record<DeskStatus, string> = {
  free: 'Available',
  occupied: 'Occupied',
  offline: 'Offline',
  reserved: 'Reserved Hold',
  'in-meeting': 'In Meeting (Away)',
};

interface Props {
  deskId: string;
  rec: DeskRecord;
  status: DeskStatus;
  serverNow: number;
  index: number;
}

export default function DeskCard({
  deskId,
  rec,
  status,
  serverNow,
  index,
}: Props) {
  const bars = rssiBars(rec.rssi ?? -120);
  const agoMs = serverNow - (rec.last_updated ?? 0) * 1000;
  const isBooked =
    rec.hold_user &&
    rec.hold_expires &&
    rec.hold_expires * 1000 > serverNow;
  return (
    <article
      className={`card card--${status}`}
      style={{animationDelay: `${index * 45}ms`}}>
      <header className="card__head">
        <span className="card__name">
          <span className="material-symbols-outlined">chair</span>
          <span>{deskId}</span>
        </span>
        {rec.node_id != null && (
          <span className="card__id">
            N{String(rec.node_id).padStart(2, '0')}
          </span>
        )}
      </header>

      {/* key= makes React remount on status change, replaying the flash */}
      <div className="card__status" key={status}>
        {STATUS_LABEL[status]}
        {isBooked
          ? ` · ${rec.hold_user}${rec.hold_expires ? ` (until ${formatTime(rec.hold_expires)})` : ''}`
          : ''}
      </div>

      <footer className="card__meta">
        <span title="time since last update for this desk">
          {status === 'offline' ? 'lost ' : ''}
          {timeAgo(agoMs)}
          {status === 'offline' ? ' ago' : ''}
        </span>
        <span
          className="card__bars"
          title={`RSSI ${rec.rssi?.toFixed?.(0) ?? '?'} dBm · SNR ${rec.snr?.toFixed?.(1) ?? '?'} dB · seq ${rec.seq ?? '?'}`}>
          {[1, 2, 3, 4].map((b) => (
            <i
              key={b}
              className={b <= bars ? 'on' : ''}
              style={{height: 3 + b * 3}}
            />
          ))}
        </span>
        <span title="raw ToF distance behind the decision">
          {rec.distance_mm ?? '—'} mm
        </span>
      </footer>
    </article>
  );
}
