// Mirrors the team schema the hub writes:
//   /{country}/{site}/{office}/{floor}/{deskId} -> DeskRecord
// Field names/units match the pre-existing records (last_updated = epoch
// SECONDS); everything beyond occupied/last_updated is our telemetry.
export interface DeskRecord {
  occupied: boolean;
  last_updated: number; // epoch seconds
  distance_mm?: number;
  battery_mv?: number;
  seq?: number;
  node_id?: number;
  rssi?: number;
  snr?: number;
}

// One office subtree: floor -> deskId -> record
export type OfficeTree = Record<string, Record<string, DeskRecord>>;

export interface HubRecord {
  last_updated: number; // epoch seconds
  ip?: string;
  wifi_rssi?: number;
  uptime_s?: number;
}

export type DeskStatus = "free" | "occupied" | "offline";

// A node heartbeats every ~30 s; three misses in a row = call it dead.
export const DESK_OFFLINE_AFTER_MS = 90_000;
// The hub heartbeats every 20 s.
export const HUB_OFFLINE_AFTER_MS = 60_000;

// The hub's self-test desk id — rendered with a friendlier label.
export const SELFTEST_DESK_ID = "_SELFTEST";
