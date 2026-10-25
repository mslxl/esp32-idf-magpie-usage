import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

export default defineConfig(({ mode }) => ({
  base: "./",
  plugins: [solid({ hot: mode !== "test" })],
}));
