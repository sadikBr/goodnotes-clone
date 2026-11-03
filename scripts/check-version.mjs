// The pushed tag (v1.2.3) must match the app's version everywhere, or the
// updater would compare the wrong numbers.
import { readFileSync } from "node:fs";

const tag = (process.env.TAG ?? "").replace(/^v/, "");
const pkg = JSON.parse(readFileSync("package.json", "utf8")).version;
const conf = JSON.parse(readFileSync("src-tauri/tauri.conf.json", "utf8")).version;
const cargo = /^version\s*=\s*"([^"]+)"/m.exec(readFileSync("src-tauri/Cargo.toml", "utf8"))?.[1];
const all = { tag, "package.json": pkg, "tauri.conf.json": conf, "Cargo.toml": cargo };
if (new Set(Object.values(all)).size !== 1) {
  console.error("Version mismatch:", all, "\nRun: npm run set-version <version>");
  process.exit(1);
}
console.log(`version ${tag} ok`);
