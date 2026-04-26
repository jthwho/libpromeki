// Pinia store: the demo's set of pipelines plus the per-pipeline log /
// stats buffers driven by the WebSocket event stream. Lifecycle actions
// (build / open / start / stop / close / replaceConfig / replaceSettings)
// always update local state from the describe payload the backend returns
// so the UI stays in sync without an extra round-trip.
import { defineStore } from 'pinia';
import { api } from '../api';
import type {
  MediaPipelineConfig,
  PipelineDescribe,
  PipelineSettings,
  PipelineState,
} from '../types/api';

export interface LogEntry {
  ts: number;
  level: string;
  source?: string;
  line?: number;
  threadName?: string;
  message: string;
  stage?: string;
  // 'stageError' = surfaced with extra prominence; 'log' = ordinary log line.
  kind: 'log' | 'stageError';
}

export interface StageStat {
  stage: string;
  state?: string;
  fpsIn?: number;
  fpsOut?: number;
  framesDropped?: number;
  queueDepth?: number;
  bytesPerSec?: number;
  raw?: Record<string, unknown>;
}

export interface PipelineEntry extends PipelineDescribe {
  log: LogEntry[];
  latestStats?: { ts: number; stages: StageStat[] };
  // True between user edit and the debounced PUT actually landing.
  dirty: boolean;
  // Pending PUT timer handle.
  pendingTimer?: number;
}

interface State {
  byId: Record<string, PipelineEntry>;
  order: string[];
  activeId: string | null;
}

const LOG_RING_LIMIT = 500;

function emptyEntry(d: PipelineDescribe): PipelineEntry {
  return {
    ...d,
    log: [],
    dirty: false,
  };
}

function num(v: unknown): number | undefined {
  if (typeof v === 'number') return v;
  if (typeof v === 'string') {
    const n = Number(v);
    return Number.isFinite(n) ? n : undefined;
  }
  return undefined;
}

function statsFromJson(j: unknown): StageStat[] {
  // The MediaPipelineStats JSON shape (from src/proav/mediapipelinestats.cpp)
  // is roughly:
  //   { stages: [{ name, state, fpsIn, fpsOut, framesDropped, ... }, ...] }
  // We dig defensively so a future field rename does not blank the UI.
  if (!j || typeof j !== 'object') return [];
  const obj = j as Record<string, unknown>;
  const rawStages = obj.stages;
  if (!Array.isArray(rawStages)) return [];
  return rawStages.map((s) => {
    const o = (s ?? {}) as Record<string, unknown>;
    return {
      stage: typeof o.name === 'string' ? o.name : '?',
      state: typeof o.state === 'string' ? o.state : undefined,
      fpsIn: num(o.fpsIn ?? o.framesPerSecondIn),
      fpsOut: num(o.fpsOut ?? o.framesPerSecondOut ?? o.framesPerSecond),
      framesDropped: num(o.framesDropped),
      queueDepth: num(o.queueDepth ?? o.queueSize ?? o.queueLength),
      bytesPerSec: num(o.bytesPerSecond ?? o.bytesPerSec),
      raw: o,
    } satisfies StageStat;
  });
}

