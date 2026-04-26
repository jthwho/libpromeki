<!--
  Left rail. Lists registered MediaIO backends grouped by mode. Each item
  carries a `application/x-promeki-type` payload on drag start; the canvas
  consumes that payload on drop and asks the types store for defaults.
-->
<template>
  <div>
    <div v-if="store.loading" class="palette-empty">Loading types…</div>
    <div v-else-if="store.catalog.length === 0" class="palette-empty">No types registered.</div>
    <template v-else>
      <PaletteSection title="Source" :items="store.sources" />
      <PaletteSection title="Sink" :items="store.sinks" />
      <PaletteSection title="Transform" :items="store.transforms" />
    </template>
  </div>
</template>

<script setup lang="ts">
import { onMounted } from 'vue';
import { useTypesStore } from '../stores/types';
import PaletteSection from './PaletteSection.vue';

const store = useTypesStore();
onMounted(() => void store.loadCatalog());
</script>
