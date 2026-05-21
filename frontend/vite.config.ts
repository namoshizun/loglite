import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
  ],
  server: {
    proxy: {
      '/health': 'http://localhost:7788',
      '/version': 'http://localhost:7788',
      '/logs': 'http://localhost:7788',
      '/stats': 'http://localhost:7788',
    },
  },
})

