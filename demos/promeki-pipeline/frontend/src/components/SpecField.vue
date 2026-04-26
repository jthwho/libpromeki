<!--
  Schema-driven single-key editor.

  Switches on the spec's first listed Variant type.  Real backend
  schemas observed today cover bool, int, float, String, Rational,
  FrameRate, Size2D, Enum, EnumList, Color, SocketAddress, the
  TypeRegistry-backed dropdowns (PixelFormat, PixelMemLayout,
  VideoCodec — these arrive carrying an `enum` block synthesized
  by the demo backend), and the HDR composite types
  ContentLightLevel and MasteringDisplay.

  FrameRate gets a dedicated `frameRate` editor when the backend
  attaches a `presets` array (which it does for all FrameRate
  specs sourced from `FrameRate::wellKnownRates()`): a dropdown
  of well-known rates plus a "Custom..." option that reveals the
  underlying `n/d` rational editor.  Bare FrameRate specs without
  presets fall back to the rational editor.

  Anything else falls through to a raw-JSON textarea and emits a
  console.warn so the omission is visible during development.  All
  observed Variant types in registered backends should hit a typed
  branch first.
-->
<template>
  <div class="spec-field">
    <label>
      <span>{{ name }}</span>
      <span v-if="usingRawJson" class="raw-badge">raw JSON</span>
    </label>

    <!-- Bool -->
    <div v-if="kind === 'bool'" class="spec-input-row">
      <input
        type="checkbox"
        :checked="!!boolValue"
        @change="onCheckbox(($event.target as HTMLInputElement).checked)"
      />
    </div>

    <!-- Numeric (S32 / U32 / S64 / U64 / int) -->
    <div v-else-if="kind === 'int'" class="spec-input-row">
      <input
        type="number"
        :value="numericValue"
        :min="numAttr('min')"
        :max="numAttr('max')"
        step="1"
        @input="onNumeric(($event.target as HTMLInputElement).value, true)"
      />
    </div>

    <!-- Floating point -->
    <div v-else-if="kind === 'float'" class="spec-input-row">
      <input
        type="number"
        :value="numericValue"
        :min="numAttr('min')"
        :max="numAttr('max')"
        step="any"
        @input="onNumeric(($event.target as HTMLInputElement).value, false)"
      />
    </div>

    <!-- Enum dropdown -->
    <div v-else-if="kind === 'enum' && spec.enum" class="spec-input-row">
      <select :value="stringValue" @change="emitValue(($event.target as HTMLSelectElement).value)">
        <option v-for="v in spec.enum.values" :key="v" :value="v">{{ v }}</option>
      </select>
    </div>

    <!-- EnumList: comma-separated string of enum values, edited as
         a vertical stack of checkboxes -->
    <div v-else-if="kind === 'enumList' && spec.enum" class="spec-input-row spec-multi">
      <div class="enum-list">
        <label v-for="v in spec.enum.values" :key="v" class="enum-list-item">
          <input
            type="checkbox"
            :checked="enumListSet.has(v)"
            @change="onEnumListToggle(v, ($event.target as HTMLInputElement).checked)"
          />
          {{ v }}
        </label>
      </div>
    </div>

    <!-- FrameRate with backend-supplied presets: dropdown of well-known
         rates + "Custom..." that reveals the underlying n/d editor.
         The presets list arrives via `spec.presets` so the frontend
         never hardcodes well-known rates — they come from
         FrameRate::wellKnownRates() in the library. -->
    <div v-else-if="kind === 'frameRate'" class="spec-input-row spec-frame-rate">
      <select
        :value="frameRatePresetSelection"
        @change="onFrameRateSelect(($event.target as HTMLSelectElement).value)"
      >
        <option v-for="p in spec.presets" :key="p.value" :value="p.value">
          {{ p.label }}
        </option>
        <option :value="FRAME_RATE_CUSTOM_SENTINEL">Custom...</option>
      </select>
      <template v-if="frameRateIsCustom">
        <input
          type="number"
          min="1"
          :value="ratNum"
          @input="onRational('num', ($event.target as HTMLInputElement).value)"
        />
        <span style="color: var(--fg-faint);">/</span>
        <input
          type="number"
          min="1"
          :value="ratDen"
          @input="onRational('den', ($event.target as HTMLInputElement).value)"
        />
      </template>
    </div>

    <!-- Rational<int> / FrameRate stored as "n/d" string -->
    <div v-else-if="kind === 'rational'" class="spec-input-row">
      <input
        type="number"
        :value="ratNum"
        @input="onRational('num', ($event.target as HTMLInputElement).value)"
      />
      <span style="color: var(--fg-faint);">/</span>
      <input
        type="number"
        :value="ratDen"
        @input="onRational('den', ($event.target as HTMLInputElement).value)"
      />
    </div>

    <!-- Size2D as "WxH" -->
    <div v-else-if="kind === 'size2d'" class="spec-input-row">
      <input
        type="number"
        :value="sizeW"
        @input="onSize('w', ($event.target as HTMLInputElement).value)"
      />
      <span style="color: var(--fg-faint);">×</span>
      <input
        type="number"
        :value="sizeH"
        @input="onSize('h', ($event.target as HTMLInputElement).value)"
      />
    </div>

    <!-- Color: HTML5 color picker + canonical wire string +
         independent 0..1 alpha so HDR / non-sRGB models survive.
         Edits in the picker rewrite the sRGB(r,g,b,a) wire form
         and round-trip; pasting a wire string updates the picker. -->
    <div v-else-if="kind === 'color'" class="spec-input-row spec-color">
      <input
        type="color"
        :value="colorHex"
        @input="onColorPicker(($event.target as HTMLInputElement).value)"
      />
      <input
        class="color-text"
        type="text"
        :value="stringValue"
        @input="emitValue(($event.target as HTMLInputElement).value)"
        placeholder="sRGB(r,g,b,a)"
      />
      <input
        class="color-alpha"
        type="number"
        min="0"
        max="1"
        step="0.01"
        :value="colorAlpha"
        @input="onColorAlpha(($event.target as HTMLInputElement).value)"
        title="alpha (0-1)"
      />
    </div>

    <!-- SocketAddress: single text input with placeholder -->
    <div v-else-if="kind === 'socketAddress'" class="spec-input-row">
      <input
        type="text"
        :value="stringValue"
        @input="emitValue(($event.target as HTMLInputElement).value)"
        placeholder="host:port"
      />
    </div>

    <!-- ContentLightLevel: MaxCLL / MaxFALL composite -->
    <div v-else-if="kind === 'contentLightLevel'" class="spec-input-row spec-composite">
      <div class="composite-grid composite-grid-2">
        <label>MaxCLL <input type="number" min="0" step="1"
            :value="cll.maxCLL"
            @input="onCll('maxCLL', ($event.target as HTMLInputElement).value)" /></label>
        <label>MaxFALL <input type="number" min="0" step="1"
            :value="cll.maxFALL"
            @input="onCll('maxFALL', ($event.target as HTMLInputElement).value)" /></label>
      </div>
    </div>

    <!-- MasteringDisplay: 8 fields (R/G/B/WP chromaticities + luminance) -->
    <div v-else-if="kind === 'masteringDisplay'" class="spec-input-row spec-composite">
      <div class="composite-grid composite-grid-md">
        <label>R x <input type="number" step="0.0001"
            :value="md.rx"
            @input="onMd('rx', ($event.target as HTMLInputElement).value)" /></label>
        <label>R y <input type="number" step="0.0001"
            :value="md.ry"
            @input="onMd('ry', ($event.target as HTMLInputElement).value)" /></label>
        <label>G x <input type="number" step="0.0001"
            :value="md.gx"
            @input="onMd('gx', ($event.target as HTMLInputElement).value)" /></label>
        <label>G y <input type="number" step="0.0001"
            :value="md.gy"
            @input="onMd('gy', ($event.target as HTMLInputElement).value)" /></label>
        <label>B x <input type="number" step="0.0001"
            :value="md.bx"
            @input="onMd('bx', ($event.target as HTMLInputElement).value)" /></label>
        <label>B y <input type="number" step="0.0001"
            :value="md.by"
            @input="onMd('by', ($event.target as HTMLInputElement).value)" /></label>
        <label>WP x <input type="number" step="0.0001"
            :value="md.wpx"
            @input="onMd('wpx', ($event.target as HTMLInputElement).value)" /></label>
        <label>WP y <input type="number" step="0.0001"
            :value="md.wpy"
            @input="onMd('wpy', ($event.target as HTMLInputElement).value)" /></label>
        <label>L<sub>min</sub> (nits) <input type="number" min="0" step="0.0001"
            :value="md.minLum"
            @input="onMd('minLum', ($event.target as HTMLInputElement).value)" /></label>
        <label>L<sub>max</sub> (nits) <input type="number" min="0" step="1"
            :value="md.maxLum"
            @input="onMd('maxLum', ($event.target as HTMLInputElement).value)" /></label>
      </div>
    </div>

    <!-- String / generic fallback -->
    <div v-else-if="kind === 'string'" class="spec-input-row">
      <input
        type="text"
        :value="stringValue"
        @input="emitValue(($event.target as HTMLInputElement).value)"
      />
    </div>

    <!-- Raw JSON fallback -->
    <div v-else class="spec-input-row">
      <textarea
        :value="jsonText"
        @blur="onJsonBlur(($event.target as HTMLTextAreaElement).value)"
      ></textarea>
    </div>

    <span v-if="spec.description" class="help">{{ spec.description }}</span>
  </div>
