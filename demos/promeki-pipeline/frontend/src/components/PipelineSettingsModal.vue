<!--
  Per-pipeline settings modal triggered by the gear icon. PUTs to
  /api/pipelines/{id}/settings on save.
-->
<template>
  <div class="modal-backdrop" @click.self="$emit('close')">
    <div class="modal-card">
      <h2>Pipeline settings</h2>

      <div class="modal-row">
        <label>Name</label>
        <input v-model="form.name" type="text" />
      </div>

      <div class="modal-row">
        <label>Stats interval (ms)</label>
        <input
          v-model.number="form.statsIntervalMs"
          type="number"
          min="0"
          step="100"
        />
      </div>

      <div class="modal-row">
        <label>Autoplan</label>
        <input v-model="form.autoplan" type="checkbox" />
      </div>

      <div class="modal-row">
        <label>Quality</label>
        <select v-model="form.quality">
          <option value="Highest">Highest</option>
          <option value="Balanced">Balanced</option>
          <option value="Fastest">Fastest</option>
          <option value="ZeroCopyOnly">ZeroCopyOnly</option>
        </select>
      </div>

      <div class="modal-row">
        <label>Max bridge depth</label>
        <input v-model.number="form.maxBridgeDepth" type="number" min="1" max="10" />
      </div>

      <div class="modal-row">
        <label>Excluded bridges</label>
        <div class="multi">
          <label v-for="t in typeNames" :key="t">
            <input
              type="checkbox"
              :checked="form.excludedBridges.includes(t)"
              @change="toggleExcluded(t, ($event.target as HTMLInputElement).checked)"
            />
            <span>{{ t }}</span>
          </label>
        </div>
      </div>

      <div class="modal-actions">
        <button @click="$emit('close')">Cancel</button>
        <button class="primary" @click="onSave">Save</button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, reactive, watch } from 'vue';
import { usePipelinesStore } from '../stores/pipelines';
import { useTypesStore } from '../stores/types';
import type { PipelineSettings, PlannerQuality } from '../types/api';

const props = defineProps<{
  pipelineId: string;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const store = usePipelinesStore();
const typesStore = useTypesStore();

const typeNames = computed(() => typesStore.typeNames);

const initial = (): PipelineSettings => {
  const e = store.byId[props.pipelineId];
  if (e?.settings) return JSON.parse(JSON.stringify(e.settings));
  return {
    name: '',
    statsIntervalMs: 1000,
    quality: 'Highest' as PlannerQuality,
    maxBridgeDepth: 4,
    excludedBridges: [],
    autoplan: true,
  };
};

const form = reactive<PipelineSettings>(initial());

watch(
  () => props.pipelineId,
  () => Object.assign(form, initial()),
);

function toggleExcluded(name: string, on: boolean) {
  const set = new Set(form.excludedBridges);
  if (on) set.add(name);
  else set.delete(name);
  form.excludedBridges = [...set];
}

async function onSave() {
  try {
    await store.replaceSettings(props.pipelineId, JSON.parse(JSON.stringify(form)));
    emit('close');
  } catch (err) {
    console.error('settings save failed', err);
    alert('Failed to save settings: ' + (err as Error).message);
  }
}
</script>
