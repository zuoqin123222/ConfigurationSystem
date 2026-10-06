import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig({
  base: './',
  plugins: [react()],
  build: {
    outDir: '../../../package/clients/web',
    emptyOutDir: true,
  },
  server: {
    proxy: {
      '/health': 'http://localhost:8080',
      '/api': 'http://localhost:8080',
      '/assets': 'http://localhost:8080',
    },
  },
  test: {
    environment: 'jsdom',
    setupFiles: './src/test/setup.ts',
  },
})
