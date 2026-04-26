<!--
  Vue Flow wrapper. Renders the active pipeline's graph.

  Display: when `resolvedConfig` has stages we draw _that_ (so bridges
  appear); otherwise we draw `userConfig`.  Editing only ever mutates
  `userConfig` — bridges are read-only and the planner regenerates them
  each `Build`.
-->
<template>
  <div
    ref="wrapperRef"
    class="canvas-wrapper"
    @dragenter.prevent
    @dragover.prevent
    @drop.prevent.stop="onDrop"
  >
    <div v-if="!active" class="palette-empty" style="padding: 12px;">
      No pipeline selected. Click <strong>+</strong> to create one.
    </div>
    <VueFlow
      v-else
      v-model:nodes="nodes"
      v-model:edges="edges"
      :node-types="nodeTypes"
      :delete-key-code="['Delete', 'Backspace']"
      fit-view-on-init
      @node-click="onNodeClick"
      @connect="onConnect"
      @nodes-change="onNodesChange"
      @edges-change="onEdgesChange"
    >
      <Background pattern-color="#2a2f37" :gap="20" />
      <Controls />
    </VueFlow>
  </div>
</template>

<script setup lang="ts">
import { computed, markRaw, nextTick, ref, watch } from 'vue';
import {
  VueFlow,
  Position,
  useVueFlow,
  type Connection,
  type Edge,
  type EdgeChange,
  type Node,
  type NodeChange,
} from '@vue-flow/core';
import { Background } from '@vue-flow/background';
import { Controls } from '@vue-flow/controls';
import '@vue-flow/core/dist/style.css';
import '@vue-flow/core/dist/theme-default.css';
import '@vue-flow/controls/dist/style.css';

import { usePipelinesStore } from '../stores/pipelines';
import { useTypesStore } from '../stores/types';
import {
  isBridgeName,
  type MediaPipelineConfig,
  type Route,
  type StageConfig,
  type StageMode,
} from '../types/api';
import NodeChip from './NodeChip.vue';
import BridgeChip from './BridgeChip.vue';

const emit = defineEmits<{
  (e: 'select', stageName: string | null): void;
}>();

const store = usePipelinesStore();
const typesStore = useTypesStore();

const active = computed(() => store.active);

// Vue Flow's viewport helpers.  `screenToFlowCoordinate` is the v1.40+
// API that converts a viewport-relative {x,y} into the pan/zoom-aware
// flow-space coordinate.  We need it for the drop handler (so a node
// lands exactly where the cursor is regardless of pan/zoom).
const { screenToFlowCoordinate } = useVueFlow();

// Vue Flow custom node templates. Cast to `any` to side-step the
// Vue Flow generic NodeProps signature — our chips render the data
// they need without going through the typed prop hierarchy.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const nodeTypes: any = {
  user: markRaw(NodeChip),
  bridge: markRaw(BridgeChip),
};

const wrapperRef = ref<HTMLDivElement | null>(null);
const nodes = ref<Node[]>([]);
const edges = ref<Edge[]>([]);

// Frontend.X / Frontend.Y wire keys (declared on the C++ Metadata
// class — see include/promeki/metadata.h).  The dot is the namespace
// separator; we use the wire form everywhere on the frontend so the
// raw JSON the backend round-trips matches verbatim.
const FRONTEND_X = 'Frontend.X';
const FRONTEND_Y = 'Frontend.Y';

