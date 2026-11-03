// npm run licenses — writes public/third-party-licenses.txt from the LICENSE
// of every bundled @fontsource package (the fonts are redistributed with the app).
import { existsSync, readFileSync, writeFileSync } from "node:fs";

const pkg = JSON.parse(readFileSync("package.json", "utf8"));
const fonts = Object.keys(pkg.dependencies).filter((d) => d.startsWith("@fontsource/")).sort();
const rule = "-".repeat(72);
let out = "Third-party fonts bundled with Inkleaf\n======================================\n\nThe fonts below are redistributed unmodified via the @fontsource npm packages, under the licences that follow.\n";
for (const name of fonts) {
  const file = ["LICENSE", "LICENSE.md", "license"].map((f) => `node_modules/${name}/${f}`).find(existsSync);
  if (!file) throw new Error(`no licence file in ${name}`);
  out += `\n${rule}\n${name}\n${rule}\n${readFileSync(file, "utf8").trim()}\n`;
}
writeFileSync("public/third-party-licenses.txt", out);
console.log(`licences for ${fonts.length} font packages written`);
