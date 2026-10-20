import { useEffect, useRef, useState } from "react";

const FINGER_COLORS = ["#ffb02e", "#4fd1c5", "#63b3ff", "#b794f6", "#f687b3"];
const NAMES = ["T", "I", "M", "R", "P"];

/**
 * SVG hand. Five fingers as segmented strips that curl from pct (0–100).
 * Layout: palm block at bottom, fingers fan upward. Thumb angles out left.
 */

type Props = {
  pct: number[];
  bent: boolean[];
  pose: string | null;
  actionFlash: string | null;
  twist?: number | null;
};

const FINGER_DEFS = [
  // thumb: base at palm left, shorter, angled
  { x: 92, y: 300, length: 78, baseAngle: -40, segments: 3, widths: [22, 19, 16] },
  { x: 148, y: 160, length: 120, baseAngle: -8, segments: 4, widths: [20, 17, 14, 11] },
  { x: 188, y: 148, length: 132, baseAngle: -2, segments: 4, widths: [21, 18, 15, 12] },
  { x: 228, y: 156, length: 120, baseAngle: 4, segments: 4, widths: [20, 17, 14, 11] },
  { x: 266, y: 174, length: 100, baseAngle: 12, segments: 4, widths: [18, 15, 12, 10] },
];

function useSmoothPct(target: number[]): number[] {
  const [shown, setShown] = useState<number[]>([0, 0, 0, 0, 0]);
  const cur = useRef<number[]>([0, 0, 0, 0, 0]);
  const targetRef = useRef<number[]>(target);
  targetRef.current = target;

  useEffect(() => {
    let raf = 0;
    const tick = () => {
      const t = targetRef.current;
      const next = cur.current.slice();
      let changed = false;
      for (let i = 0; i < 5; i++) {
        const want = t[i] ?? 0;
        const c = cur.current[i];
        const v = c + (want - c) * 0.35;
        const settled = Math.abs(v - want) < 0.4 ? want : v;
        if (settled !== next[i]) changed = true;
        next[i] = settled;
      }
      cur.current = next;
      if (changed) setShown(next);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  useEffect(() => {
    const allZero = target.every((v) => v === 0);
    const shownAllZero = shown.every((v) => v === 0);
    if (allZero !== shownAllZero) {
      cur.current = target.slice();
      setShown(target.slice());
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [target]);

  return shown;
}

function Finger({
  def,
  pct,
  isBent,
  color,
  name,
}: {
  def: (typeof FINGER_DEFS)[number];
  pct: number;
  isBent: boolean;
  color: string;
  name: string;
}) {
  const bend = Math.max(0, Math.min(100, pct)) / 100;
  const segLen = def.length / def.segments;
  const pts: { x: number; y: number; a: number }[] = [];
  let x = def.x;
  let y = def.y;
  let angle = def.baseAngle;
  pts.push({ x, y, a: angle });
  for (let s = 0; s < def.segments; s++) {
    const curl = bend * (18 + s * 10);
    angle = def.baseAngle + curl;
    const rad = ((angle - 90) * Math.PI) / 180;
    x += Math.cos(rad) * segLen;
    y += Math.sin(rad) * segLen;
    pts.push({ x, y, a: angle });
  }

  const tip = pts[pts.length - 1];

  return (
    <g>
      {pts.slice(0, -1).map((p, i) => {
        const nxt = pts[i + 1];
        const w = def.widths[i] ?? 10;
        return (
          <g key={i}>
            <line
              x1={p.x}
              y1={p.y}
              x2={nxt.x}
              y2={nxt.y}
              stroke={color}
              strokeWidth={w}
              strokeLinecap="round"
            />
            <line
              x1={p.x}
              y1={p.y}
              x2={nxt.x}
              y2={nxt.y}
              stroke="#ffffff"
              strokeWidth={w * 0.3}
              strokeLinecap="round"
              opacity={0.14}
            />
          </g>
        );
      })}
      <circle cx={tip.x} cy={tip.y} r={3} fill={color} />
      {isBent && (
        <circle
          cx={tip.x}
          cy={tip.y}
          r={11}
          fill="none"
          stroke={color}
          strokeWidth={1.5}
          opacity={0.35}
        />
      )}
      <text
        x={tip.x}
        y={tip.y - 10}
        fill={isBent ? color : "#4a5468"}
        fontSize="10"
        fontFamily="ui-monospace, monospace"
        textAnchor="middle"
        fontWeight={isBent ? 700 : 500}
        opacity={isBent ? 1 : 0.8}
      >
        {name} {Math.round(pct)}
      </text>
    </g>
  );
}

export default function Hand({ pct, bent, pose, actionFlash, twist }: Props) {
  const smooth = useSmoothPct(pct);
  const tilt = Math.max(-35, Math.min(35, (twist ?? 0) * 0.18));

  return (
    <div className="hand-wrap">
      <svg viewBox="0 0 360 400" className="hand-svg" role="img" aria-label="Glove hand">
        <defs>
          <radialGradient id="palmGrad" cx="50%" cy="40%" r="60%">
            <stop offset="0%" stopColor="#1e2739" />
            <stop offset="100%" stopColor="#121826" />
          </radialGradient>
          <filter id="glow" x="-30%" y="-30%" width="160%" height="160%">
            <feGaussianBlur stdDeviation="2" result="b" />
            <feMerge>
              <feMergeNode in="b" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
        </defs>

        <g
          style={{
            transform: `rotate(${tilt}deg)`,
            transformOrigin: "175px 380px",
            transformBox: "view-box",
            transition: "transform 0.15s linear",
          }}
        >
          <rect
            x={96}
            y={150}
            width={158}
            height={224}
            rx={38}
            fill="url(#palmGrad)"
            stroke="#2a3548"
            strokeWidth={2}
          />
          <path
            d="M 118 186 Q 175 168 232 186"
            fill="none"
            stroke="#ffffff"
            strokeWidth={2}
            opacity={0.05}
          />
          <text
            x={175}
            y={330}
            fill="#3d4a61"
            fontSize="11"
            fontFamily="ui-monospace, monospace"
            textAnchor="middle"
          >
            QD023
          </text>
          <rect
            x={132}
            y={356}
            width={86}
            height={26}
            rx={13}
            fill="#0d1220"
            stroke="#253043"
            strokeWidth={1.5}
          />

          <g filter="url(#glow)">
            {FINGER_DEFS.map((d, i) => (
              <Finger
                key={i}
                def={d}
                pct={smooth[i] ?? 0}
                isBent={bent[i] ?? false}
                color={FINGER_COLORS[i]}
                name={NAMES[i]}
              />
            ))}
          </g>
        </g>
      </svg>

      <div className="hand-meta">
        <div className="pose-chip">
          <span className="pose-label">POSE</span>
          <span className="pose-name">{pose ?? "—"}</span>
        </div>
        {actionFlash && (
          <div key={actionFlash} className="action-flash">
            {actionFlash.replace(/_/g, " ").toUpperCase()}
          </div>
        )}
      </div>
    </div>
  );
}