export const usePipelinesStore = defineStore('pipelines', {
  state: (): State => ({
    byId: {},
    order: [],
    activeId: null,
  }),
  getters: {
    active: (s): PipelineEntry | undefined =>
      s.activeId ? s.byId[s.activeId] : undefined,
    list: (s): PipelineEntry[] => s.order.map((id) => s.byId[id]).filter(Boolean),
  },
  actions: {
    setActive(id: string | null) {
      this.activeId = id;
    },
    upsert(d: PipelineDescribe) {
      const existing = this.byId[d.id];
      if (existing) {
        existing.name = d.name;
        existing.state = d.state;
        existing.settings = d.settings;
        existing.userConfig = d.userConfig;
        existing.resolvedConfig = d.resolvedConfig;
        existing.dirty = false;
      } else {
        this.byId[d.id] = emptyEntry(d);
        this.order.push(d.id);
      }
    },
    async fetchAll() {
      const all = await api.listPipelines();
      for (const d of all) this.upsert(d);
      // Drop any that vanished.
      const seen = new Set(all.map((d) => d.id));
      this.order = this.order.filter((id) => seen.has(id));
      for (const id of Object.keys(this.byId)) {
        if (!seen.has(id)) delete this.byId[id];
      }
      if (!this.activeId && this.order.length > 0) this.activeId = this.order[0];
      else if (this.activeId && !seen.has(this.activeId)) {
        this.activeId = this.order[0] ?? null;
      }
    },
    async fetchOne(id: string) {
      const d = await api.describePipeline(id);
      this.upsert(d);
    },
    async create(name?: string) {
      const r = await api.createPipeline(name);
      await this.fetchOne(r.id);
      this.activeId = r.id;
      return r.id;
    },
    async remove(id: string) {
      await api.removePipeline(id);
      delete this.byId[id];
      this.order = this.order.filter((x) => x !== id);
      if (this.activeId === id) this.activeId = this.order[0] ?? null;
    },
    /**
     * Locally update userConfig (does NOT immediately PUT — call
     * `commitConfig` to flush, or rely on `scheduleConfigPush` for
     * debounced flushing during canvas edits).
     */
    setUserConfig(id: string, cfg: MediaPipelineConfig) {
      const e = this.byId[id];
      if (!e) return;
      e.userConfig = cfg;
      e.dirty = true;
    },
    scheduleConfigPush(id: string, debounceMs = 300) {
      const e = this.byId[id];
      if (!e) return;
      if (e.pendingTimer) {
        clearTimeout(e.pendingTimer);
      }
      e.pendingTimer = window.setTimeout(() => {
        e.pendingTimer = undefined;
        void this.commitConfig(id);
      }, debounceMs);
    },
    /**
     * Cancels any pending debounced PUT and commits userConfig
     * synchronously-ish (returns the awaitable Promise).  Use this
     * for discrete canvas events (drop, drag-end, delete) where the
     * user will see the consequences immediately and a refresh /
     * tab-close should not lose the edit.
     */
    async flushConfig(id: string) {
      const e = this.byId[id];
      if (!e) return;
      if (e.pendingTimer) {
        clearTimeout(e.pendingTimer);
        e.pendingTimer = undefined;
      }
      await this.commitConfig(id);
    },
    async commitConfig(id: string) {
      const e = this.byId[id];
      if (!e) return;
      try {
        const updated = await api.replaceConfig(id, e.userConfig);
        this.upsert(updated);
      } catch (err) {
        console.error('replaceConfig failed', err);
      }
    },
    async replaceSettings(id: string, s: PipelineSettings) {
      await api.replaceSettings(id, s);
      // The settings-only PUT does not return describe, so refresh.
      await this.fetchOne(id);
    },
    async build(id: string, autoplan?: boolean) {
      // Force any pending edits out before kicking the planner.
      const e = this.byId[id];
      if (e && e.pendingTimer) {
        clearTimeout(e.pendingTimer);
        e.pendingTimer = undefined;
        await this.commitConfig(id);
      }
      const updated = await api.build(id, autoplan);
      this.upsert(updated);
    },
    async open(id: string) {
      this.upsert(await api.open(id));
    },
    async start(id: string) {
      this.upsert(await api.start(id));
    },
    /**
     * UX-helper macro: drives the pipeline to Running through the
     * minimum lifecycle cascade.  See PipelineManager::run on the
     * backend — this lets the [Start] button restart a Stopped
     * pipeline without the user having to learn the close → build →
     * open → start chain.
     */
    async run(id: string) {
      // Force any pending edits out before kicking the cascade so the
      // server-side build sees the latest user config.
      const e = this.byId[id];
      if (e && e.pendingTimer) {
        clearTimeout(e.pendingTimer);
        e.pendingTimer = undefined;
        await this.commitConfig(id);
      }
      this.upsert(await api.run(id));
    },
    async stop(id: string) {
      this.upsert(await api.stop(id));
    },
    async close(id: string) {
      this.upsert(await api.close(id));
    },

    // -------------------- WS event sink --------------------
    onState(id: string, state: PipelineState) {
      const e = this.byId[id];
      if (!e) return;
      e.state = state;
    },
    onStats(id: string, jsonPayload: unknown, ts: number) {
      const e = this.byId[id];
      if (!e) return;
      e.latestStats = { ts, stages: statsFromJson(jsonPayload) };
    },
    appendLog(id: string, entry: LogEntry) {
      const e = this.byId[id];
      if (!e) return;
      e.log.push(entry);
      if (e.log.length > LOG_RING_LIMIT) {
        e.log.splice(0, e.log.length - LOG_RING_LIMIT);
      }
    },
  },
});
