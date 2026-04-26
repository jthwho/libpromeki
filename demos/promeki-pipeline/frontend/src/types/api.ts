// TypeScript shapes for the promeki-pipeline REST + WebSocket API.
// These mirror the JSON envelopes the C++ backend emits — see
// devplan/promeki-pipeline.md (Phase D) for the canonical contract.

export type PipelineState =
  | 'Empty'
  | 'Built'
  | 'Open'
  | 'Running'
  | 'Stopped'
  | 'Closed';

export type StageMode = 'Source' | 'Sink' | 'Transform';

export interface TypeSummary {
  name: string;
  // Human-readable label provided by the backend; the C++ side
  // already falls back to `name` when the FormatDesc didn't set one,
  // so this is always a non-empty string suitable for direct render.
  displayName: string;
  description: string;
  modes: StageMode[];
  extensions: string[];
  schemes: string[];
}

export interface VariantSpec {
  types: string[];
  default?: unknown;
  min?: unknown;
  max?: unknown;
  enum?: { type: string; values: string[] };
  // Suggested-but-not-exclusive preset values (e.g. FrameRate's
  // well-known industry rates).  Different from `enum`: the user
  // may still enter a custom value, the editor surfaces presets as
  // a dropdown plus a "Custom..." option.
  presets?: { label: string; value: string }[];
  description?: string;
}

export type TypeSchema = Record<string, VariantSpec>;

export type MediaConfigJson = Record<string, unknown>;
export type MetadataJson = Record<string, unknown>;

export interface Route {
  from: string;
  to: string;
  fromTrack?: string;
  toTrack?: string;
}

export interface StageConfig {
  name: string;
  type?: string;
  path?: string;
  mode?: StageMode;
  config?: MediaConfigJson;
  metadata?: MetadataJson;
}

export interface MediaPipelineConfig {
  stages: StageConfig[];
  routes: Route[];
  metadata?: MetadataJson;
  frameCount?: number;
}

export type PlannerQuality = 'Highest' | 'Balanced' | 'Fastest' | 'ZeroCopyOnly';

export interface PipelineSettings {
  name: string;
  statsIntervalMs: number;
  quality: PlannerQuality;
  maxBridgeDepth: number;
  excludedBridges: string[];
  autoplan: boolean;
}

export interface PipelineDescribe {
  id: string;
  name: string;
  state: PipelineState;
  settings: PipelineSettings;
  userConfig: MediaPipelineConfig;
  resolvedConfig: MediaPipelineConfig;
}

// --- WebSocket envelope ---------------------------------------------------
export type EventKind =
  | 'StateChanged'
  | 'StageState'
  | 'StageError'
  | 'StatsUpdated'
  | 'PlanResolved'
  | 'Log';

export interface WsEvent {
  pipeline: string;
  kind: EventKind;
  stage?: string;
  ts?: string | number;
  metadata?: Record<string, unknown>;
  payload?: unknown;
  jsonPayload?: unknown;
}

// Minimum bridge-name regex used by GraphCanvas + BridgeChip.
// Matches the planner's `br<N>_<from>_<to>` pattern.
export const BRIDGE_NAME_RE = /^br\d+_.+_.+$/;

export function isBridgeName(name: string | undefined): boolean {
  if (!name) return false;
  return BRIDGE_NAME_RE.test(name);
}