</template>

<script setup lang="ts">
import { computed, watch } from 'vue';
import type { VariantSpec } from '../types/api';

const props = defineProps<{
  name: string;
  spec: VariantSpec;
  modelValue: unknown;
}>();

const emit = defineEmits<{
  (e: 'update:modelValue', value: unknown): void;
}>();

// Pick a primary type for switching. Library reports the most-specific
// type first in the array.
const primary = computed<string>(() => (props.spec.types && props.spec.types[0]) || '');

const kind = computed<string>(() => {
  // Whenever the backend supplied an enum block, prefer the enum
  // editor.  This safety net catches any future Variant type that
  // picks up enum metadata without a code change here.  EnumList is
  // a special case (multi-select) so we still need to disambiguate.
  if (props.spec.enum) {
    if (primary.value === 'EnumList') return 'enumList';
    return 'enum';
  }
  switch (primary.value) {
    case 'bool': return 'bool';
    case 'S32': case 'U32': case 'S64': case 'U64':
    case 'int8_t': case 'uint8_t':
    case 'int16_t': case 'uint16_t':
    case 'int32_t': case 'uint32_t':
    case 'int64_t': case 'uint64_t':
    case 'int':     case 'unsigned':
      return 'int';
    case 'F32': case 'F64':
    case 'float': case 'double':
      return 'float';
    case 'String': case 'std::string':
    case 'VideoFormat':
      return 'string';
    case 'FrameRate':
      // FrameRate with a backend-supplied preset list gets the
      // dropdown-plus-custom editor; specs without presets fall
      // through to the bare n/d rational editor.
      if (props.spec.presets && props.spec.presets.length > 0) return 'frameRate';
      return 'rational';
    case 'Rational<int>': case 'Rational':
      return 'rational';
    case 'Size2Du32': case 'Size2Di32': case 'Size2D':
      return 'size2d';
    case 'Color':
      return 'color';
    case 'SocketAddress':
      return 'socketAddress';
    case 'ContentLightLevel':
      return 'contentLightLevel';
    case 'MasteringDisplay':
      return 'masteringDisplay';
    default:
      return 'json';
  }
});

