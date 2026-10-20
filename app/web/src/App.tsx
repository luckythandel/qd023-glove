import { useEffect, useMemo, useRef, useState } from "react";
import { useGloveSocket } from "./hooks/useGloveSocket";
import Hand from "./components/Hand";
import SettingsPanel from "./components/SettingsPanel";
import {
  ACTION_LABEL,
  DEFAULT_BINDINGS,
  DEFAULT_THRESHOLDS,
  FINGER_KEYS,
  type ActionId,
} from "./types";

/** Local fake stream when the user enables Simulate and the bridge is down. */
const SIM_CYCLE: number[][] = [
  [0, 0, 0, 0, 0],
  [100, 100, 100, 100, 100],
  [0, 0, 100, 100, 100],
  [0, 0, 0, 100, 100],
  [0, 100, 100, 100, 100],
  [100, 0, 0, 0, 0],
  [0, 0, 100, 100, 0],
  [0, 100, 0, 100, 100],
  [0, 100, 100, 100, 0],
];

export default function App() {
  const { state, sendSetConfig } = useGloveSocket();
  const [simOn, setSimOn] = useState(false);
  const [simPct, setSimPct] = useState<number[]>([0, 0, 0, 0, 0]);
  const simIdx = useRef(0);
  const [flash, setFlash] = useState<{ key: string; label: string } | null>(null);

  useEffect(() => {
    if (!simOn || state.wsConnected) return;
    const id = window.setInterval(() => {
      simIdx.current = (simIdx.current + 1) % SIM_CYCLE.length;
      setSimPct(SIM_CYCLE[simIdx.current]);
    }, 1400);
    return () => window.clearInterval(id);
  }, [simOn, state.wsConnected]);

  useEffect(() => {
    if (!state.lastAction) return;
    const { action, at } = state.lastAction;
    setFlash({ key: `${action}-${at}`, label: ACTION_LABEL[action as ActionId] ?? action });
    const id = window.setTimeout(() => setFlash(null), 1600);
    return () => window.clearTimeout(id);
  }, [state.lastAction]);

  const useSim = simOn && !state.wsConnected;
  const pct = useSim ? simPct : state.pct;
  const thresholds = useMemo(
    () => state.thresholds ?? DEFAULT_THRESHOLDS,
    [state.thresholds]
  );
  const custom = state.custom ?? [];
  const bent = pct.map((p, i) => p >= thresholds[FINGER_KEYS[i]].on);
  const pose = useSim ? guessPose(bent) : state.pose;

  const bindings = state.bindings ?? DEFAULT_BINDINGS;

  const connClass = !state.wsConnected
    ? "down"
    : state.bleConnected
      ? "up"
      : "amber";

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="dot" />
          QD023 <em>Glove Lab</em>
        </div>
        <div className={`conn conn-${connClass}`}>
          {!state.wsConnected
            ? "BRIDGE OFFLINE"
            : state.bleConnected
              ? `BLE · ${state.device}`
              : "BRIDGE UP · WAITING FOR GLOVE"}
        </div>
      </header>

      <main className="layout">
        <section className="stage">
          <Hand pct={pct} bent={bent} pose={pose} actionFlash={flash?.label ?? null} twist={state.twist} />
          {flash && (
            <div key={flash.key} className="toast">
              {flash.label}
            </div>
          )}
          <div className="fingers-table">
            {(["Thumb", "Index", "Middle", "Ring", "Pinky"] as const).map((n, i) => (
              <div key={n} className={`finger-cell ${bent[i] ? "bent" : ""}`}>
                <span className="fc-name">{n}</span>
                <span className="fc-pct">{Math.round(pct[i] ?? 0)}%</span>
                <div className="fc-bar">
                  <div
                    className="fc-fill"
                    style={{ width: `${Math.max(0, Math.min(100, pct[i] ?? 0))}%` }}
                  />
                  <div
                    className="fc-thresh"
                    style={{ left: `${thresholds[FINGER_KEYS[i]].on}%` }}
                  />
                </div>
              </div>
            ))}
          </div>
        </section>

        <SettingsPanel
          bindings={bindings}
          thresholds={thresholds}
          custom={custom}
          onPatch={sendSetConfig}
          onSimulate={() => setSimOn((v) => !v)}
          simulateOn={simOn}
          wsConnected={state.wsConnected}
        />
      </main>

      {state.cal === "NEED_CALIBRATION" && (
        <div className="banner">Glove needs calibration — press <code>c</code> on serial.</div>
      )}

      {state.logLines.length > 0 && (
        <footer className="logs">
          {state.logLines.slice(0, 4).map((l, i) => (
            <div key={i} className={`log log-${l.level}`}>
              {l.msg}
            </div>
          ))}
        </footer>
      )}
    </div>
  );
}

function guessPose(bent: boolean[]): string | null {
  const [t, i, m, r, p] = bent;
  if (bent.every(Boolean)) return "FIST";
  if (!bent.some(Boolean)) return "OPEN_PALM";
  if (t && !i && !m && !r && !p) return "THUMB_BENT";
  if (!t && i && m && r && p) return "THUMBS_UP";
  if (!i && m && r && p) return "INDEX_UP";
  if (!i && !m && r && p) return "PEACE";
  if (!m && i && r && p) return "MIDDLE_UP";
  if (i && m && r && !p) return "PINKY_UP";
  if (!i && m && r && !p) return "MID_RING";
  if (!i && !m && r && !p) return "RING_ONLY";
  return null;
}
