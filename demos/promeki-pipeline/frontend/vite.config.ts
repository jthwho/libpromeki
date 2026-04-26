import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

// Phase E will flesh this out — for now this exists so `npm run build` from
// inside frontend/ produces a fresh ../frontend-dist/ that cirf can pick up.
export default defineConfig({
        plugins: [vue()],
        build: {
                outDir: '../frontend-dist',
                emptyOutDir: true,
        },
        server: {
                proxy: {
                        '/api/events': {
                                target: 'ws://localhost:8080',
                                ws: true,
                                changeOrigin: true,
                        },
                        '/api': {
                                target: 'http://localhost:8080',
                                changeOrigin: true,
                        },
                },
        },
});