const usingRawJson = computed(() => kind.value === 'json');

// Surface every fallback during development so we notice when a
// schema introduces a Variant type the editor doesn't recognise.
watch(
  () => kind.value,
  (k) => {
    if (k === 'json') {
      // eslint-disable-next-line no-console
      console.warn(
        `[SpecField] falling back to raw JSON for "${props.name}" (types=${
          (props.spec.types || []).join(',')
        })`,
      );
    }
  },
  { immediate: true },
);

// --- Bool ---
const boolValue = computed(() => Boolean(props.modelValue));

function onCheckbox(v: boolean) {
  emitValue(v);
}

// --- Numeric ---
const numericValue = computed(() => {
  if (typeof props.modelValue === 'number') return props.modelValue;
  if (typeof props.modelValue === 'string') {
    const n = Number(props.modelValue);
    return Number.isFinite(n) ? n : '';
  }
  return '';
});

function numAttr(key: 'min' | 'max'): number | undefined {
  const v = (props.spec as unknown as Record<string, unknown>)[key];
  return typeof v === 'number' ? v : undefined;
}

function onNumeric(raw: string, _integer: boolean) {
  if (raw === '' || raw === '-') {
    emitValue(raw);
    return;
  }
  const n = Number(raw);
  if (Number.isFinite(n)) emitValue(n);
}