/**
 * Per-pipeline position cache.  Used for two narrow purposes:
 *
 *   - Bridges: bridges have no Metadata so the cache is their only
 *     source of truth.  Computed once from the neighbors' resolved
 *     positions and reused on every render until the bridge is gone.
 *   - User stages mid-drag: Vue Flow's onNodesChange only fires the
 *     final position (`dragging=false`); we cache that here BEFORE
 *     the `setUserConfig` -> debounced PUT -> describe round-trip
 *     completes so a re-render in that window doesn't snap the node
 *     back to the metadata's stale value.  As soon as the round-trip
 *     finishes (`upsert` replaces userConfig), step 2 takes over and
 *     the metadata wins.
 *
 * Resolution order for any given stage's position (see `resolvePosition`):
 *   1. Bridge with cached position → use it.
 *   2. User stage with persisted Frontend.X / Frontend.Y metadata in
 *      userConfig → use that, ALWAYS (overrides any stale cache so a
 *      page reload picks up the saved coords even after a session-only
 *      auto-layout fallback was cached on a prior render).
 *   3. Bridge: midpoint between its from/to neighbors' resolved
 *      positions, or the deterministic auto-layout when a neighbor
 *      isn't placed yet.  Cached.
 *   4. User stage cache hit (post-drag, pre-PUT-roundtrip) → use it.
 *   5. Else (a user stage with no saved coords — fresh drop or race
 *      window) → deterministic auto-layout.  Caller is told to PUT
 *      these coords back into userConfig so they survive a refresh.
 *
 * The cache is keyed by pipeline id; switching pipelines clears it
 * for the new id only.  Removing a stage drops its entry but leaves
 * all other entries untouched.
 */
const nodePositions = ref<Map<string, { x: number; y: number }>>(new Map());
// Pipeline id whose positions are currently in `nodePositions`.  The
// active-id watcher resets the cache whenever this changes.
let cachedPipelineId: string | null = null;

// Layout: simple horizontal stack on a fresh canvas. We position once
// based on stage index so the user sees something sensible without
// having to drag everything around.  This is the deterministic fallback
// for stages whose Frontend.X / Frontend.Y metadata keys are not set
// AND that the bridge midpoint heuristic could not place.
function layoutFor(idx: number): { x: number; y: number } {
  return { x: 60 + idx * 220, y: 80 + (idx % 2) * 30 };
}

// Coerce a metadata value (which round-trips through Variant and may
// land as a number or a numeric String depending on serializer) into a
// finite number, or undefined when the key is missing / unparseable.
function asNumber(v: unknown): number | undefined {
  if (typeof v === 'number') return Number.isFinite(v) ? v : undefined;
  if (typeof v === 'string') {
    const n = Number(v);
    return Number.isFinite(n) ? n : undefined;
  }
  return undefined;
}

// Read persisted (Frontend.X, Frontend.Y) from a user stage's
// metadata, or undefined when not set.
function persistedPositionFor(name: string): { x: number; y: number } | undefined {
  const cfg = active.value?.userConfig;
  if (!cfg) return undefined;
  for (const s of cfg.stages) {
    if (s.name !== name) continue;
    const meta = s.metadata ?? {};
    const x = asNumber(meta[FRONTEND_X]);
    const y = asNumber(meta[FRONTEND_Y]);
    if (x !== undefined && y !== undefined) return { x, y };
    return undefined;
  }
  return undefined;
}

// Parse a planner bridge name `br<N>_<from>_<to>` into its from/to
// neighbor names.  Returns null when the name doesn't match.  We split
// on the first and last underscore so neighbor names containing
// underscores still work.
function parseBridgeName(name: string): { from: string; to: string } | null {
  const m = /^br\d+_(.+)_([^_]+)$/.exec(name);
  if (!m) return null;
  return { from: m[1], to: m[2] };
}

