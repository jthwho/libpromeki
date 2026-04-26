// Pinia store: the /api/types catalog plus per-type schema/defaults/metadata
// caches. Schemas are pulled lazily — the catalog itself comes in once at
// app boot.
import { defineStore } from 'pinia';
import { api } from '../api';
import type {
  MediaConfigJson,
  MetadataJson,
  TypeSchema,
  TypeSummary,
} from '../types/api';

interface State {
  catalog: TypeSummary[];
  loaded: boolean;
  loading: boolean;
  schemas: Record<string, TypeSchema>;
  defaults: Record<string, MediaConfigJson>;
  metadata: Record<string, MetadataJson>;
}

export const useTypesStore = defineStore('types', {
  state: (): State => ({
    catalog: [],
    loaded: false,
    loading: false,
    schemas: {},
    defaults: {},
    metadata: {},
  }),
  getters: {
    byName: (s) => (name: string) => s.catalog.find((t) => t.name === name),
    sources: (s) => s.catalog.filter((t) => t.modes.includes('Source')),
    sinks: (s) => s.catalog.filter((t) => t.modes.includes('Sink')),
    transforms: (s) => s.catalog.filter((t) => t.modes.includes('Transform')),
    typeNames: (s) => s.catalog.map((t) => t.name),
  },
  actions: {
    async loadCatalog(force = false) {
      if (this.loaded && !force) return;
      if (this.loading) return;
      this.loading = true;
      try {
        this.catalog = await api.listTypes();
        this.loaded = true;
      } finally {
        this.loading = false;
      }
    },
    async schema(name: string): Promise<TypeSchema> {
      if (this.schemas[name]) return this.schemas[name];
      const s = await api.schema(name);
      this.schemas[name] = s;
      return s;
    },
    async defaultConfig(name: string): Promise<MediaConfigJson> {
      if (this.defaults[name]) return this.defaults[name];
      const d = await api.defaults(name);
      this.defaults[name] = d;
      return d;
    },
    async defaultMetadata(name: string): Promise<MetadataJson> {
      if (this.metadata[name]) return this.metadata[name];
      const m = await api.metadata(name);
      this.metadata[name] = m;
      return m;
    },
  },
});
