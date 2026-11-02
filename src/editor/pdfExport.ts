// Builds a vector PDF of notebook pages using the regular page renderer.

import type { Page } from "../api";
import type { FontId } from "../fonts";
import type { El } from "./model";
import { loadFaces, type FaceSpec } from "./pdfFonts";
import { canvasMeasurer, markOpaque, PdfDocument, PdfPainter } from "./pdfVector";
import { drawElement, drawTemplate, type ImageCache } from "./render";
import { templateUsesText } from "./papers";

export interface PdfPageInput {
  page: Page;
  elements: El[];
  /** Rendered PDF background (imported PDFs), embedded as an image. */
  background: HTMLCanvasElement | null;
}

/** The font faces used by text on these pages. */
function facesOf(pages: PdfPageInput[]): FaceSpec[] {
  const out = new Map<string, FaceSpec>();
  for (const p of pages) {
    for (const el of p.elements) {
      if (el.kind !== "text" && el.kind !== "sticky") continue;
      const spec: FaceSpec = { font: (el.font ?? "sans") as FontId, bold: !!el.bold, italic: !!el.italic };
      out.set(`${spec.font}/${spec.bold}/${spec.italic}`, spec);
    }
  }
  // Planner labels are drawn in the regular sans face.
  if (pages.some((p) => templateUsesText(p.page.template))) out.set("sans/false/false", { font: "sans", bold: false, italic: false });
  if (pages.some((p) => templateUsesText(p.page.template))) out.set("sans/true/false", { font: "sans", bold: true, italic: false });
  return [...out.values()];
}

export async function buildVectorPdf(pages: PdfPageInput[], images: ImageCache, title: string): Promise<Uint8Array> {
  await loadFaces(facesOf(pages));
  const measurer = canvasMeasurer();
  const doc = new PdfDocument();
  for (const { page, elements, background } of pages) {
    const painter = new PdfPainter(page.width, page.height, measurer);
    const ctx = painter as unknown as CanvasRenderingContext2D;
    drawTemplate(ctx, page.template, page.width, page.height, page.paperColor);
    if (background) ctx.drawImage(markOpaque(background), 0, 0, page.width, page.height);
    for (const el of elements) drawElement(ctx, el, images, () => {});
    await doc.addPage(painter.rec);
  }
  return doc.finish(title);
}