// --- Enum / String ---
const stringValue = computed(() => {
  const v = props.modelValue;
  if (v == null) return '';
  if (typeof v === 'string') return v;
  return String(v);
});

// --- EnumList: comma-separated wire form ---
const enumListSet = computed<Set<string>>(() => {
  const set = new Set<string>();
  const v = props.modelValue;
  if (typeof v !== 'string' || v.length === 0) return set;
  for (const tok of v.split(',')) {
    const trimmed = tok.trim();
    if (trimmed) set.add(trimmed);
  }
  return set;
});

function onEnumListToggle(value: string, on: boolean) {
  // Preserve the spec's declared order so the wire form is stable
  // across toggles.  Falls back to the current set order when the
  // spec doesn't carry an enum block (defensive).
  const order = props.spec.enum?.values ?? Array.from(enumListSet.value);
  const set = new Set(enumListSet.value);
  if (on) set.add(value);
  else set.delete(value);
  const out: string[] = [];
  for (const v of order) {
    if (set.has(v)) out.push(v);
  }
  emitValue(out.join(','));
}

// --- Rational<int> / FrameRate stored as "num/den" string ---
const ratNum = computed(() => parseRational().num);
const ratDen = computed(() => parseRational().den);

function parseRational(): { num: number; den: number } {
  const v = props.modelValue;
  if (typeof v === 'string' && v.includes('/')) {
    const [n, d] = v.split('/');
    return { num: Number(n) || 0, den: Number(d) || 1 };
  }
  if (typeof v === 'number') return { num: v, den: 1 };
  return { num: 0, den: 1 };
}

function onRational(part: 'num' | 'den', raw: string) {
  const parsed = parseRational();
  const n = Number(raw);
  if (!Number.isFinite(n)) return;
  if (part === 'num') parsed.num = n;
  else parsed.den = n || 1;
  emitValue(`${parsed.num}/${parsed.den}`);
}

// --- FrameRate (presets + Custom...) ---
// The dropdown holds the canonical wire form ("24/1", "30000/1001",
// ...) as the option value, which keeps the round-trip exact —
// we emit whatever the user picked verbatim.  Selecting "Custom..."
// reveals the n/d inputs (which already drive the rational wire form
// via onRational), and any value not present in the preset list
// auto-shows the inputs so the user can edit it.
const FRAME_RATE_CUSTOM_SENTINEL = '__custom__';

const frameRatePresetSelection = computed<string>(() => {
  const v = stringValue.value;
  const presets = props.spec.presets ?? [];
  for (const p of presets) {
    if (p.value === v) return v;
  }
  return FRAME_RATE_CUSTOM_SENTINEL;
});

const frameRateIsCustom = computed<boolean>(
  () => frameRatePresetSelection.value === FRAME_RATE_CUSTOM_SENTINEL,
);

function onFrameRateSelect(value: string) {
  if (value === FRAME_RATE_CUSTOM_SENTINEL) {
    // Stay on the current model value — switching to "Custom..."
    // just reveals the editor; we don't want to overwrite the
    // user's existing rate.  If the current value happened to
    // already match a preset (i.e. the user just clicked
    // "Custom..." for the first time on a preset value), leaving
    // the value alone is still correct — the n/d inputs will
    // show that exact value and the user edits from there.
    return;
  }
  emitValue(value);
}

// --- Size2D stored as "WxH" string ---
const sizeW = computed(() => parseSize().w);
const sizeH = computed(() => parseSize().h);

function parseSize(): { w: number; h: number } {
  const v = props.modelValue;
  if (typeof v === 'string') {
    const m = /([0-9]+)\s*[x×]\s*([0-9]+)/i.exec(v);
    if (m) return { w: Number(m[1]) || 0, h: Number(m[2]) || 0 };
  }
  if (v && typeof v === 'object') {
    const o = v as Record<string, unknown>;
    return {
      w: typeof o.width === 'number' ? o.width : 0,
      h: typeof o.height === 'number' ? o.height : 0,
    };
  }
  return { w: 0, h: 0 };
}