// Resolve a stage's runtime position via the documented priority
// chain.  Populates `nodePositions` with the result for steps 3 / 5
// so subsequent calls short-circuit; for user stages with metadata
// (step 2) the cache is updated as well so future renders get the
// metadata value via step 4 if the metadata ever disappears.  When
// `outNeedsPersist` is supplied and we hit step 5 (user stage with
// no saved coords yet), we record the stage name so the caller can
// push the auto-laid-out position back into userConfig.
function resolvePosition(
  name: string,
  isBridge: boolean,
  fallbackIdx: number,
  outNeedsPersist?: Set<string>,
): { x: number; y: number } {
  // 1. Bridges always read the cache first.  They have no metadata,
  // so once placed they should stay put until the bridge is gone.
  if (isBridge) {
    const cached = nodePositions.value.get(name);
    if (cached) return cached;
  }

  // 2. User stage with persisted metadata — metadata is the source
  // of truth and always overrides the cache.  Without this, an
  // earlier auto-layout fallback (step 5) cached BEFORE userConfig
  // populated would lock the node into the wrong position forever
  // even after the metadata loaded.
  if (!isBridge) {
    const persisted = persistedPositionFor(name);
    if (persisted) {
      nodePositions.value.set(name, persisted);
      return persisted;
    }
  }

  // 3. Bridge midpoint between resolved from/to neighbors.  Bridges
  // hit step 1 first; this branch only runs when the bridge is new.
  if (isBridge) {
    const parsed = parseBridgeName(name);
    if (parsed) {
      const a = nodePositions.value.get(parsed.from);
      const b = nodePositions.value.get(parsed.to);
      if (a && b) {
        const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
        nodePositions.value.set(name, mid);
        return mid;
      }
    }
  }

  // 4. User-stage cache hit (post-drag, pre-PUT-roundtrip window).
  // This catches the brief moment where Vue Flow has reported the
  // drag-end position but the metadata hasn't been set yet because
  // setStagePosition runs in the same event tick the cache update
  // does.  Once the round-trip completes, step 2 takes over.
  if (!isBridge) {
    const cached = nodePositions.value.get(name);
    if (cached) return cached;
  }

  // 5. Last resort: deterministic auto-layout.  For unseeded user
  // stages also flag for persistence so the position survives a
  // refresh.  We deliberately do NOT cache this for user stages —
  // caching here is what allowed a transient race to lock in a
  // wrong auto-layout coord even after the real metadata loaded.
  // For bridges the cache is necessary (no metadata), so we set
  // it; bridges have a stable identity anyway.
  const fallback = layoutFor(fallbackIdx);
  if (isBridge) nodePositions.value.set(name, fallback);
  if (!isBridge && outNeedsPersist) outNeedsPersist.add(name);
  return fallback;
}

/**
 * What the canvas actually renders.
 *
 * Always shows every userConfig stage so newly-dropped nodes appear
 * immediately, even when an older `resolvedConfig` is sitting around
 * from a previous Build.  Bridges from `resolvedConfig` are overlaid
 * on top — but only the ones whose `from` / `to` neighbors still exist
 * in userConfig (so dangling bridges from a previous pipeline shape
 * don't linger after the user removes one of their endpoints).
 *
 * The previous behaviour of "if resolvedConfig has stages, return it
 * verbatim" hid newly-added userConfig stages whenever a build had
 * ever run, because the planner only refreshes resolvedConfig on an
 * explicit Build click.
 */
function pickConfig(): MediaPipelineConfig {
  if (!active.value) return { stages: [], routes: [] };
  const userCfg = active.value.userConfig;
  const userStages = userCfg.stages ?? [];
  const userRoutes = userCfg.routes ?? [];

  const r = active.value.resolvedConfig;
  if (!r || !r.stages || r.stages.length === 0) {
    return { ...userCfg, stages: userStages, routes: userRoutes };
  }

  const userNames = new Set(userStages.map((s) => s.name));
  const bridges = (r.stages ?? []).filter((s) => {
    if (!isBridgeName(s.name)) return false;
    const parsed = parseBridgeName(s.name);
    if (!parsed) return false;
    return userNames.has(parsed.from) && userNames.has(parsed.to);
  });
  const bridgeNames = new Set(bridges.map((s) => s.name));
  const bridgeRoutes = (r.routes ?? []).filter(
    (rt) => bridgeNames.has(rt.from) || bridgeNames.has(rt.to),
  );

  return {
    ...userCfg,
    stages: [...userStages, ...bridges],
    routes: [...userRoutes, ...bridgeRoutes],
  };
}

