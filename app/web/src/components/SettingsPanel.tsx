import { useEffect, useRef, useState } from "react";
import {
  ACTION_IDS,
  ACTION_LABEL,
  FINGER_KEYS,
  FINGER_LABEL,
  GESTURE_IDS,
  GESTURE_LABEL,
  type ActionId,
  type Bindings,
  type ConfigPatch,
  type CustomGesture,
  type FingerKey,
  type GestureId,
  type PoseCell,
  type RotDir,
  type Thresholds,
} from "../types";

type Props = {
  bindings: Bindings;
  thresholds: Thresholds;
  custom: CustomGesture[];
  onPatch: (patch: ConfigPatch) => void;
  onSimulate: () => void;
  simulateOn: boolean;
  wsConnected: boolean;
};

const EMPTY_PATTERN: PoseCell[] = ["X", "X", "X", "X", "X"];

export default function SettingsPanel({
  bindings,
  thresholds,
  custom,
  onPatch,
  onSimulate,
  simulateOn,
  wsConnected,
}: Props) {
  const [tDraft, setTDraft] = useState<Thresholds | null>(null);
  const t = tDraft ?? thresholds;
  const debounceRef = useRef<number | null>(null);
  const [gName, setGName] = useState("");
  const [gPattern, setGPattern] = useState<PoseCell[]>([...EMPTY_PATTERN]);
  const [gAction, setGAction] = useState<ActionId>("mouse_click");
  const [gRot, setGRot] = useState<RotDir>("none");

  useEffect(() => {
    setTDraft(null);
  }, [thresholds]);

  useEffect(
    () => () => {
      if (debounceRef.current) window.clearTimeout(debounceRef.current);
    },
    []
  );

  const setBinding = (g: GestureId, a: ActionId) =>
    onPatch({ bindings: { ...bindings, [g]: a } });

  const setOn = (k: FingerKey, raw: number) => {
    const on = Math.max(15, Math.min(95, raw));
    const next: Thresholds = { ...t, [k]: { on, off: Math.max(5, on - 26) } };
    setTDraft(next);
    if (debounceRef.current) window.clearTimeout(debounceRef.current);
    debounceRef.current = window.setTimeout(() => {
      debounceRef.current = null;
      onPatch({ thresholds: next });
    }, 250);
  };

  const removeGesture = (id: string) =>
    onPatch({ custom: custom.filter((c) => c.id !== id) });

  const addGesture = () => {
    const name = gName.trim().slice(0, 24) || "Custom";
    const id =
      typeof crypto !== "undefined" && typeof crypto.randomUUID === "function"
        ? crypto.randomUUID()
        : `g-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
    onPatch({
      custom: [...custom, { id, name, pattern: [...gPattern], action: gAction, rot: gRot }],
    });
    setGName("");
    setGRot("none");
  };

  return (
    <aside className="panel">
      <h2>Gesture → Action</h2>
      <p className="hint">
        Hold a pose ~150&nbsp;ms to fire. 400&nbsp;ms cooldown (volume/mouse repeat while
        held). Built-in gestures take priority if a custom pattern matches them.
      </p>

      <div className="bindings">
        {GESTURE_IDS.map((g) => (
          <label key={g} className="bind-row">
            <span className="bind-gesture">{GESTURE_LABEL[g]}</span>
            <span className="bind-arrow">→</span>
            <select
              value={bindings[g]}
              onChange={(e) => setBinding(g, e.target.value as ActionId)}
            >
              {ACTION_IDS.map((a) => (
                <option key={a} value={a}>
                  {ACTION_LABEL[a]}
                </option>
              ))}
            </select>
          </label>
        ))}
      </div>

      <h2>Finger Sensitivity</h2>
      <div className="sens-list">
        {FINGER_KEYS.map((k) => (
          <div key={k} className="sens-row">
            <label htmlFor={`sens-${k}`}>{FINGER_LABEL[k]}</label>
            <input
              id={`sens-${k}`}
              type="range"
              min={15}
              max={95}
              step={1}
              value={t[k].on}
              onChange={(e) => setOn(k, Number(e.target.value))}
            />
            <span className="sens-val">{t[k].on}%</span>
          </div>
        ))}
      </div>

      <h2>Custom Gestures</h2>
      <p className="hint">
        Match = your 5 finger states (X = don't care). Fires after ~150&nbsp;ms hold. Max
        12.
      </p>
      <div className="cg-list">
        {custom.length === 0 && (
          <p className="cg-empty">No custom gestures yet — add one below.</p>
        )}
        {custom.map((g) => (
          <div key={g.id} className="cg-row">
            <span className="cg-name">{g.name}</span>
            <span className="cg-chips">
              {g.pattern.map((c, i) => (
                <span
                  key={i}
                  className={`cg-chip ${c === "B" ? "on" : ""}`}
                  title={FINGER_LABEL[FINGER_KEYS[i]]}
                >
                  {c}
                </span>
              ))}
            </span>
            {g.rot && g.rot !== "none" && (
              <span
                className={`cg-rot cg-rot-${g.rot}`}
                title={g.rot === "cw" ? "Requires clockwise wrist turn" : "Requires counter-clockwise wrist turn"}
              >
                {g.rot === "cw" ? "↻" : "↺"}
              </span>
            )}
            <span className="cg-act">{ACTION_LABEL[g.action]}</span>
            <button
              type="button"
              className="cg-del"
              onClick={() => removeGesture(g.id)}
              title="Delete"
            >
              ✕
            </button>
          </div>
        ))}
      </div>

      <div className="cg-add">
        <input
          className="cg-name-input"
          placeholder="Gesture name (max 24)"
          maxLength={24}
          value={gName}
          onChange={(e) => setGName(e.target.value)}
        />
        <div className="cg-add-grid">
          {FINGER_KEYS.map((k, i) => (
            <div key={k} className="cg-fcell">
              <span>{FINGER_LABEL[k][0]}</span>
              <select
                aria-label={FINGER_LABEL[k]}
                value={gPattern[i]}
                onChange={(e) => {
                  const next = [...gPattern];
                  next[i] = e.target.value as PoseCell;
                  setGPattern(next);
                }}
              >
                <option value="X">Any</option>
                <option value="B">Bent</option>
                <option value="E">Ext</option>
              </select>
            </div>
          ))}
        </div>
        <label className="cg-rot-row">
          <span>Wrist rotation</span>
          <select
            value={gRot}
            onChange={(e) => setGRot(e.target.value as RotDir)}
            aria-label="Wrist rotation requirement"
          >
            <option value="none">None</option>
            <option value="cw">Clockwise ≥45°</option>
            <option value="ccw">Counter-clockwise ≥45°</option>
          </select>
        </label>
        <div className="cg-add-actions">
          <select
            value={gAction}
            onChange={(e) => setGAction(e.target.value as ActionId)}
            aria-label="Custom gesture action"
          >
            {ACTION_IDS.map((a) => (
              <option key={a} value={a}>
                {ACTION_LABEL[a]}
              </option>
            ))}
          </select>
          <button
            type="button"
            className="btn"
            onClick={addGesture}
            disabled={custom.length >= 12}
          >
            Add
          </button>
        </div>
      </div>

      <div className="panel-footer">
        <button
          className="btn"
          onClick={() => onPatch({ bindings, thresholds: t, custom })}
          disabled={!wsConnected}
        >
          Sync to bridge
        </button>
        <button
          className={`btn ghost ${simulateOn ? "on" : ""}`}
          onClick={onSimulate}
          title="Drive the hand locally when the bridge is not running"
        >
          {simulateOn ? "Simulate: ON" : "Simulate: OFF"}
        </button>
      </div>

      {!wsConnected && (
        <p className="warn">
          Bridge offline — start <code>glove_bridge.py</code> then this page will reconnect
          automatically.
        </p>
      )}
    </aside>
  );
}
