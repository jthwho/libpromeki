<template>
  <div class="app-root">
    <TabStrip @open-settings="settingsOpen = true" />

    <!--
      The top section is laid out as a CSS grid whose column tracks
      come straight from the splitter widths so the user can drag the
      palette / editor edges without us touching the pane DOM.  The
      6-pixel splitter columns are rendered as absolute siblings of the
      grid cells so they overlay the pane edges and don't steal grid
      space from the canvas.
    -->
    <div
      class="three-pane"
      :style="{
        gridTemplateColumns: `${paletteSplit.size.value}px 1fr ${editorSplit.size.value}px`,
      }"
    >
      <div class="pane-wrap palette-pane-wrap">
        <div class="palette-pane">
          <PaletteView />
        </div>
        <div
          class="splitter splitter-v"
          :class="{ dragging: paletteSplit.dragging.value }"
          @mousedown="paletteSplit.onMouseDown"
          title="Drag to resize"
        ></div>
      </div>
      <div class="canvas-pane">
        <GraphCanvas @select="selectedStage = $event" />
      </div>
      <div class="pane-wrap editor-pane-wrap">
        <div
          class="splitter splitter-v splitter-left"
          :class="{ dragging: editorSplit.dragging.value }"
          @mousedown="editorSplit.onMouseDown"
          title="Drag to resize"
        ></div>
        <div class="editor-pane">
          <ConfigEditor :stage-name="selectedStage" />
        </div>
      </div>
    </div>

    <div
      class="bottom-panel-wrap"
      :style="{ height: `${bottomSplit.size.value}px` }"
    >
      <div
        class="splitter splitter-h"
        :class="{ dragging: bottomSplit.dragging.value }"
        @mousedown="bottomSplit.onMouseDown"
        title="Drag to resize"
      ></div>
      <LogStatsPanel />
    </div>

    <PipelineSettingsModal
      v-if="settingsOpen && pipelinesStore.active"
      :pipeline-id="pipelinesStore.active.id"
      @close="settingsOpen = false"
    />
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';
import TabStrip from './components/TabStrip.vue';
import PaletteView from './components/PaletteView.vue';
import GraphCanvas from './components/GraphCanvas.vue';
import ConfigEditor from './components/ConfigEditor.vue';
import LogStatsPanel from './components/LogStatsPanel.vue';
import PipelineSettingsModal from './components/PipelineSettingsModal.vue';

import { usePipelinesStore } from './stores/pipelines';
import { useTypesStore } from './stores/types';
import { useEventsStore } from './stores/events';
import { useDragSplit } from './composables/useDragSplit';

const pipelinesStore = usePipelinesStore();
const typesStore = useTypesStore();
const eventsStore = useEventsStore();

const selectedStage = ref<string | null>(null);
const settingsOpen = ref(false);

// Side-pane caps: never let either pane exceed half the viewport so
// the canvas always retains at least 320 px of breathing room (also
// enforced as a min on the canvas via CSS).
function halfViewportWidth(): number {
  return Math.max(320, Math.floor(window.innerWidth / 2));
}

// Bottom-panel cap: 60 % of viewport height; min via CSS so the
// header bar always fits.
function maxBottomHeight(): number {
  return Math.max(80, Math.floor(window.innerHeight * 0.6));
}

const paletteSplit = useDragSplit({
  initial: 220,
  storageKey: 'pp.palette.width',
  orientation: 'horizontal',
  edge: 'start',
  min: 160,
  max: halfViewportWidth,
});

const editorSplit = useDragSplit({
  initial: 320,
  storageKey: 'pp.editor.width',
  orientation: 'horizontal',
  edge: 'end',
  min: 240,
  max: halfViewportWidth,
});

const bottomSplit = useDragSplit({
  initial: 240,
  storageKey: 'pp.bottom.height',
  orientation: 'vertical',
  edge: 'end',
  min: 80,
  max: maxBottomHeight,
});

onMounted(() => {
  void typesStore.loadCatalog();
  void pipelinesStore.fetchAll();
  eventsStore.connect();
});

// Reset selection when active pipeline changes.
watch(
  () => pipelinesStore.activeId,
  () => {
    selectedStage.value = null;
    settingsOpen.value = false;
  },
);
</script>
