import DeskCard from "./DeskCard";
import { databaseUrl } from "./firebase";
import { useDeskData } from "./useDeskData";
import {
  DESK_OFFLINE_AFTER_MS,
  HUB_OFFLINE_AFTER_MS,
  type DeskRecord,
  type DeskStatus,
} from "./types";

function deskStatus(desk: DeskRecord, serverNow: number): DeskStatus {
  if (!desk.lastSeenAt || serverNow - desk.lastSeenAt > DESK_OFFLINE_AFTER_MS)
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
      <p className="panel__title">// connected — waiting for first desk data</p>
      <p className="panel__hint">
        Power a node, or fake one from your shell to see the board light up
        (curl commands in <b>docs/05-firebase.md</b>).
      </p>
    </div>
  );
}

export default function App() {
  const { desks, hub, config, connected, serverNow } = useDeskData();

  if (!databaseUrl) {
    return (
      <main className="shell">
        <Header free={0} occupied={0} offline={0} live={false} serverNow={Date.now()} />
        <SetupPanel />
      </main>
    );
  }

  const entries = Object.entries(desks)
    .filter(([, d]) => d && typeof d === "object")
    .sort(([, a], [, b]) => (a.nodeId ?? 0) - (b.nodeId ?? 0));

  const statuses = entries.map(([, d]) => deskStatus(d, serverNow));
  const free = statuses.filter((s) => s === "free").length;
  const occupied = statuses.filter((s) => s === "occupied").length;
  const offline = statuses.filter((s) => s === "offline").length;

  const hubOnline =
    hub != null && serverNow - hub.lastSeenAt < HUB_OFFLINE_AFTER_MS;

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
          {hub ? ` — last heard ${Math.round((serverNow - hub.lastSeenAt) / 1000)}s ago` : " — never seen"}
          . Desk states below may be stale.
        </div>
      )}

      {entries.length === 0 ? (
        <WaitingPanel />
      ) : (
        <section className="grid">
          {entries.map(([key, desk], i) => (
            <DeskCard
              key={key}
              desk={desk}
              name={config[key]?.name ?? `Desk ${desk.nodeId ?? "?"}`}
              status={statuses[i]}
              serverNow={serverNow}
              index={i}
            />
          ))}
        </section>
      )}

      <footer className="foot">
        <span>{connected ? "db link up" : "db link down"}</span>
        {hub?.ip && <span>hub {hub.ip} · wifi {hub.wifiRssi} dBm</span>}
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