function onSize(part: 'w' | 'h', raw: string) {
  const parsed = parseSize();
  const n = Number(raw);
  if (!Number.isFinite(n)) return;
  if (part === 'w') parsed.w = n;
  else parsed.h = n;
  emitValue(`${parsed.w}x${parsed.h}`);
}

// --- Color ---
// The C++ side serializes Color in "ModelFormat":
//   sRGB(r,g,b,a)        — normalized floats 0..1
// (ContentLightLevel and similar `r,g,b,a` triplets also possible.)
// The HTML5 <input type="color"> only handles 7-char #rrggbb; we
// derive that from the first three components and round-trip.

interface ParsedColor {
  model: string;
  r: number;
  g: number;
  b: number;
  a: number;
}

function parseColor(): ParsedColor {
  const def: ParsedColor = { model: 'sRGB', r: 0, g: 0, b: 0, a: 1 };
  const v = props.modelValue;
  if (typeof v !== 'string' || v.length === 0) return def;

  // ModelFormat: ModelName(c0,c1,c2[,c3])
  const model = /^([A-Za-z][A-Za-z0-9_]*)\s*\(\s*([^)]*)\)\s*$/.exec(v);
  if (model) {
    const parts = model[2].split(',').map((s) => Number(s.trim()));
    return {
      model: model[1],
      r: Number.isFinite(parts[0]) ? parts[0] : 0,
      g: Number.isFinite(parts[1]) ? parts[1] : 0,
      b: Number.isFinite(parts[2]) ? parts[2] : 0,
      a: Number.isFinite(parts[3]) ? parts[3] : 1,
    };
  }
  // Hex form.
  const hex = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(v);
  if (hex) {
    const rgb = hex[1];
    return {
      model: 'sRGB',
      r: parseInt(rgb.slice(0, 2), 16) / 255,
      g: parseInt(rgb.slice(2, 4), 16) / 255,
      b: parseInt(rgb.slice(4, 6), 16) / 255,
      a: hex[2] ? parseInt(hex[2], 16) / 255 : 1,
    };
  }
  return def;
}

const colorHex = computed(() => {
  const c = parseColor();
  const to8 = (x: number) => Math.max(0, Math.min(255, Math.round(x * 255)));
  const hh = (n: number) => n.toString(16).padStart(2, '0');
  return `#${hh(to8(c.r))}${hh(to8(c.g))}${hh(to8(c.b))}`;
});

const colorAlpha = computed(() => {
  const c = parseColor();
  return c.a;
});

function emitColor(c: ParsedColor) {
  // Trim trailing zeros for a tidier wire form, but keep at least
  // one decimal — Color::fromString accepts both "1" and "1.0".
  const fmt = (n: number) => {
    const s = n.toFixed(4);
    return s.replace(/\.?0+$/, '') || '0';
  };
  emitValue(`${c.model}(${fmt(c.r)},${fmt(c.g)},${fmt(c.b)},${fmt(c.a)})`);
}

function onColorPicker(hex: string) {
  const c = parseColor();
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  if (!m) return;
  c.r = parseInt(m[1], 16) / 255;
  c.g = parseInt(m[2], 16) / 255;
  c.b = parseInt(m[3], 16) / 255;
  emitColor(c);
}

function onColorAlpha(raw: string) {
  const c = parseColor();
  const n = Number(raw);
  if (!Number.isFinite(n)) return;
  c.a = Math.max(0, Math.min(1, n));
  emitColor(c);
}

// --- ContentLightLevel ---
// Wire shape is the toString form `MaxCLL=1000 MaxFALL=400 cd/m²`.
// We emit that exact format so the user-config round-trips through
// the demo's JSON pipeline; the C++ side currently has no parser
// for this string, so reload-after-edit is informational only.
interface ParsedCll {
  maxCLL: number;
  maxFALL: number;
}