// Build a fresh Node entry for a stage that doesn't already exist in the
// canvas.  Position is resolved through the documented priority chain.
function makeNode(
  s: StageConfig,
  fallbackIdx: number,
  needsPersist: Set<string>,
): Node {
  const isBridge = isBridgeName(s.name);
  const pos = resolvePosition(s.name, isBridge, fallbackIdx, needsPersist);
  return {
    id: s.name,
    type: isBridge ? 'bridge' : 'user',
    position: pos,
    data: {
      type: s.type ?? '',
      name: s.name,
      mode: s.mode,
    },
    sourcePosition: Position.Right,
    targetPosition: Position.Left,
    draggable: !isBridge,
    deletable: !isBridge,
    selectable: true,
  };
}

/**
 * Reconcile `nodes` / `edges` against the current MediaPipelineConfig
 * by REUSING existing entries by reference whenever a stage / route
 * still exists.  Only newly-added entries get fresh objects.
 *
 * Why this matters: Vue Flow only "moves" a node when its position
 * field changes between renders.  If we hand it the same object back,
 * it does nothing — no re-mount, no visual jump, no position reset.
 * The previous implementation replaced the entire array with brand-new
 * objects on every rebuild, which made Vue Flow re-render every node
 * and (for reasons buried in its internal layout pass) sometimes shift
 * positions even when our cached coordinates were unchanged.  Reusing
 * by-ref makes "user did not touch it → does not move" a hard
 * invariant.
 */
function syncFromConfig() {
  const cfg = pickConfig();
  const needsPersist = new Set<string>();

  // Two-pass position resolution so bridges can read already-placed
  // user stages from the cache when computing their midpoint.
  cfg.stages.forEach((s, idx) => {
    if (isBridgeName(s.name)) return;
    resolvePosition(s.name, false, idx, needsPersist);
  });
  cfg.stages.forEach((s, idx) => {
    if (!isBridgeName(s.name)) return;
    resolvePosition(s.name, true, idx, needsPersist);
  });

  // ----- Nodes -----
  // Build a map of already-rendered nodes by id; reuse them by ref so
  // Vue Flow's reactivity treats unchanged nodes as truly unchanged.
  // Cast through `any` because Vue Flow's `Node` generic is recursive
  // enough to choke vue-tsc — we only need the id and the object itself.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const currentNodes = nodes.value as any[];
  const existingNodes = new Map<string, Node>();
  for (const n of currentNodes) existingNodes.set(n.id, n as Node);

  const wantedStageNames = new Set(cfg.stages.map((s) => s.name));
  const newNodes: Node[] = [];
  cfg.stages.forEach((s, idx) => {
    const existing = existingNodes.get(s.name);
    if (existing) {
      newNodes.push(existing);
    } else {
      newNodes.push(makeNode(s, idx, needsPersist));
    }
  });

  // Only swap the array reference if the membership actually changed —
  // otherwise reactivity churns for no reason.
  let nodesChanged = newNodes.length !== currentNodes.length;
  if (!nodesChanged) {
    for (let i = 0; i < newNodes.length; i++) {
      if (newNodes[i] !== currentNodes[i]) { nodesChanged = true; break; }
    }
  }
  if (nodesChanged) nodes.value = newNodes;

  // ----- Edges -----
  // Edge id is `e_<idx>_<from>_<to>` in our scheme; identity by
  // (from,to) pair is what actually matters semantically.  Index can
  // shift when intermediate routes are removed, so we key existing
  // edges by from/to rather than by composed id.  Same vue-tsc
  // recursive-type workaround as for nodes.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const currentEdges = edges.value as any[];
  const existingEdges = new Map<string, Edge>();
  for (const e of currentEdges) {
    existingEdges.set(`${e.source}__${e.target}`, e as Edge);
  }

  const newEdges: Edge[] = [];
  cfg.routes.forEach((r, idx) => {
    const key = `${r.from}__${r.to}`;
    const existing = existingEdges.get(key);
    if (existing) {
      newEdges.push(existing);
    } else {
      newEdges.push({
        id: `e_${idx}_${r.from}_${r.to}`,
        source: r.from,
        target: r.to,
      });
    }
  });

  let edgesChanged = newEdges.length !== currentEdges.length;
  if (!edgesChanged) {
    for (let i = 0; i < newEdges.length; i++) {
      if (newEdges[i] !== currentEdges[i]) { edgesChanged = true; break; }
    }
  }
  if (edgesChanged) edges.value = newEdges;

  // Drop cache entries for stages that vanished (planner-rebuild may
  // change bridge names; keeping their stale entries forever is harmless
  // but wastes memory across long sessions).
  for (const cachedName of Array.from(nodePositions.value.keys())) {
    if (!wantedStageNames.has(cachedName)) {
      nodePositions.value.delete(cachedName);
    }
  }

  // Step-4 stages need their auto-laid-out coords pushed back into
  // userConfig so they survive a page reload.
  if (needsPersist.size > 0 && active.value) {
    void nextTick(() => persistAutoPositions(needsPersist));
  }
}

