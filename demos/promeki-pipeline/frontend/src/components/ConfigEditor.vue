<!--
  Right rail. When a user (non-bridge) stage is selected we fetch the
  schema for its `type`, render one SpecField per key, and auto-save
  every edit through a 300 ms debounce — same UX as the canvas drag
  PUTs in GraphCanvas.vue.  When a bridge is selected we show its
  config read-only.
-->
<template>
  <div v-if="!stage" class="editor-empty">
    Select a node to edit its configuration.
  </div>
  <div v-else>
    <div class="editor-title">{{ stage.name }}</div>
    <div class="editor-subtitle">
      <span v-if="stage.type">{{ stage.type }}</span>
      <span v-else>(unknown type)</span>
      <span v-if="readOnly"> — auto-inserted, read-only</span>
    </div>

    <MjpegPreview
      v-if="canPreview"
      :pipeline-id="active!.id"
      :stage="stage.name"
    />

    <div v-if="loading" class="editor-empty">Loading schema…</div>
    <template v-else>
      <SpecField
        v-for="(spec, key) in schema"
        :key="key"
        :name="String(key)"
        :spec="spec"
        :model-value="localCfg[String(key)]"
        @update:model-value="onChange(String(key), $event)"
      />
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { usePipelinesStore } from '../stores/pipelines';
import { useTypesStore } from '../stores/types';
import {
  isBridgeName,
  type MediaPipelineConfig,
  type StageConfig,
  type TypeSchema,
} from '../types/api';
import SpecField from './SpecField.vue';
import MjpegPreview from './MjpegPreview.vue';

const props = defineProps<{
  stageName: string | null;
}>();

const store = usePipelinesStore();
const typesStore = useTypesStore();

const active = computed(() => store.active);

const stage = computed<StageConfig | null>(() => {
  if (!active.value || !props.stageName) return null;
  // Prefer resolvedConfig (so bridges are visible); fall back to user.
  const inResolved = active.value.resolvedConfig?.stages?.find(
    (s) => s.name === props.stageName,
  );
  if (inResolved) return inResolved;
  return (
    active.value.userConfig?.stages?.find((s) => s.name === props.stageName) ?? null
  );
});

const readOnly = computed(() => isBridgeName(stage.value?.name));

const schema = ref<TypeSchema>({});
const loading = ref(false);

watch(
  () => stage.value?.type,
  async (t) => {
    if (!t) {
      schema.value = {};
      return;
    }
    loading.value = true;
    try {
      schema.value = await typesStore.schema(t);
    } catch (err) {
      console.warn('schema load failed for', t, err);
      schema.value = {};
    } finally {
      loading.value = false;
    }
  },
  { immediate: true },
);

// Per-stage local edit buffer.  Lets number inputs (and any field with
// a typing rhythm) feel responsive without round-tripping the backend
// on every keystroke; the debounced PUT in `onChange` covers the
// commit.  We rebuild this buffer ONLY when the selected stage's name
// changes — using a deep `watch(() => stage.value, ...)` would also
// fire when the user's own PUT round-trips back through `upsert` and
// updates the stage object, nuking pending edits.
const localCfg = ref<Record<string, unknown>>({});

watch(
  () => stage.value?.name,
  () => {
    const cfg = (stage.value?.config ?? {}) as Record<string, unknown>;
    localCfg.value = { ...cfg };
  },
  { immediate: true },
);

// Push every edit straight into userConfig and let the store's
// debounce coalesce the PUTs.  This mirrors GraphCanvas.vue's drag-end
// behaviour so the two surfaces have one model: edit -> auto-save.
function onChange(key: string, value: unknown) {
  if (readOnly.value) return;
  if (!active.value || !stage.value) return;

  // Update the local buffer first so the SpecField re-renders with the
  // new value immediately (the v-model effectively flows through us).
  localCfg.value = { ...localCfg.value, [key]: value };

  const stageName = stage.value.name;
  const cur = active.value.userConfig;
  const stages = (cur.stages ?? []).map((s) => {
    if (s.name !== stageName) return s;
    return { ...s, config: { ...localCfg.value } };
  });
  const next: MediaPipelineConfig = {
    ...cur,
    stages,
    routes: cur.routes ?? [],
  };
  store.setUserConfig(active.value.id, next);
  store.scheduleConfigPush(active.value.id);
}

const canPreview = computed(() => {
  if (!active.value || !stage.value) return false;
  if (stage.value.type !== 'MjpegStream') return false;
  return active.value.state === 'Running';
});
</script>
