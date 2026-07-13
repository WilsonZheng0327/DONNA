import {onValue, ref} from 'firebase/database';
import {useEffect, useState} from 'react';
import {db} from './firebase';
import type {HubRecord, OfficeTree} from './types';

// Which office subtree this board displays, e.g. "US/SVL/CRBN100".
export const OFFICE_PATH =
  (import.meta.env.VITE_OFFICE_PATH as string | undefined) ?? 'US/SVL/CRBN100';

export function useDeskData() {
  const [office, setOffice] = useState<OfficeTree>({});
  const [hub, setHub] = useState<HubRecord | null>(null);
  const [serverOffset, setServerOffset] = useState(0);
  const [connected, setConnected] = useState(false);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (!db) return;
    // onValue keeps a websocket open; every write by the hub lands here
    // within ~100 ms. No polling anywhere.
    const unsubs = [
      onValue(ref(db, OFFICE_PATH), (s) => setOffice(s.val() ?? {})),
      onValue(ref(db, 'hub'), (s) => setHub(s.val() ?? null)),
      // Difference between our clock and the database's clock, so staleness
      // math works even on a wall machine with a drifting clock.
      onValue(ref(db, '.info/serverTimeOffset'), (s) =>
        setServerOffset(s.val() ?? 0),
      ),
      onValue(ref(db, '.info/connected'), (s) =>
        setConnected(s.val() === true),
      ),
    ];
    return () => unsubs.forEach((u) => u());
  }, []);

  // 1 Hz tick drives the clock and the "Ns ago" / offline computations.
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(t);
  }, []);

  return {office, hub, connected, serverNow: now + serverOffset};
}
