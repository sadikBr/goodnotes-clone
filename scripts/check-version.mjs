// The pushed tag (v1.2.3) must match the app's version everywhere, or the
// app would compare the wrong numbers when it looks for updates.
import { readFileSync } from "node:fs";

const tag = (process.env.TAG ?? "").replace(/^v/, "");
const cargo = (p) => /^version\s*=\s*"([^"]+)"/m.exec(readFileSync(p, "utf8"))?.[1];
const all = {
  tag,
  "package.json": JSON.parse(readFileSync("package.json", "utf8")).version,
  "core/Cargo.toml": cargo("core/Cargo.toml"),
  "qt/native/Cargo.toml": cargo("qt/native/Cargo.toml"),
};
if (new Set(Object.values(all)).size !== 1) {
  console.error("Version mismatch:", all, "\nRun: npm run set-version <version>");
  process.exit(1);
}
console.log(`version ${tag} ok`);
