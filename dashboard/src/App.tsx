import DeskCard from "./DeskCard";
import { databaseUrl } from "./firebase";
import { OFFICE_PATH, useDeskData } from "./useDeskData";
import {
  DESK_OFFLINE_AFTER_MS,
  HUB_OFFLINE_AFTER_MS,
  type DeskRecord,
  type DeskStatus,
} from "./types";

function deskStatus(desk: DeskRecord, serverNow: number): DeskStatus {
  // last_updated is epoch seconds (team schema); serverNow is ms.
  if (!desk.last_updated || serverNow - desk.last_updated * 1000 > DESK_OFFLINE_AFTER_MS)
    return "offline";
  return desk.occupied ? "occupied" : "free";
}

function Clock({ serverNow }: { serverNow: number }) {
  const d = new Date(serverNow);
  const p = (n: number) => String(n).padStart(2, "0");
  return (
    <time className="clock">
      {p(d.getHours())}:{p(d.getMinutes())}
      <span className="clock__s">:{p(d.getSeconds())}</span>
    </time>
  );
}

function SetupPanel() {
  return (
    <div className="panel">
      <p className="panel__title">// not configured</p>
      <pre className="panel__code">{`cp .env.example .env.local
$EDITOR .env.local     # paste your Firebase database URL
npm run dev            # restart`}</pre>
      <p className="panel__hint">
        Create the database first if you haven't — see <b>docs/05-firebase.md</b>.
      </p>
    </div>
  );
}

function WaitingPanel() {
  return (
    <div className="panel">
      <p className="panel__title">// connected — no desks under {OFFICE_PATH} yet</p>
      <p className="panel__hint">
        Power the hub (its self-test desk appears within ~10 s), power a node,
        or fake a desk with curl (<b>docs/05-firebase.md</b>).
      </p>
    </div>
  );
}

interface FlatDesk {
  floor: string;
  deskId: string;
  rec: DeskRecord;
  status: DeskStatus;
}

export default function App() {
  const { office, hub, connected, serverNow } = useDeskData();

  if (!databaseUrl) {
    return (
      <main className="shell">
        <Header free={0} occupied={0} offline={0} live={false} serverNow={Date.now()} />
        <SetupPanel />
      </main>
    );
  }

  // floor -> desks, sorted so the board is spatially stable.
  const floors = Object.keys(office).sort((a, b) =>
    a.localeCompare(b, undefined, { numeric: true }),
  );
  const flat: FlatDesk[] = floors.flatMap((floor) =>
    Object.entries(office[floor] ?? {})
      .filter(([, rec]) => rec && typeof rec === "object")
      .sort(([a], [b]) => a.localeCompare(b, undefined, { numeric: true }))
      .map(([deskId, rec]) => ({
        floor,
        deskId,
        rec,
        status: deskStatus(rec, serverNow),
      })),
  );

  const free = flat.filter((d) => d.status === "free").length;
  const occupied = flat.filter((d) => d.status === "occupied").length;
  const offline = flat.filter((d) => d.status === "offline").length;

  const hubOnline =
    hub != null && serverNow - hub.last_updated * 1000 < HUB_OFFLINE_AFTER_MS;

  return (
    <main className="shell">
      <Header
        free={free}
        occupied={occupied}
        offline={offline}
        live={connected && hubOnline}
        serverNow={serverNow}
      />

      {!hubOnline && (
        <div className="alert">
          <span className="alert__dot" />
          hub offline
          {hub
            ? ` — last heard ${Math.round(serverNow / 1000 - hub.last_updated)}s ago`
            : " — never seen"}
          . Desk states below may be stale.
        </div>
      )}

      {flat.length === 0 ? (
        <WaitingPanel />
      ) : (
        floors.map((floor) => {
          const floorDesks = flat.filter((d) => d.floor === floor);
          if (floorDesks.length === 0) return null;
          return (
            <section key={floor} className="floor">
              <h2 className="floor__label">
                floor <b>{floor}</b>
                <span className="floor__tally">
                  {floorDesks.filter((d) => d.status === "free").length} free ·{" "}
                  {floorDesks.length} desks
                </span>
              </h2>
              <div className="grid">
                {floorDesks.map((d, i) => (
                  <DeskCard
                    key={`${d.floor}/${d.deskId}`}
                    deskId={d.deskId}
                    rec={d.rec}
                    status={d.status}
                    serverNow={serverNow}
                    index={i}
                  />
                ))}
              </div>
            </section>
          );
        })
      )}

      <footer className="foot">
        <span>{connected ? "db link up" : "db link down"}</span>
        <span>{OFFICE_PATH}</span>
        {hub?.ip && <span>hub {hub.ip} · wifi {hub.wifi_rssi} dBm</span>}
        <span>deskfinder</span>
      </footer>
    </main>
  );
}

function Header(props: {
  free: number;
  occupied: number;
  offline: number;
  live: boolean;
  serverNow: number;
}) {
  return (
    <header className="head">
      <div className="head__brand">
        <span className={`head__dot ${props.live ? "head__dot--live" : ""}`} />
        <h1>
          DESK<span>FINDER</span>
        </h1>
        <span className="head__office">{OFFICE_PATH.split("/").pop()}</span>
      </div>
      <div className="head__counts">
        <div className="count count--free">
          <b>{props.free}</b>
          <span>free</span>
        </div>
        <div className="count count--occ">
          <b>{props.occupied}</b>
          <span>in use</span>
        </div>
        <div className="count count--off">
          <b>{props.offline}</b>
          <span>offline</span>
        </div>
      </div>
      <Clock serverNow={props.serverNow} />
    </header>
  );
}
