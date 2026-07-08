import type { DeskRecord, DeskStatus } from "./types";

function timeAgo(ms: number): string {
  if (ms < 0) ms = 0;
  const s = Math.floor(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m`;
  return `${Math.floor(m / 60)}h`;
}

// Rough usable-signal buckets for LoRa at SF9 (sensitivity ≈ −123 dBm).
function rssiBars(rssi: number): number {
  if (rssi > -85) return 4;
  if (rssi > -100) return 3;
  if (rssi > -112) return 2;
  return 1;
}

const STATUS_LABEL: Record<DeskStatus, string> = {
  free: "FREE",
  occupied: "IN USE",
  offline: "OFFLINE",
};

interface Props {
  desk: DeskRecord;
  name: string;
  status: DeskStatus;
  serverNow: number;
  index: number;
}

export default function DeskCard({ desk, name, status, serverNow, index }: Props) {
  const bars = rssiBars(desk.rssi ?? -120);
  return (
    <article
      className={`card card--${status}`}
      style={{ animationDelay: `${index * 45}ms` }}
    >
      <header className="card__head">
        <span className="card__name">{name}</span>
        <span className="card__id">N{String(desk.nodeId ?? "?").padStart(2, "0")}</span>
      </header>

      {/* key= makes React remount on status change, replaying the flash */}
      <div className="card__status" key={status}>
        {STATUS_LABEL[status]}
      </div>

      <footer className="card__meta">
        <span title="time since last packet from this node">
          {status === "offline" ? "lost " : ""}
          {timeAgo(serverNow - (desk.lastSeenAt ?? 0))}
          {status === "offline" ? " ago" : ""}
        </span>
        <span
          className="card__bars"
          title={`RSSI ${desk.rssi?.toFixed?.(0) ?? "?"} dBm · SNR ${desk.snr?.toFixed?.(1) ?? "?"} dB · seq ${desk.seq ?? "?"}`}
        >
          {[1, 2, 3, 4].map((b) => (
            <i key={b} className={b <= bars ? "on" : ""} style={{ height: 3 + b * 3 }} />
          ))}
        </span>
        <span title="raw ToF distance behind the decision">
          {desk.distanceMm ?? "—"} mm
        </span>
      </footer>
    </article>
  );
}
