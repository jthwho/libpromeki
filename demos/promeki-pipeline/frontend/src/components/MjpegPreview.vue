<!--
  Inline preview for an MjpegStream sink. Browsers natively render the
  multipart/x-mixed-replace body via <img>, so we just point src at the
  endpoint. When the sink is not yet open we get 503; we re-mount the
  <img> every 500ms until a successful first frame arrives.
-->
<template>
  <div class="preview-block" v-if="ready">
    <img :src="srcUrl" :alt="`Preview: ${stage}`" @error="onError" @load="onLoad" />
  </div>
  <div v-else class="preview-block">
    <div class="preview-waiting">
      Waiting for first frame from <code>{{ stage }}</code>…
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from 'vue';

const props = defineProps<{
  pipelineId: string;
  stage: string;
}>();

const ready = ref(true);
const tick = ref(0);
let retryHandle: number | undefined;

const srcUrl = computed(
  () => `/api/pipelines/${encodeURIComponent(props.pipelineId)}/preview/${encodeURIComponent(
    props.stage,
  )}?t=${tick.value}`,
);

function onLoad() {
  ready.value = true;
  if (retryHandle) {
    clearTimeout(retryHandle);
    retryHandle = undefined;
  }
}

function onError() {
  ready.value = false;
  if (retryHandle) clearTimeout(retryHandle);
  retryHandle = window.setTimeout(() => {
    tick.value += 1;
    ready.value = true;
  }, 500);
}

watch(
  () => [props.pipelineId, props.stage],
  () => {
    tick.value += 1;
    ready.value = true;
  },
);

onUnmounted(() => {
  if (retryHandle) clearTimeout(retryHandle);
});
</script>
