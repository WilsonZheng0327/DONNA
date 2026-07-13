import DeskCard from './DeskCard';
import {databaseUrl} from './firebase';
import {
  DESK_OFFLINE_AFTER_MS,
  HUB_OFFLINE_AFTER_MS,
  SELFTEST_DESK_ID,
  type DeskRecord,
  type DeskStatus,
} from './types';
import {OFFICE_PATH, useDeskData} from './useDeskData';

function deskStatus(desk: DeskRecord, serverNow: number): DeskStatus {
  // last_updated is epoch seconds (team schema); serverNow is ms.
  if (
    !desk.last_updated ||
    serverNow - desk.last_updated * 1000 > DESK_OFFLINE_AFTER_MS
  )
    return 'offline';

  const isBooked =
    desk.hold_user &&
    desk.hold_expires &&
    desk.hold_expires * 1000 > serverNow;

  if (isBooked) {
    if (desk.occupied) {
      return 'occupied';
    }
    if (desk.in_meeting) {
      return 'in-meeting';
    }
    return 'reserved';
  }

  return desk.occupied ? 'occupied' : 'free';
}

function Icon({name}: {name: string}) {
  return <span className="material-symbols-outlined">{name}</span>;
}

function Clock({serverNow}: {serverNow: number}) {
  const d = new Date(serverNow);
  const p = (n: number) => String(n).padStart(2, '0');
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
      <p className="panel__title">
        <Icon name="settings" />
        Not configured
      </p>
      <pre className="panel__code">{`cp .env.example .env.local
$EDITOR .env.local     # paste your Firebase database URL
bun run dev            # restart`}</pre>
      <p className="panel__hint">
        Create the database first if you haven't — see{' '}
        <b>docs/05-firebase.md</b>.
      </p>
    </div>
  );
}

function WaitingPanel() {
  return (
    <div className="panel">
      <p className="panel__title">
        <Icon name="cloud_done" />
        Connected — no desks under {OFFICE_PATH} yet
      </p>
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
  const {office, hub, connected, serverNow} = useDeskData();

  if (!databaseUrl) {
    return (
      <main className="shell">
        <Header live={false} serverNow={Date.now()} />
        <div className="container">
          <SetupPanel />
        </div>
      </main>
    );
  }

  // floor -> desks, sorted so the board is spatially stable.
  const floors = Object.keys(office).sort((a, b) =>
    a.localeCompare(b, undefined, {numeric: true})
  );
  const flat: FlatDesk[] = floors.flatMap((floor) =>
    Object.entries(office[floor] ?? {})
      // _SELFTEST is a backend-only liveness probe; never show it on the board.
      .filter(([deskId]) => deskId !== SELFTEST_DESK_ID)
      .filter(([, rec]) => rec && typeof rec === 'object')
      .sort(([a], [b]) => a.localeCompare(b, undefined, {numeric: true}))
      .map(([deskId, rec]) => ({
        floor,
        deskId,
        rec,
        status: deskStatus(rec, serverNow),
      }))
  );

  const free = flat.filter((d) => d.status === 'free').length;
  const occupied = flat.filter(
    (d) =>
      d.status === 'occupied' ||
      d.status === 'reserved' ||
      d.status === 'in-meeting'
  ).length;
  const offline = flat.filter((d) => d.status === 'offline').length;

  const hubOnline =
    hub != null && serverNow - hub.last_updated * 1000 < HUB_OFFLINE_AFTER_MS;

  return (
    <main className="shell">
      <Header live={connected && hubOnline} serverNow={serverNow} />

      <div className="container">
        <section className="summary">
          <div className="kpi kpi--free">
            <span className="kpi__icon">
              <Icon name="event_available" />
            </span>
            <div className="kpi__body">
              <span className="kpi__num">{free}</span>
              <span className="kpi__label">Available</span>
            </div>
          </div>
          <div className="kpi kpi--occ">
            <span className="kpi__icon">
              <Icon name="event_busy" />
            </span>
            <div className="kpi__body">
              <span className="kpi__num">{occupied}</span>
              <span className="kpi__label">In use</span>
            </div>
          </div>
          <div className="kpi kpi--off">
            <span className="kpi__icon">
              <Icon name="sensors_off" />
            </span>
            <div className="kpi__body">
              <span className="kpi__num">{offline}</span>
              <span className="kpi__label">Offline</span>
            </div>
          </div>
        </section>

        {!hubOnline && (
          <div className="alert">
            <Icon name="warning" />
            <span>
              Hub offline
              {hub
                ? ` — last heard ${Math.round(serverNow / 1000 - hub.last_updated)}s ago`
                : ' — never seen'}
              . Desk states below may be stale.
            </span>
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
                  <Icon name="layers" />
                  Floor <b>{floor}</b>
                  <span className="floor__tally">
                    {floorDesks.filter((d) => d.status === 'free').length}{' '}
                    available · {floorDesks.length} desks
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
          <span>
            <Icon name={connected ? 'cloud_done' : 'cloud_off'} />
            {connected ? 'Database connected' : 'Database offline'}
          </span>
          <span>{OFFICE_PATH}</span>
          {hub?.ip && (
            <span>
              Hub {hub.ip} · Wi-Fi {hub.wifi_rssi} dBm
            </span>
          )}
          <span className="foot__spacer" />
          <span>
            <b>DONNA</b> desk availability
          </span>
        </footer>
      </div>
    </main>
  );
}

function Header(props: {live: boolean; serverNow: number}) {
  return (
    <header className="head">
      <div className="head__inner container">
        <div className="head__brand">
          <span className="head__mark">
            <Icon name="desk" />
          </span>
          <div>
            <h1 className="wordmark">
              <i>D</i>
              <i>O</i>
              <i>N</i>
              <i>N</i>
              <i>A</i>
            </h1>
            <div className="head__sub">
              Desk availability · {OFFICE_PATH.split('/').pop()}
            </div>
          </div>
        </div>
        <span className="head__spacer" />
        <div className="head__right">
          <span className={`live ${props.live ? 'live--on' : ''}`}>
            <span className="live__dot" />
            {props.live ? 'Live' : 'Reconnecting'}
          </span>
          <Clock serverNow={props.serverNow} />
        </div>
      </div>
    </header>
  );
}
