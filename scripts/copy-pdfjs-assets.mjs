// Copies pdf.js runtime data (character maps, standard fonts, WebAssembly
// image decoders, colour profiles) into public/pdfjs so PDFs render offline.
import { cpSync, existsSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const src = join(root, "node_modules", "pdfjs-dist");
const dest = join(root, "public", "pdfjs");
mkdirSync(dest, { recursive: true });
for (const dir of ["cmaps", "standard_fonts", "wasm", "iccs"]) {
  if (existsSync(join(src, dir))) cpSync(join(src, dir), join(dest, dir), { recursive: true });
}
cpSync(join(src, "LICENSE"), join(dest, "LICENSE"));
console.log("pdf.js assets copied to public/pdfjs");
