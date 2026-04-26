// Pinia store: owns the single WebSocket connection to /api/events.
// Reconnects with exponential backoff on disconnect. Inbound envelopes
// fan out into the pipelines store as state / stats / log updates.
import { defineStore } from 'pinia';
import type { WsEvent } from '../types/api';
import { usePipelinesStore } from './pipelines';

interface State {
  ws: WebSocket | null;
  url: string;
  connected: boolean;
  attempt: number;
  lastError?: string;
  reconnectTimer?: number;
}

function tsToMillis(ts: unknown): number {
  if (typeof ts === 'number') return ts > 1e12 ? ts : ts * 1000;
  if (typeof ts === 'string') {
    const n = Number(ts);
    if (Number.isFinite(n)) return n > 1e12 ? n : n * 1000;
    const t = Date.parse(ts);
    if (Number.isFinite(t)) return t;
  }
  return Date.now();
}

export const useEventsStore = defineStore('events', {
  state: (): State => {
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return {
      ws: null,
      url: `${proto}//${window.location.host}/api/events`,
      connected: false,
      attempt: 0,
    };
  },
  actions: {
    connect() {
      if (this.ws) return;
      this._open();
    },
    _open() {
      try {
        const ws = new WebSocket(this.url);
        this.ws = ws;
        ws.onopen = () => {
          this.connected = true;
          this.attempt = 0;
          this.lastError = undefined;
        };
        ws.onmessage = (m) => this._dispatch(m.data);
        ws.onerror = () => {
          this.lastError = 'WebSocket error';
        };
        ws.onclose = () => {
          this.connected = false;
          this.ws = null;
          this._scheduleReconnect();
        };
      } catch (err) {
        this.lastError = String(err);
        this._scheduleReconnect();
      }
    },
    _scheduleReconnect() {
      if (this.reconnectTimer) return;
      this.attempt += 1;
      const delay = Math.min(15000, 500 * Math.pow(1.7, this.attempt));
      this.reconnectTimer = window.setTimeout(() => {
        this.reconnectTimer = undefined;
        this._open();
      }, delay);
    },
    _dispatch(raw: unknown) {
      let env: WsEvent | undefined;
      try {
        env = typeof raw === 'string' ? (JSON.parse(raw) as WsEvent) : undefined;
      } catch {
        env = undefined;
      }
      if (!env || typeof env !== 'object' || !env.pipeline) return;

      const pipelines = usePipelinesStore();
      const id = env.pipeline;
      const ts = tsToMillis(env.ts);

      switch (env.kind) {
        case 'StateChanged': {
          const state = typeof env.payload === 'string' ? env.payload : '';
          if (state) pipelines.onState(id, state as never);
          // Always re-fetch describe so resolvedConfig / settings stay synced.
          void pipelines.fetchOne(id).catch(() => {});
          break;
        }
        case 'StageState': {
          // Append a low-noise log line so users see lifecycle in the panel.
          const transition = typeof env.payload === 'string' ? env.payload : '?';
          pipelines.appendLog(id, {
            ts,
            level: 'Info',
            message: `Stage '${env.stage ?? '?'}' ${transition}`,
            stage: env.stage,
            kind: 'log',
          });
          break;
        }
        case 'StageError': {
          const msg = typeof env.payload === 'string' ? env.payload : 'stage error';
          pipelines.appendLog(id, {
            ts,
            level: 'Err',
            message: msg,
            stage: env.stage,
            kind: 'stageError',
          });
          break;
        }
        case 'StatsUpdated': {
          // Backend emits jsonPayload OR payload depending on serialization.
          const j = (env as { jsonPayload?: unknown }).jsonPayload ?? env.payload;
          pipelines.onStats(id, j, ts);
          break;
        }
        case 'PlanResolved': {
          // Pull a fresh describe so resolvedConfig with bridges shows up.
          void pipelines.fetchOne(id).catch(() => {});
          break;
        }
        case 'Log': {
          const meta = (env.metadata ?? {}) as Record<string, unknown>;
          const msg = typeof env.payload === 'string' ? env.payload : '';
          pipelines.appendLog(id, {
            ts,
            level: typeof meta.level === 'string' ? (meta.level as string) : 'Info',
            source: typeof meta.source === 'string' ? (meta.source as string) : undefined,
            line: typeof meta.line === 'number' ? (meta.line as number) : undefined,
            threadName:
              typeof meta.threadName === 'string' ? (meta.threadName as string) : undefined,
            message: msg,
            stage: env.stage,
            kind: 'log',
          });
          break;
        }
      }
    },
  },
});