function parseCll(): ParsedCll {
  const def: ParsedCll = { maxCLL: 0, maxFALL: 0 };
  const v = props.modelValue;
  if (typeof v !== 'string' || v.length === 0) return def;
  const cll = /MaxCLL\s*=\s*([0-9]+)/i.exec(v);
  const fall = /MaxFALL\s*=\s*([0-9]+)/i.exec(v);
  return {
    maxCLL: cll ? Number(cll[1]) || 0 : 0,
    maxFALL: fall ? Number(fall[1]) || 0 : 0,
  };
}

const cll = computed(() => parseCll());

function onCll(field: keyof ParsedCll, raw: string) {
  const cur = parseCll();
  const n = Number(raw);
  if (!Number.isFinite(n)) return;
  cur[field] = n;
  emitValue(`MaxCLL=${cur.maxCLL} MaxFALL=${cur.maxFALL} cd/m²`);
}

// --- MasteringDisplay ---
// Wire shape is the toString form
//   R(0.7080,0.2920) G(0.1700,0.7970) B(0.1310,0.0460)
//   WP(0.3127,0.3290) Lmin=0.0050 Lmax=1000.0 cd/m²
// The C++ side has no parser; same caveat as ContentLightLevel.
interface ParsedMd {
  rx: number; ry: number;
  gx: number; gy: number;
  bx: number; by: number;
  wpx: number; wpy: number;
  minLum: number; maxLum: number;
}

function parseMd(): ParsedMd {
  const def: ParsedMd = {
    rx: 0, ry: 0, gx: 0, gy: 0, bx: 0, by: 0,
    wpx: 0, wpy: 0, minLum: 0, maxLum: 0,
  };
  const v = props.modelValue;
  if (typeof v !== 'string' || v.length === 0) return def;
  const grab = (re: RegExp): [number, number] => {
    const m = re.exec(v);
    if (!m) return [0, 0];
    return [Number(m[1]) || 0, Number(m[2]) || 0];
  };
  const [rx, ry] = grab(/R\s*\(\s*([-0-9.eE+]+)\s*,\s*([-0-9.eE+]+)\s*\)/);
  const [gx, gy] = grab(/G\s*\(\s*([-0-9.eE+]+)\s*,\s*([-0-9.eE+]+)\s*\)/);
  const [bx, by] = grab(/B\s*\(\s*([-0-9.eE+]+)\s*,\s*([-0-9.eE+]+)\s*\)/);
  const [wpx, wpy] = grab(/WP\s*\(\s*([-0-9.eE+]+)\s*,\s*([-0-9.eE+]+)\s*\)/);
  const lmin = /Lmin\s*=\s*([-0-9.eE+]+)/.exec(v);
  const lmax = /Lmax\s*=\s*([-0-9.eE+]+)/.exec(v);
  return {
    rx, ry, gx, gy, bx, by, wpx, wpy,
    minLum: lmin ? Number(lmin[1]) || 0 : 0,
    maxLum: lmax ? Number(lmax[1]) || 0 : 0,
  };
}

const md = computed(() => parseMd());

function onMd(field: keyof ParsedMd, raw: string) {
  const cur = parseMd();
  const n = Number(raw);
  if (!Number.isFinite(n)) return;
  cur[field] = n;
  // Match the C++ toString format exactly so the user-config
  // serializes to a recognisable string when round-tripped.
  const f4 = (x: number) => x.toFixed(4);
  const f1 = (x: number) => x.toFixed(1);
  emitValue(
    `R(${f4(cur.rx)},${f4(cur.ry)}) ` +
      `G(${f4(cur.gx)},${f4(cur.gy)}) ` +
      `B(${f4(cur.bx)},${f4(cur.by)}) ` +
      `WP(${f4(cur.wpx)},${f4(cur.wpy)}) ` +
      `Lmin=${f4(cur.minLum)} Lmax=${f1(cur.maxLum)} cd/m²`,
  );
}

// --- Raw JSON fallback ---
const jsonText = computed(() => {
  try {
    return JSON.stringify(props.modelValue ?? null, null, 2);
  } catch {
    return String(props.modelValue ?? '');
  }
});

function onJsonBlur(raw: string) {
  try {
    emitValue(JSON.parse(raw));
  } catch {
    // leave as string so the user can fix it
    emitValue(raw);
  }
}

function emitValue(v: unknown) {
  emit('update:modelValue', v);
}
</script>