// Push the cached auto-layout positions for the named user stages
// back into userConfig.metadata so the next describe round-trip keeps
// them.  Only invoked from rebuild() for stages that hit step 4.
function persistAutoPositions(names: Set<string>) {
  const a = active.value;
  if (!a) return;
  let mutated = false;
  let cfg = a.userConfig;
  for (const name of names) {
    const pos = nodePositions.value.get(name);
    if (!pos) continue;
    // Skip if userConfig now already has the keys (raced with a
    // previous PUT or another call).
    const existing = persistedPositionFor(name);
    if (existing) continue;
    const stages = cfg.stages.map((s) => {
      if (s.name !== name) return s;
      const meta = { ...(s.metadata ?? {}) };
      meta[FRONTEND_X] = pos.x;
      meta[FRONTEND_Y] = pos.y;
      return { ...s, metadata: meta };
    });
    cfg = { ...cfg, stages, routes: cfg.routes ?? [] };
    mutated = true;
  }
  if (mutated) {
    store.setUserConfig(a.id, cfg);
    store.scheduleConfigPush(a.id);
  }
}

// Mutate userConfig.stages[name].metadata in place, replacing the
// stage record so Vue / Pinia notice the change.  Returns the new
// MediaPipelineConfig.
function setStagePosition(
  cfg: MediaPipelineConfig,
  name: string,
  pos: { x: number; y: number },
): MediaPipelineConfig {
  const stages = cfg.stages.map((s) => {
    if (s.name !== name) return s;
    const meta = { ...(s.metadata ?? {}) };
    meta[FRONTEND_X] = pos.x;
    meta[FRONTEND_Y] = pos.y;
    return { ...s, metadata: meta };
  });
  return { ...cfg, stages, routes: cfg.routes ?? [] };
}

watch(
  () => active.value?.id,
  (id) => {
    // Cache is per-pipeline.  When the user switches to a different
    // tab we drop the old cache and let the new pipeline reseed from
    // metadata / auto-layout.  We also drop the rendered nodes/edges
    // so the by-ref preservation in syncFromConfig starts clean for
    // the new pipeline.
    if (id !== cachedPipelineId) {
      nodePositions.value = new Map();
      cachedPipelineId = id ?? null;
      nodes.value = [];
      edges.value = [];
    }
    syncFromConfig();
  },
  { immediate: true },
);

// Watch only the SHAPE of the config — stage names + route from/to
// pairs, plus any change to resolvedConfig (which the planner rebuilds
// wholesale).  A deep watch on userConfig would fire on every metadata
// edit and every position-during-drag mutation, churning Vue Flow's
// internal layout for no reason.  Stage / route shape is what actually
// changes the graph membership.
watch(
  () => {
    const a = active.value;
    if (!a) return null;
    const stageNames = (a.userConfig.stages ?? []).map((s) => s.name).join('|');
    const routes = (a.userConfig.routes ?? [])
      .map((r) => `${r.from}>${r.to}`)
      .join('|');
    // resolvedConfig identity changes whenever the planner ran.  Use
    // a similar fingerprint so adding/removing bridges triggers sync.
    const r = a.resolvedConfig;
    const resolvedNames =
      r && r.stages ? r.stages.map((s) => s.name).join('|') : '';
    const resolvedRoutes =
      r && r.routes ? r.routes.map((rt) => `${rt.from}>${rt.to}`).join('|') : '';
    return `${stageNames}#${routes}#${resolvedNames}#${resolvedRoutes}`;
  },
  () => syncFromConfig(),
);

