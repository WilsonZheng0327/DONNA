import { useEffect, useState } from "react";
import { onValue, ref } from "firebase/database";
import { db } from "./firebase";
import type { DeskConfigMap, DeskRecord, HubRecord } from "./types";

export function useDeskData() {
  const [desks, setDesks] = useState<Record<string, DeskRecord>>({});
  const [hub, setHub] = useState<HubRecord | null>(null);
  const [config, setConfig] = useState<DeskConfigMap>({});
  const [serverOffset, setServerOffset] = useState(0);
  const [connected, setConnected] = useState(false);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (!db) return;
    // onValue keeps a websocket open; every write by the hub lands here
    // within ~100 ms. No polling anywhere.
    const unsubs = [
      onValue(ref(db, "desks"), (s) => setDesks(s.val() ?? {})),
      onValue(ref(db, "hub"), (s) => setHub(s.val() ?? null)),
      onValue(ref(db, "config/desks"), (s) => setConfig(s.val() ?? {})),
      // Difference between our clock and the database's clock, so staleness
      // math works even on a wall machine with a drifting clock.
      onValue(ref(db, ".info/serverTimeOffset"), (s) => setServerOffset(s.val() ?? 0)),
      onValue(ref(db, ".info/connected"), (s) => setConnected(s.val() === true)),
    ];
    return () => unsubs.forEach((u) => u());
  }, []);

  // 1 Hz tick drives the clock and the "Ns ago" / offline computations.
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(t);
  }, []);

  return { desks, hub, config, connected, serverNow: now + serverOffset };
}
