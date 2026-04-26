<!--
  Top tab strip: one tab per pipeline, plus a `+` to create a new one.
  Tabs show an `*` suffix when the local userConfig has unsaved edits.
-->
<template>
  <div class="tab-strip">
    <div
      v-for="p in store.list"
      :key="p.id"
      :class="['tab', store.activeId === p.id ? 'active' : '']"
      @click="store.setActive(p.id)"
      :title="`id: ${p.id}\nstate: ${p.state}`"
    >
      <span>{{ p.name || p.id }}<span v-if="p.dirty">*</span></span>
      <button class="close-x" @click.stop="onClose(p.id)" title="delete pipeline">×</button>
    </div>

    <button @click="onCreate" title="new pipeline">+</button>

    <div class="toolbar">
      <span class="pipe-state" v-if="store.active">
        state: {{ store.active.state }}
      </span>
      <button :disabled="!canBuild" @click="onAction('build')">Build</button>
      <button :disabled="!canOpen" @click="onAction('open')">Open</button>
      <button
        class="primary"
        :disabled="!canRun"
        @click="onAction('run')"
        title="drive the pipeline to Running (cascades through close/build/open as needed)"
      >Run</button>
      <button :disabled="!canStop" @click="onAction('stop')">Stop</button>
      <button :disabled="!canClose" @click="onAction('close')">Close</button>
      <button :disabled="!store.active" @click="$emit('open-settings')" title="settings">⚙</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { usePipelinesStore } from '../stores/pipelines';

const store = usePipelinesStore();

defineEmits<{
  (e: 'open-settings'): void;
}>();

async function onCreate() {
  try {
    const newName = `pipeline ${store.list.length + 1}`;
    await store.create(newName);
  } catch (err) {
    console.error('create failed', err);
  }
}

async function onClose(id: string) {
  if (!window.confirm('Delete this pipeline?')) return;
  try {
    await store.remove(id);
  } catch (err) {
    console.error('remove failed', err);
  }
}

const state = computed(() => store.active?.state ?? 'Empty');

const canBuild = computed(() => !!store.active && (state.value === 'Empty' || state.value === 'Closed'));
const canOpen  = computed(() => state.value === 'Built');
// Run cascades from any non-Running state to Running, including
// Stopped (which the library only exits via close → build).  Disabled
// only while the pipeline is already Running.
const canRun   = computed(() => !!store.active && state.value !== 'Running');
const canStop  = computed(() => state.value === 'Running');
const canClose = computed(() =>
  state.value !== 'Empty' && state.value !== 'Closed' && !!store.active,
);

async function onAction(kind: 'build' | 'open' | 'run' | 'stop' | 'close') {
  if (!store.active) return;
  const id = store.active.id;
  try {
    switch (kind) {
      case 'build': await store.build(id); break;
      case 'open':  await store.open(id);  break;
      case 'run':   await store.run(id);   break;
      case 'stop':  await store.stop(id);  break;
      case 'close': await store.close(id); break;
    }
  } catch (err) {
    console.error(kind, 'failed', err);
    alert(`${kind} failed: ${(err as Error).message}`);
  }
}
</script>
