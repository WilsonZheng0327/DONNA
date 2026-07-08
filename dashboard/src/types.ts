// Mirrors what the hub writes into Firebase — see docs/04-protocol.md.
export interface DeskRecord {
  nodeId: number;
  occupied: boolean;
  distanceMm: number;
  batteryMv: number;
  seq: number;
  rssi: number;
  snr: number;
  lastSeenAt: number; // epoch ms, stamped by the RTDB server, not the hub
}

export interface HubRecord {
  lastSeenAt: number;
  ip?: string;
  wifiRssi?: number;
  uptimeS?: number;
}

export type DeskConfigMap = Record<string, { name?: string }>;

export type DeskStatus = "free" | "occupied" | "offline";

// A node heartbeats every ~30 s; three misses in a row = call it dead.
export const DESK_OFFLINE_AFTER_MS = 90_000;
// The hub heartbeats every 20 s.
export const HUB_OFFLINE_AFTER_MS = 60_000;
