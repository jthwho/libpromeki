<!--
  Smaller, muted node template for stages auto-inserted by the planner.
  Names match `^br\d+_<from>_<to>$`. Non-deletable, non-editable.
-->
<template>
  <div :class="['bridge-chip', { selected }]" title="Auto-inserted by planner">
    <Handle type="target" :position="Position.Left" />
    <div class="type">{{ data.type || 'bridge' }}</div>
    <div class="name">{{ data.name }}</div>
    <Handle type="source" :position="Position.Right" />
  </div>
</template>

<script setup lang="ts">
import { Handle, Position } from '@vue-flow/core';

interface BridgeChipData {
  type?: string;
  name: string;
}

// Selection is still meaningful for bridges — the editor pane shows
// the bridge's read-only config when selected.  Vue Flow passes
// `selected: boolean` as a built-in prop on every custom node.
defineProps<{
  id: string;
  data: BridgeChipData;
  selected?: boolean;
}>();
</script>
