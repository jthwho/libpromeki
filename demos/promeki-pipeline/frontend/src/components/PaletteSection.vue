<template>
  <div class="palette-section">
    <div class="palette-section-title">{{ title }}</div>
    <div v-if="items.length === 0" class="palette-empty">none</div>
    <div
      v-for="t in items"
      :key="t.name"
      class="palette-item"
      draggable="true"
      @dragstart="onDragStart($event, t)"
      :title="`${t.name}${t.description ? ' — ' + t.description : ''}`"
    >
      <div>{{ t.displayName || t.name }}</div>
      <div v-if="t.description" class="desc">{{ t.description }}</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { TypeSummary } from '../types/api';

const props = defineProps<{
  title: string;
  items: TypeSummary[];
}>();

function onDragStart(ev: DragEvent, t: TypeSummary) {
  if (!ev.dataTransfer) return;
  // Pick the section's title as the dropped node's mode so the drop
  // handler does not have to guess from `.modes`.
  const mode = props.title;
  const payload = JSON.stringify({ type: t.name, mode });
  ev.dataTransfer.setData('application/x-promeki-type', payload);
  ev.dataTransfer.setData('text/plain', payload);
  ev.dataTransfer.effectAllowed = 'copy';
}
</script>
