// npm run set-version 0.8.1 — sets the version in package.json,
// src-tauri/tauri.conf.json and src-tauri/Cargo.toml.
import { readFileSync, writeFileSync } from "node:fs";

const v = (process.argv[2] ?? "").replace(/^v/, "");
if (!/^\d+\.\d+\.\d+$/.test(v)) {
  console.error("usage: npm run set-version 1.2.3");
  process.exit(1);
}
const json = (p, f) => {
  const d = JSON.parse(readFileSync(p, "utf8"));
  f(d);
  writeFileSync(p, JSON.stringify(d, null, 2) + "\n");
};
json("package.json", (d) => (d.version = v));
json("src-tauri/tauri.conf.json", (d) => (d.version = v));
const cargo = readFileSync("src-tauri/Cargo.toml", "utf8").replace(/^version\s*=\s*"[^"]+"/m, `version = "${v}"`);
writeFileSync("src-tauri/Cargo.toml", cargo);
console.log(`version set to ${v} — commit, then: git tag v${v} && git push origin v${v}`);
