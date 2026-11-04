// npm run set-version 0.8.1 — sets the app version in package.json (which
// the Qt build reads), package-lock.json and in the Rust crates' Cargo.toml files.
import { readFileSync, writeFileSync } from "node:fs";

const v = (process.argv[2] ?? "").replace(/^v/, "");
if (!/^\d+\.\d+\.\d+$/.test(v)) {
  console.error("usage: npm run set-version 1.2.3");
  process.exit(1);
}
const pkg = JSON.parse(readFileSync("package.json", "utf8"));
pkg.version = v;
writeFileSync("package.json", JSON.stringify(pkg, null, 2) + "\n");
const lock = JSON.parse(readFileSync("package-lock.json", "utf8"));
lock.version = v;
if (lock.packages?.[""]) lock.packages[""].version = v;
writeFileSync("package-lock.json", JSON.stringify(lock, null, 2) + "\n");
for (const p of ["core/Cargo.toml", "qt/native/Cargo.toml"]) {
  writeFileSync(p, readFileSync(p, "utf8").replace(/^version\s*=\s*"[^"]+"/m, `version = "${v}"`));
}
console.log(`version set to ${v} — commit, then: git tag v${v} && git push origin main v${v}`);
