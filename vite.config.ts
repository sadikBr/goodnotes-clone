import { defineConfig } from "vitest/config";

// Tauri expects a fixed dev-server port and must not clear its own output.
export default defineConfig({
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: "127.0.0.1",
    watch: { ignored: ["**/src-tauri/**"] },
  },
  envPrefix: ["VITE_", "TAURI_ENV_"],
  build: {
    target: "es2021",
    sourcemap: false,
  },
  test: {
    environment: "node",
  },
});
