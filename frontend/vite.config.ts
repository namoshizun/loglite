import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

const API_PATHS = ['/health', '/version', '/logs', '/stats', '/settings', '/schema'];

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const apiTarget = env.VITE_API_BASE_URL || 'http://localhost:7788';

  return {
    plugins: [react(), tailwindcss()],
    server: {
      fs: {
        allow: ['..'],
      },
      proxy: Object.fromEntries(API_PATHS.map((path) => [path, apiTarget])),
    },
  };
});
