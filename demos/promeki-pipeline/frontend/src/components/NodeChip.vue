<!--
  Vue Flow custom node template for user (non-bridge) stages. Renders the
  type label, an inline-editable stage name, the current state badge, and
  (when stats are flowing) a single-line stats footer.
-->
<template>
  <div :class="['node-chip', modeClass, { selected }]">
    <Handle v-if="hasInput" type="target" :position="Position.Left" />

    <button
      class="delete-x nodrag nopan"
      title="delete this stage"
      @click.stop="onDelete"
      @mousedown.stop
      @pointerdown.stop
    >×</button>

    <div class="row1">
      <span class="type" :title="data.type || ''">{{ typeLabel }}</span>
      <span :class="['badge', `state-${badgeState}`]">{{ badgeState }}</span>
    </div>

    <input
      class="name nodrag nopan"
      :value="data.name"
      @blur="onRename(($event.target as HTMLInputElement).value)"
      @keydown.enter.prevent="($event.target as HTMLInputElement).blur()"
      :placeholder="'name'"
    />

    <div v-if="statsLine" class="stats">
      <span v-if="statsLine.fpsIn !== undefined">in: {{ fmt(statsLine.fpsIn) }} fps</span>
      <span v-if="statsLine.fpsOut !== undefined">out: {{ fmt(statsLine.fpsOut) }} fps</span>
      <span v-if="statsLine.framesDropped !== undefined">drop: {{ statsLine.framesDropped }}</span>
      <span v-if="statsLine.queueDepth !== undefined">q: {{ statsLine.queueDepth }}</span>
    </div>

    <Handle v-if="hasOutput" type="source" :position="Position.Right" />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { Handle, Position } from '@vue-flow/core';
import { usePipelinesStore } from '../stores/pipelines';
import { useTypesStore } from '../stores/types';
import type { StageMode } from '../types/api';

interface NodeChipData {
  type?: string;
  name: string;
  mode?: StageMode;
}

// Vue Flow passes `selected: boolean` as a built-in prop on every
// custom node.  We accept it here so the chip can render a clear
// selected-state visual treatment (border + glow) — see the
// `.node-chip.selected` rules in style.css.
const props = defineProps<{
  id: string;
  data: NodeChipData;
  selected?: boolean;
}>();

const store = usePipelinesStore();
const typesStore = useTypesStore();

// Friendly label sourced from the types catalog.  Falls back to the
// canonical type id while the catalog is still loading or when the
// stage's type is unknown / blank.
const typeLabel = computed(() => {
  const id = props.data.type;
  if (!id) return '—';
  const entry = typesStore.byName(id);
  return entry?.displayName || id;
});

const modeClass = computed(() => {
  switch (props.data.mode) {
    case 'Source': return 'is-source';
    case 'Sink': return 'is-sink';
    case 'Transform': return 'is-xform';
    default: return '';
  }
});

const hasInput  = computed(() => props.data.mode !== 'Source');
const hasOutput = computed(() => props.data.mode !== 'Sink');

const statsLine = computed(() => {
  const stats = store.active?.latestStats;
  if (!stats) return null;
  return stats.stages.find((s) => s.stage === props.data.name) ?? null;
});

const badgeState = computed(() => {
  const s = statsLine.value?.state;
  if (s) return s;
  // Fall back to pipeline-wide state so users see _something_ while stats
  // haven't ticked yet.
  return store.active?.state ?? 'Empty';
});

function fmt(n: number): string {
  if (!Number.isFinite(n)) return '?';
  return n.toFixed(1);
}

function onRename(newName: string) {
  const trimmed = newName.trim();
  if (!trimmed || trimmed === props.data.name) return;
  const active = store.active;
  if (!active) return;
  // Renaming a stage: update userConfig (stage name + any route refs)
  // and let the canvas debounce the PUT.
  const cfg = JSON.parse(JSON.stringify(active.userConfig)) as typeof active.userConfig;
  for (const s of cfg.stages) {
    if (s.name === props.data.name) s.name = trimmed;
  }
  for (const r of cfg.routes) {
    if (r.from === props.data.name) r.from = trimmed;
    if (r.to === props.data.name) r.to = trimmed;
  }
  store.setUserConfig(active.id, cfg);
  store.scheduleConfigPush(active.id);
}

// Click handler for the × badge.  Removes the stage and any routes
// touching it, then schedules the debounced PUT.  Mirrors what Vue
// Flow's Delete / Backspace path triggers via onNodesChange — the
// badge exists because users new to the demo don't always think to
// reach for the keyboard shortcut.
function onDelete() {
  const active = store.active;
  if (!active) return;
  const cur = active.userConfig;
  const stages = (cur.stages ?? []).filter((s) => s.name !== props.data.name);
  const routes = (cur.routes ?? []).filter(
    (r) => r.from !== props.data.name && r.to !== props.data.name,
  );
  store.setUserConfig(active.id, { ...cur, stages, routes });
  store.scheduleConfigPush(active.id);
}
</script>