// --- Drop handler: turn a palette payload into a new node ---
async function onDrop(ev: DragEvent) {
  if (!ev.dataTransfer) return;
  const raw = ev.dataTransfer.getData('application/x-promeki-type')
    || ev.dataTransfer.getData('text/plain');
  if (!raw) return;
  let payload: { type: string; mode?: string };
  try {
    payload = JSON.parse(raw);
  } catch {
    return;
  }
  const type = payload.type;
  const mode = (payload.mode || 'Transform') as StageMode;
  if (!type || !active.value) return;

  // Default config is what `MediaIO::create` would have used; the demo
  // exposes it explicitly via /api/types/{name}/defaults so the server
  // doesn't have to merge silently.
  let defaults: Record<string, unknown> = {};
  try {
    defaults = await typesStore.defaultConfig(type);
  } catch (err) {
    console.warn('failed to load defaults for', type, err);
  }
  // Strip Type from the per-stage config — it lives at the stage level
  // (the library's StageConfig duplicates it but our PUT round-trip is
  // cleaner without it).
  const cleanCfg: Record<string, unknown> = { ...defaults };
  delete cleanCfg.Type;

  const newName = nextNameFor(mode);
  // Convert the drop event's viewport coords into Vue Flow's flow-
  // space coordinate system so the node lands exactly at the cursor
  // regardless of the canvas's pan/zoom.  The wrapper-rect math we
  // used to do here was wrong as soon as the user panned or zoomed.
  const flowPos = screenToFlowCoordinate({ x: ev.clientX, y: ev.clientY });
  // Center the chip on the cursor (chips are ~160x60 — see
  // .node-chip).  Half-dimension offset gives a "drop where the
  // pointer is" feel.
  const pos = { x: flowPos.x - 80, y: flowPos.y - 30 };

  const newStage: StageConfig = {
    name: newName,
    type,
    mode,
    config: cleanCfg,
    // Persist the dropped position in the stage's metadata so it
    // survives a refresh — see Frontend.X / Frontend.Y on Metadata.
    metadata: {
      [FRONTEND_X]: pos.x,
      [FRONTEND_Y]: pos.y,
    },
  };
  const cur = active.value.userConfig;
  const next: MediaPipelineConfig = {
    ...cur,
    stages: [...(cur.stages ?? []), newStage],
    routes: cur.routes ?? [],
  };
  // Seed the runtime cache directly so the upcoming rebuild sees a
  // cache hit and doesn't re-derive (or, in flight, mis-derive) the
  // position before the userConfig watcher fires.
  nodePositions.value.set(newName, pos);
  store.setUserConfig(active.value.id, next);
  // Discrete event — commit immediately so a refresh right after the
  // drop never loses the new stage / its persisted position.
  void store.flushConfig(active.value.id);
  await nextTick();
  emit('select', newName);
}

// Generate the next available stage name for a given mode.  Names
// follow the pattern `<mode>N` where the prefix is the lowercased
// mode (`source`, `sink`, `transform`) and N is the smallest
// positive integer not already taken in this pipeline.  The user
// can rename via NodeChip's inline rename UI afterwards.
function nextNameFor(mode: StageMode): string {
  const prefix = (mode || 'Transform').toLowerCase();
  const stages = active.value?.userConfig.stages ?? [];
  const taken = new Set(stages.map((s) => s.name));
  let i = 1;
  while (taken.has(`${prefix}${i}`)) i++;
  return `${prefix}${i}`;
}

