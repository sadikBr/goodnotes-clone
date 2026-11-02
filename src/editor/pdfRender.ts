// Renders pages of imported PDFs (pdf.js, bundled for offline use) as page
// backgrounds. pdf.js is loaded on first use so it doesn't slow startup.

import { api } from "../api";

type PdfJs = typeof import("pdfjs-dist/legacy/build/pdf.mjs");
type PdfDoc = Awaited<ReturnType<PdfJs["getDocument"]>["promise"]>;

/** Where scripts/copy-pdfjs-assets.mjs puts pdf.js data files. */
const ASSETS = "/pdfjs/";
/** PDF points are 1/72 inch; Inkleaf units are 1/96 inch. */
export const PT_TO_UNITS = 96 / 72;

let pdfjsPromise: Promise<PdfJs> | null = null;
function pdfjs(): Promise<PdfJs> {
  pdfjsPromise ??= (async () => {
    const lib = await import("pdfjs-dist/legacy/build/pdf.mjs");
    const worker = await import("pdfjs-dist/legacy/build/pdf.worker.min.mjs?url");
    lib.GlobalWorkerOptions.workerSrc = worker.default;
    return lib;
  })();
  return pdfjsPromise;
}

async function open(data: Uint8Array): Promise<PdfDoc> {
  return (await load(data)).promise;
}

async function load(data: Uint8Array) {
  const lib = await pdfjs();
  return lib.getDocument({
    data: data.slice(), // pdf.js takes ownership of the buffer it is given
    cMapUrl: `${ASSETS}cmaps/`,
    cMapPacked: true,
    standardFontDataUrl: `${ASSETS}standard_fonts/`,
    wasmUrl: `${ASSETS}wasm/`,
    iccUrl: `${ASSETS}iccs/`,
  });
}

/** Page sizes of a PDF file, in Inkleaf units. */
export async function pdfPageSizes(data: Uint8Array): Promise<{ width: number; height: number }[]> {
  const task = await load(data);
  const doc = await task.promise;
  try {
    const sizes: { width: number; height: number }[] = [];
    for (let i = 1; i <= doc.numPages; i++) {
      const vp = (await doc.getPage(i)).getViewport({ scale: 1 });
      sizes.push({ width: Math.round(vp.width * PT_TO_UNITS), height: Math.round(vp.height * PT_TO_UNITS) });
    }
    return sizes;
  } finally {
    void task.destroy();
  }
}

const docs = new Map<string, Promise<PdfDoc>>();
function docFor(assetId: string): Promise<PdfDoc> {
  let d = docs.get(assetId);
  if (!d) {
    d = api.assetBytes(assetId).then(open);
    d.catch(() => docs.delete(assetId));
    docs.set(assetId, d);
  }
  return d;
}

interface Rendered {
  /** Sharpness achieved (may be capped for huge pages). */
  scale: number;
  /** Sharpness that was asked for. */
  requested: number;
  canvas: HTMLCanvasElement;
}
/** Rendered pages, most recently used last; trimmed to keep memory bounded. */
const rendered = new Map<string, Rendered>();
const pending = new Map<string, Promise<HTMLCanvasElement>>();
const MAX_RENDERED = 16;
const MAX_PIXELS = 24_000_000;

const keyOf = (assetId: string, page: number) => `${assetId}#${page}`;

function remember(key: string, r: Rendered) {
  rendered.delete(key);
  rendered.set(key, r);
  while (rendered.size > MAX_RENDERED) rendered.delete(rendered.keys().next().value!);
}

/**
 * The best render already available for a page (any sharpness), or null.
 * Lets callers draw immediately and refresh when a sharper one is ready.
 */
export function cachedBackground(assetId: string, page: number): { canvas: HTMLCanvasElement; requested: number } | null {
  const r = rendered.get(keyOf(assetId, page));
  return r ? { canvas: r.canvas, requested: r.requested } : null;
}

/**
 * Renders a PDF page at `scale` canvas pixels per Inkleaf unit (reusing a
 * render that is at least as sharp).
 */
export function renderBackground(assetId: string, page: number, scale: number): Promise<HTMLCanvasElement> {
  const key = keyOf(assetId, page);
  const have = rendered.get(key);
  if (have && have.requested >= scale * 0.95) {
    remember(key, have);
    return Promise.resolve(have.canvas);
  }
  const pendingKey = `${key}@${scale.toFixed(2)}`;
  const inFlight = pending.get(pendingKey);
  if (inFlight) return inFlight;
  const job = (async () => {
    const doc = await docFor(assetId);
    const p = await doc.getPage(page + 1);
    let vp = p.getViewport({ scale: scale * PT_TO_UNITS });
    // Keep huge zoom levels within sensible canvas sizes.
    const shrink = Math.min(1, Math.sqrt(MAX_PIXELS / (vp.width * vp.height)));
    if (shrink < 1) vp = p.getViewport({ scale: scale * PT_TO_UNITS * shrink });
    const canvas = document.createElement("canvas");
    canvas.width = Math.ceil(vp.width);
    canvas.height = Math.ceil(vp.height);
    await p.render({ canvas, canvasContext: canvas.getContext("2d")!, viewport: vp }).promise;
    const current = rendered.get(key);
    if (!current || current.requested < scale) remember(key, { scale: scale * shrink, requested: scale, canvas });
    return canvas;
  })().finally(() => pending.delete(pendingKey));
  pending.set(pendingKey, job);
  return job;
}
