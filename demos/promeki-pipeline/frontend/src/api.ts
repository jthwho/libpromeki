// Tiny fetch-based wrapper around the promeki-pipeline REST surface.
// All routes are relative — Vite's dev proxy forwards /api → :8080
// during dev, and the production build is served from the same origin.

import type {
  MediaConfigJson,
  MediaPipelineConfig,
  MetadataJson,
  PipelineDescribe,
  PipelineSettings,
  TypeSchema,
  TypeSummary,
} from './types/api';

async function asJson<T>(r: Response): Promise<T> {
  if (!r.ok) {
    let body = '';
    try {
      body = await r.text();
    } catch {
      body = '';
    }
    throw new Error(`HTTP ${r.status} ${r.statusText}: ${body}`);
  }
  // Some routes return 204 No Content — guard.
  const text = await r.text();
  if (!text) return undefined as unknown as T;
  return JSON.parse(text) as T;
}

export const api = {
  health: () => fetch('/api/health').then((r) => asJson<{ ok: boolean; name: string; version: string; build: string }>(r)),

  // ---- Type registry ----
  listTypes: () => fetch('/api/types').then((r) => asJson<TypeSummary[]>(r)),
  schema: (name: string) =>
    fetch(`/api/types/${encodeURIComponent(name)}/schema`).then((r) => asJson<TypeSchema>(r)),
  defaults: (name: string) =>
    fetch(`/api/types/${encodeURIComponent(name)}/defaults`).then((r) => asJson<MediaConfigJson>(r)),
  metadata: (name: string) =>
    fetch(`/api/types/${encodeURIComponent(name)}/metadata`).then((r) => asJson<MetadataJson>(r)),

  // ---- Pipelines ----
  listPipelines: () => fetch('/api/pipelines').then((r) => asJson<PipelineDescribe[]>(r)),
  createPipeline: (name?: string) =>
    fetch('/api/pipelines', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: name ?? '' }),
    }).then((r) => asJson<{ id: string }>(r)),
  describePipeline: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}`).then((r) => asJson<PipelineDescribe>(r)),
  replaceConfig: (id: string, cfg: MediaPipelineConfig) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg),
    }).then((r) => asJson<PipelineDescribe>(r)),
  removePipeline: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}`, { method: 'DELETE' }).then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
    }),
  getSettings: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/settings`).then((r) => asJson<PipelineSettings>(r)),
  replaceSettings: (id: string, s: PipelineSettings) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/settings`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(s),
    }).then((r) => asJson<PipelineSettings>(r)),

  build: (id: string, autoplan?: boolean) => {
    const q = autoplan === undefined ? '' : `?autoplan=${autoplan ? 1 : 0}`;
    return fetch(`/api/pipelines/${encodeURIComponent(id)}/build${q}`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    );
  },
  open: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/open`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    ),
  start: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/start`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    ),
  // UX-helper macro that drives any reachable state (Empty / Built /
  // Open / Stopped / Closed) all the way to Running.  Implemented by
  // PipelineManager::run on the backend; documented in the demo's
  // apiroutes.cpp and devplan/promeki-pipeline.md.
  run: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/run`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    ),
  stop: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/stop`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    ),
  close: (id: string) =>
    fetch(`/api/pipelines/${encodeURIComponent(id)}/close`, { method: 'POST' }).then((r) =>
      asJson<PipelineDescribe>(r),
    ),

  previewUrl: (id: string, stage: string) =>
    `/api/pipelines/${encodeURIComponent(id)}/preview/${encodeURIComponent(stage)}`,
};
