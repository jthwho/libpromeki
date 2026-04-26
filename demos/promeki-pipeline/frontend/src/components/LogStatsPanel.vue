<!--
  Bottom collapsible panel: tabbed log + stats view for the active pipeline.
-->
<template>
  <div :class="['bottom-panel', collapsed ? 'collapsed' : '']">
    <div class="bottom-panel-header">
      <span
        :class="['tab-pill', tab === 'log' ? 'active' : '']"
        @click="tab = 'log'; collapsed = false"
      >Log</span>
      <span
        :class="['tab-pill', tab === 'stats' ? 'active' : '']"
        @click="tab = 'stats'; collapsed = false"
      >Stats</span>

      <input
        v-if="!collapsed && tab === 'log'"
        v-model="filter"
        placeholder="filter"
        style="width: 140px;"
      />
      <label v-if="!collapsed && tab === 'log'" style="text-transform:none; letter-spacing:normal;">
        <input type="checkbox" v-model="autoScroll" /> auto-scroll
      </label>

      <span class="filler"></span>

      <button @click="collapsed = !collapsed">
        {{ collapsed ? 'Show' : 'Hide' }}
      </button>
    </div>

    <div v-if="!collapsed" class="bottom-panel-body" ref="bodyEl">
      <template v-if="tab === 'log'">
        <div v-if="filteredLog.length === 0" class="palette-empty">No log lines yet.</div>
        <div
          v-for="(l, idx) in filteredLog"
          :key="idx"
          :class="['log-line', `level-${l.level}`]"
        >
          <span style="color: var(--fg-faint);">{{ formatTs(l.ts) }}</span>
          [<span>{{ l.level }}</span>]
          <span v-if="l.stage">[{{ l.stage }}]</span>
          {{ l.message }}
        </div>
      </template>

      <template v-else-if="tab === 'stats'">
        <div v-if="!latest" class="palette-empty">No stats yet — start the pipeline.</div>
        <table v-else class="stats-table">
          <thead>
            <tr>
              <th>Stage</th>
              <th>State</th>
              <th>fps in</th>
              <th>fps out</th>
              <th>dropped</th>
              <th>queue</th>
              <th>bytes/s</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="s in latest.stages" :key="s.stage">
              <td>{{ s.stage }}</td>
              <td>{{ s.state ?? '—' }}</td>
              <td>{{ fmt(s.fpsIn) }}</td>
              <td>{{ fmt(s.fpsOut) }}</td>
              <td>{{ s.framesDropped ?? '—' }}</td>
              <td>{{ s.queueDepth ?? '—' }}</td>
              <td>{{ fmt(s.bytesPerSec) }}</td>
            </tr>
            <tr v-if="aggregate" class="aggregate">
              <td>total</td>
              <td>—</td>
              <td>{{ fmt(aggregate.fpsIn) }}</td>
              <td>{{ fmt(aggregate.fpsOut) }}</td>
              <td>{{ aggregate.framesDropped }}</td>
              <td>—</td>
              <td>{{ fmt(aggregate.bytesPerSec) }}</td>
            </tr>
          </tbody>
        </table>
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import { usePipelinesStore } from '../stores/pipelines';

const store = usePipelinesStore();
const tab = ref<'log' | 'stats'>('log');
const collapsed = ref(false);
const filter = ref('');
const autoScroll = ref(true);
const bodyEl = ref<HTMLElement | null>(null);

const active = computed(() => store.active);
const log = computed(() => active.value?.log ?? []);
const latest = computed(() => active.value?.latestStats);

const filteredLog = computed(() => {
  if (!filter.value) return log.value;
  const f = filter.value.toLowerCase();
  return log.value.filter((l) =>
    l.message.toLowerCase().includes(f)
    || (l.stage ?? '').toLowerCase().includes(f)
    || (l.level ?? '').toLowerCase().includes(f),
  );
});

const aggregate = computed(() => {
  if (!latest.value || latest.value.stages.length === 0) return null;
  let fpsIn = 0;
  let fpsOut = 0;
  let dropped = 0;
  let bytes = 0;
  for (const s of latest.value.stages) {
    fpsIn += s.fpsIn ?? 0;
    fpsOut += s.fpsOut ?? 0;
    dropped += s.framesDropped ?? 0;
    bytes += s.bytesPerSec ?? 0;
  }
  return { fpsIn, fpsOut, framesDropped: dropped, bytesPerSec: bytes };
});

function fmt(n?: number): string {
  if (n === undefined || !Number.isFinite(n)) return '—';
  if (Math.abs(n) >= 1_000_000) return (n / 1e6).toFixed(2) + 'M';
  if (Math.abs(n) >= 1_000) return (n / 1e3).toFixed(2) + 'k';
  return n.toFixed(2);
}

function formatTs(ts: number): string {
  const d = new Date(ts);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  const ss = String(d.getSeconds()).padStart(2, '0');
  return `${hh}:${mm}:${ss}`;
}

watch(filteredLog, () => {
  if (!autoScroll.value) return;
  nextTick(() => {
    const el = bodyEl.value;
    if (el) el.scrollTop = el.scrollHeight;
  });
});
</script>
