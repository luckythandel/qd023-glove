export type GestureId =
  | "FIST"
  | "INDEX_UP"
  | "PEACE"
  | "THUMBS_UP"
  | "THUMB_BENT"
  | "MIDDLE_UP"
  | "PINKY_UP"
  | "OPEN_PALM"
  | "MID_RING"
  | "RING_ONLY";

export type ActionId =
  | "play_pause"
  | "next"
  | "prev"
  | "volume_up"
  | "volume_down"
  | "mute"
  | "stop"
  | "mouse_left"
  | "mouse_right"
  | "mouse_up"
  | "mouse_down"
  | "mouse_click"
  | "mouse_rclick"
  | "brightness_up"
  | "brightness_down"
  | "none";

export type FingerKey = "thumb" | "index" | "middle" | "ring" | "pinky";

export type PoseCell = "B" | "E" | "X";

export type FingerThreshold = { on: number; off: number };

export type Thresholds = Record<FingerKey, FingerThreshold>;

export type RotDir = "none" | "cw" | "ccw";

export type CustomGesture = {
  id: string;
  name: string;
  pattern: PoseCell[];
  action: ActionId;
  rot?: RotDir;
};

export const FINGER_KEYS: FingerKey[] = ["thumb", "index", "middle", "ring", "pinky"];

export const FINGER_LABEL: Record<FingerKey, string> = {
  thumb: "Thumb",
  index: "Index",
  middle: "Middle",
  ring: "Ring",
  pinky: "Pinky",
};

export const DEFAULT_THRESHOLDS: Thresholds = {
  thumb: { on: 56, off: 30 },
  index: { on: 46, off: 18 },
  middle: { on: 46, off: 18 },
  ring: { on: 46, off: 18 },
  pinky: { on: 46, off: 18 },
};

export const GESTURE_IDS: GestureId[] = [
  "FIST",
  "INDEX_UP",
  "PEACE",
  "THUMBS_UP",
  "THUMB_BENT",
  "MIDDLE_UP",
  "PINKY_UP",
  "OPEN_PALM",
  "MID_RING",
  "RING_ONLY",
];

export const ACTION_IDS: ActionId[] = [
  "play_pause",
  "next",
  "prev",
  "volume_up",
  "volume_down",
  "mute",
  "stop",
  "mouse_left",
  "mouse_right",
  "mouse_up",
  "mouse_down",
  "mouse_click",
  "mouse_rclick",
  "brightness_up",
  "brightness_down",
  "none",
];

export const ACTION_LABEL: Record<ActionId, string> = {
  play_pause: "Play / Pause",
  next: "Next Track",
  prev: "Previous Track",
  volume_up: "Volume Up",
  volume_down: "Volume Down",
  mute: "Mute",
  stop: "Stop",
  mouse_left: "Mouse Left",
  mouse_right: "Mouse Right",
  mouse_up: "Mouse Up",
  mouse_down: "Mouse Down",
  mouse_click: "Left Click",
  mouse_rclick: "Right Click",
  brightness_up: "Brightness Up",
  brightness_down: "Brightness Down",
  none: "— none —",
};

export const GESTURE_LABEL: Record<GestureId, string> = {
  FIST: "Fist",
  INDEX_UP: "Index Up",
  PEACE: "Peace",
  THUMBS_UP: "Thumbs Up",
  THUMB_BENT: "Thumb Bent",
  MIDDLE_UP: "Middle Up",
  PINKY_UP: "Pinky Up",
  OPEN_PALM: "Open Palm",
  MID_RING: "Mid + Ring (twist for volume)",
  RING_ONLY: "Ring Only (sweep hand for brightness)",
};

export const DEFAULT_BINDINGS: Record<GestureId, ActionId> = {
  FIST: "play_pause",
  INDEX_UP: "next",
  PEACE: "prev",
  THUMBS_UP: "none",
  THUMB_BENT: "none",
  MIDDLE_UP: "volume_down",
  PINKY_UP: "mute",
  OPEN_PALM: "none",
  MID_RING: "none",
  RING_ONLY: "none",
};

// ---- protocol messages (bridge -> web) ----
export type ConnectionMsg = {
  type: "connection";
  connected: boolean;
  device: string;
};
export type FingersMsg = {
  type: "fingers";
  pct: number[]; // [t,i,m,r,p]
  bent: boolean[];
  twist?: number | null;
};
export type PoseMsg = { type: "pose"; name: string | null };
export type ActionMsg = { type: "action"; action: ActionId; gesture: string };
export type StatusMsg = { type: "status"; cal: string; text: string };
export type ConfigMsg = {
  type: "config";
  bindings: Record<GestureId, ActionId>;
  thresholds: Thresholds;
  custom: CustomGesture[];
};
export type LogMsg = { type: "log"; level: "info" | "warn" | "error"; msg: string };
export type PongMsg = { type: "pong" };

export type BridgeMsg =
  | ConnectionMsg
  | FingersMsg
  | PoseMsg
  | ActionMsg
  | StatusMsg
  | ConfigMsg
  | LogMsg
  | PongMsg;

export type Bindings = Record<GestureId, ActionId>;

export type ConfigPatch = {
  bindings?: Bindings;
  thresholds?: Thresholds;
  custom?: CustomGesture[];
};
