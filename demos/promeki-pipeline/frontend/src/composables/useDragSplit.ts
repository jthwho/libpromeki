// useDragSplit.ts
//
// Generic mouse-drag splitter logic shared by the three resizable
// panes in App.vue.  A composable rather than a directive keeps the
// state (current size + persistence key) straightforward to read in
// the parent template via grid-template-{columns,rows} bindings.
//
// orientation:
//   'horizontal' — drag changes a width; the splitter is a vertical
//                  bar; cursor is col-resize.
//   'vertical'   — drag changes a height; the splitter is a horizontal
//                  bar; cursor is row-resize.
//
// edge:
//   'start' — splitter sits at the END of the target pane (e.g. the
//             palette has its handle on its right edge).  Dragging
//             right grows the pane.
//   'end'   — splitter sits at the START of the target pane (e.g. the
//             editor / bottom panel has its handle on its left/top).
//             Dragging right (or down) shrinks the pane.

import { onBeforeUnmount, ref, watch } from 'vue';

export type Orientation = 'horizontal' | 'vertical';
export type Edge = 'start' | 'end';

export interface DragSplitOptions {
  /** Initial size in pixels when no stored value exists. */
  initial: number;
  /** localStorage key for cross-reload persistence. */
  storageKey: string;
  /** Drag axis. */
  orientation: Orientation;
  /** Which edge of the target pane the handle sits on. */
  edge: Edge;
  /** Minimum allowed size in pixels. */
  min: number;
  /** Maximum allowed size in pixels. */
  max: () => number;
}

export interface DragSplit {
  /** Reactive current size in pixels. */
  size: { value: number };
  /** True while a drag is in progress. */
  dragging: { value: boolean };
  /**
   * mousedown handler to attach to the splitter element.  Bind in the
   * template as `@mousedown="splitter.onMouseDown"`.
   */
  onMouseDown: (ev: MouseEvent) => void;
}

function loadStored(key: string, fallback: number): number {
  try {
    const raw = localStorage.getItem(key);
    if (raw === null) return fallback;
    const n = Number(raw);
    return Number.isFinite(n) ? n : fallback;
  } catch {
    return fallback;
  }
}

function saveStored(key: string, value: number) {
  try {
    localStorage.setItem(key, String(Math.round(value)));
  } catch {
    /* private mode etc. — non-fatal */
  }
}

export function useDragSplit(opts: DragSplitOptions): DragSplit {
  const size = ref<number>(loadStored(opts.storageKey, opts.initial));
  const dragging = ref<boolean>(false);

  // Persist on any size change after init.  We debounce trivially via
  // the watcher firing exactly once per assignment.
  watch(size, (n) => saveStored(opts.storageKey, n));

  // Drag-time scratch.
  let startCoord = 0;
  let startSize = 0;
  let savedBodyCursor = '';
  let savedBodyUserSelect = '';

  function clamp(n: number): number {
    return Math.min(Math.max(n, opts.min), opts.max());
  }

  function onMouseMove(ev: MouseEvent) {
    if (!dragging.value) return;
    const cur = opts.orientation === 'horizontal' ? ev.clientX : ev.clientY;
    const delta = cur - startCoord;
    // 'start' edge grows when pointer moves outward (right/down).
    // 'end' edge grows when pointer moves the OTHER way.
    const sign = opts.edge === 'start' ? 1 : -1;
    size.value = clamp(startSize + sign * delta);
  }

  function endDrag() {
    if (!dragging.value) return;
    dragging.value = false;
    document.removeEventListener('mousemove', onMouseMove);
    document.removeEventListener('mouseup', endDrag);
    document.body.style.cursor = savedBodyCursor;
    document.body.style.userSelect = savedBodyUserSelect;
  }

  function onMouseDown(ev: MouseEvent) {
    // Left button only.
    if (ev.button !== 0) return;
    ev.preventDefault();
    startCoord = opts.orientation === 'horizontal' ? ev.clientX : ev.clientY;
    startSize = size.value;
    dragging.value = true;
    savedBodyCursor = document.body.style.cursor;
    savedBodyUserSelect = document.body.style.userSelect;
    document.body.style.cursor =
      opts.orientation === 'horizontal' ? 'col-resize' : 'row-resize';
    // Suppress the inevitable text-selection that mousemove drags
    // would otherwise produce while we drag.
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', endDrag);
  }

  // Defensive cleanup if the component is torn down mid-drag.
  onBeforeUnmount(() => {
    if (dragging.value) endDrag();
  });

  return {
    size,
    dragging,
    onMouseDown,
  };
}
