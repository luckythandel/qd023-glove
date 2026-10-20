import { useCallback, useEffect, useRef, useState } from "react";
import type { BridgeMsg, Bindings, ConfigPatch, CustomGesture, Thresholds } from "../types";

const WS_URL = "ws://127.0.0.1:8787";

export type GloveState = {
  wsConnected: boolean;
  bleConnected: boolean;
  device: string;
  pct: number[]; // [t,i,m,r,p]
  bent: boolean[];
  pose: string | null;
  twist: number | null;
  lastAction: { action: string; gesture: string; at: number } | null;
  cal: string;
  statusText: string;
  bindings: Bindings | null;
  thresholds: Thresholds | null;
  custom: CustomGesture[] | null;
  logLines: { level: string; msg: string; at: number }[];
};

const EMPTY: GloveState = {
  wsConnected: false,
  bleConnected: false,
  device: "—",
  pct: [0, 0, 0, 0, 0],
  bent: [false, false, false, false, false],
  pose: null,
  twist: null,
  lastAction: null,
  cal: "",
  statusText: "",
  bindings: null,
  thresholds: null,
  custom: null,
  logLines: [],
};

export function useGloveSocket() {
  const [state, setState] = useState<GloveState>(EMPTY);
  const wsRef = useRef<WebSocket | null>(null);
  const retryRef = useRef(0);
  const timerRef = useRef<number | null>(null);

  const connect = useCallback(() => {
    if (timerRef.current) {
      window.clearTimeout(timerRef.current);
      timerRef.current = null;
    }
    // Null the ref BEFORE closing: a superseded socket's onclose must not
    // schedule a competing reconnect (that cycle is what caused the 1 Hz
    // reconnect loop / page shake).
    const prev = wsRef.current;
    wsRef.current = null;
    try {
      prev?.close();
    } catch {
      /* noop */
    }

    const ws = new WebSocket(WS_URL);
    wsRef.current = ws;

    ws.onopen = () => {
      if (wsRef.current !== ws) return;
      retryRef.current = 0;
      setState((s) => ({ ...s, wsConnected: true }));
      ws.send(JSON.stringify({ type: "request_config" }));
    };

    ws.onclose = () => {
      if (wsRef.current !== ws) return;
      setState((s) => ({ ...s, wsConnected: false, bleConnected: false }));
      const delay = Math.min(1000 * 2 ** retryRef.current, 8000);
      retryRef.current += 1;
      timerRef.current = window.setTimeout(connect, delay);
    };

    ws.onerror = () => {
      ws.close();
    };

    ws.onmessage = (ev) => {
      if (wsRef.current !== ws) return;
      let msg: BridgeMsg;
      try {
        msg = JSON.parse(ev.data);
      } catch {
        return;
      }
      setState((s) => {
        switch (msg.type) {
          case "connection":
            return {
              ...s,
              bleConnected: msg.connected,
              device: msg.device,
            };
          case "fingers":
            return { ...s, pct: msg.pct, bent: msg.bent, twist: msg.twist ?? null };
          case "pose":
            return { ...s, pose: msg.name };
          case "action":
            return {
              ...s,
              lastAction: { action: msg.action, gesture: msg.gesture, at: Date.now() },
            };
          case "status":
            return { ...s, cal: msg.cal, statusText: msg.text };
          case "config":
            return { ...s, bindings: msg.bindings, thresholds: msg.thresholds, custom: msg.custom };
          case "log":
            return {
              ...s,
              logLines: [
                { level: msg.level, msg: msg.msg, at: Date.now() },
                ...s.logLines.slice(0, 19),
              ],
            };
          default:
            return s;
        }
      });
    };
  }, []);

  useEffect(() => {
    connect();
    return () => {
      if (timerRef.current) window.clearTimeout(timerRef.current);
      // Detach before close (see connect()): a live ref on a closing socket
      // makes onclose schedule a resurrecting reconnect after unmount.
      const ws = wsRef.current;
      wsRef.current = null;
      try {
        ws?.close();
      } catch {
        /* noop */
      }
    };
  }, [connect]);

  const sendSetConfig = useCallback((patch: ConfigPatch) => {
    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: "set_config", ...patch }));
    }
    setState((s) => ({
      ...s,
      ...(patch.bindings !== undefined ? { bindings: patch.bindings } : {}),
      ...(patch.thresholds !== undefined ? { thresholds: patch.thresholds } : {}),
      ...(patch.custom !== undefined ? { custom: patch.custom } : {}),
    }));
    try {
      if (patch.bindings !== undefined) {
        localStorage.setItem("qd023_bindings", JSON.stringify(patch.bindings));
      }
      if (patch.thresholds !== undefined) {
        localStorage.setItem("qd023_thresholds", JSON.stringify(patch.thresholds));
      }
      if (patch.custom !== undefined) {
        localStorage.setItem("qd023_custom", JSON.stringify(patch.custom));
      }
    } catch {
      /* noop */
    }
  }, []);

  /** Apply a local (simulate) update — only used when WS is down. */
  const injectLocal = useCallback((partial: Partial<GloveState>) => {
    setState((s) => (s.wsConnected ? s : { ...s, ...partial }));
  }, []);

  return { state, sendSetConfig, injectLocal };
}

export function loadLocalBindings(): Bindings | null {
  try {
    const raw = localStorage.getItem("qd023_bindings");
    if (!raw) return null;
    return JSON.parse(raw) as Bindings;
  } catch {
    return null;
  }
}

export function loadLocalThresholds(): Thresholds | null {
  try {
    const raw = localStorage.getItem("qd023_thresholds");
    if (!raw) return null;
    return JSON.parse(raw) as Thresholds;
  } catch {
    return null;
  }
}

export function loadLocalCustom(): CustomGesture[] | null {
  try {
    const raw = localStorage.getItem("qd023_custom");
    if (!raw) return null;
    return JSON.parse(raw) as CustomGesture[];
  } catch {
    return null;
  }
}
