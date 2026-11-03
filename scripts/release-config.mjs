// Writes src-tauri/tauri.release.conf.json for CI release builds: updater
// artifacts on, and the updater's public key (from the TAURI_UPDATER_PUBKEY
// repository variable). Local builds stay unsigned and update-free.
import { writeFileSync } from "node:fs";

const pubkey = (process.env.TAURI_UPDATER_PUBKEY ?? "").trim();
if (!pubkey) {
  console.error("TAURI_UPDATER_PUBKEY is not set (Settings → Secrets and variables → Actions → Variables)");
  process.exit(1);
}
writeFileSync(
  new URL("../src-tauri/tauri.release.conf.json", import.meta.url),
  JSON.stringify({ bundle: { createUpdaterArtifacts: true }, plugins: { updater: { pubkey } } }, null, 2),
);
console.log("release config written");