// --- Vue Flow change handlers ---
function onNodeClick(payload: { node: Node }) {
  emit('select', payload.node.id);
}

function onConnect(c: Connection) {
  if (!active.value || !c.source || !c.target) return;
  const cur = active.value.userConfig;
  const dup = (cur.routes ?? []).some(
    (r) => r.from === c.source && r.to === c.target,
  );
  if (dup) return;
  const newRoute: Route = { from: c.source as string, to: c.target as string };
  store.setUserConfig(active.value.id, {
    ...cur,
    stages: cur.stages ?? [],
    routes: [...(cur.routes ?? []), newRoute],
  });
  void store.flushConfig(active.value.id);
}

function onNodesChange(changes: NodeChange[]) {
  if (!active.value) return;
  let mutated = false;
  let next = active.value.userConfig;
  for (const c of changes) {
    if (c.type === 'remove') {
      const id = c.id;
      // Drop the cache entry first so the entry never outlives the
      // stage.  Bridges are non-deletable on the chip itself; if a
      // bridge does surface a remove change we drop its cache too.
      nodePositions.value.delete(id);
      if (isBridgeName(id)) continue;
      next = {
        ...next,
        stages: (next.stages ?? []).filter((s) => s.name !== id),
        routes: (next.routes ?? []).filter(
          (r) => r.from !== id && r.to !== id,
        ),
      };
      mutated = true;
    } else if (c.type === 'position' && c.position && !c.dragging) {
      // Persist positions only when the drag has _ended_
      // (dragging=false).  Update the runtime cache for both bridges
      // and user stages so a session-level bridge drag sticks until
      // the next planner run; only user stages get a userConfig PUT.
      nodePositions.value.set(c.id, { x: c.position.x, y: c.position.y });
      if (isBridgeName(c.id)) continue;
      next = setStagePosition(next, c.id, {
        x: c.position.x,
        y: c.position.y,
      });
      mutated = true;
    }
  }
  if (mutated) {
    store.setUserConfig(active.value.id, next);
    // Drag-end and node-delete are discrete events — commit immediately
    // so a quick refresh after the drag doesn't drop the new position.
    void store.flushConfig(active.value.id);
  }
}

function onEdgesChange(changes: EdgeChange[]) {
  if (!active.value) return;
  let mutated = false;
  const cur = active.value.userConfig;
  let routes = [...(cur.routes ?? [])];
  // Snapshot the current edge map by id so the change list (which only
  // gives us ids) can be resolved to source/target strings without
  // poking back through Vue Flow's deep generic Edge types.
  const edgeMap: Record<string, { source: string; target: string }> = {};
  for (const e of edges.value) {
    edgeMap[e.id] = { source: e.source, target: e.target };
  }
  for (const c of changes) {
    if (c.type !== 'remove') continue;
    const found = edgeMap[c.id];
    if (!found) continue;
    if (isBridgeName(found.source) || isBridgeName(found.target)) continue;
    routes = routes.filter((r) => !(r.from === found.source && r.to === found.target));
    mutated = true;
  }
  if (mutated) {
    store.setUserConfig(active.value.id, { ...cur, stages: cur.stages ?? [], routes });
    void store.flushConfig(active.value.id);
  }
}
</script>

<style scoped>
/*
 * Cover the entire .canvas-pane so HTML5 dragover / drop can land
 * anywhere the user might aim — including the strip above / below
 * Vue Flow's transformer pane.  position:absolute + inset:0 keeps us
 * pinned even when the inner Vue Flow viewport sizes itself smaller
 * than the surrounding grid cell.
 */
.canvas-wrapper {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  overflow: hidden;
}

/*
 * Vue Flow renders inside our wrapper; force it to fill the wrapper so
 * the visible canvas matches the drop zone exactly.  Without this, the
 * Vue Flow viewport can be shorter than the wrapper which makes drops
 * near the edges land on the wrapper but feel "outside" the canvas.
 */
.canvas-wrapper :deep(.vue-flow) {
  width: 100%;
  height: 100%;
}
</style>
