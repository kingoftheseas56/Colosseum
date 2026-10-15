import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  base: "./",
  plugins: [react()],
  build: {
    outDir: "dist/renderer",
    emptyOutDir: true
  },
  test: {
    environment: "jsdom",
    setupFiles: ["./src/renderer/testSetup.ts"],
    exclude: ["dist/**", "release/**", "node_modules/**"],
    testTimeout: 15000,
    css: true
  }
});
