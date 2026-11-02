// Cover pages: picking a design, placing the title and date (ordinary text
// boxes, tagged so they can be re-laid out when the design changes), and
// drawing the cover's picture for the library.

import { api, type Notebook, type Page } from "./api";
import { ensureFonts } from "./fonts";
import { COVER_DESIGNS, coverDesignOf, coverTemplate, coverText, isCoverTemplate, type CoverDesign } from "./editor/covers";
import { fromStored, newId, toStored, type El, type TextEl } from "./editor/model";
import { measureText, renderPage } from "./editor/render";
import { h } from "./ui";

const DATE_RE = /^[\p{L} .,-]*\d{4}$/u;

/** Places the title and date for a design: the largest size that fits on at most two lines. */
function layoutText(design: CoverDesign, title: string, date: string, w: number, hgt: number): { title: Partial<TextEl>; date: Partial<TextEl> } {
  const t = coverText(design, w, hgt);
  const box = t.title;
  let size = t.maxSize;
  const width = (s: number) => measureText(title, t.font, s) * 1.06;
  while (size > 16 && width(size) > box.w * (size > t.maxSize * 0.78 ? 1 : 2)) size -= 2;
  const lines = width(size) > box.w ? 2 : 1;
  const lineH = size * 1.35;
  const y = Math.max(box.y - 6, box.y + (box.h - lines * lineH) / 2);
  const common = { font: t.font, align: t.align, rotation: 0 } as const;
  return {
    title: { ...common, x: box.x, y, w: box.w, fontSize: size, color: t.titleColor, bold: t.font === "sans", text: title },
    date: { ...common, x: t.date.x, y: t.date.y, w: t.date.w, fontSize: t.dateSize, color: t.dateColor, bold: false, text: date },
  };
}

/** The title and date text boxes for a new cover. */
export function coverTextElements(design: CoverDesign, title: string, date: string, w: number, hgt: number): TextEl[] {
  const l = layoutText(design, title, date, w, hgt);
  return [
    { ...(l.title as TextEl), id: newId(), z: 0, kind: "text", role: "cover-title" },
    { ...(l.date as TextEl), id: newId(), z: 1, kind: "text", role: "cover-date" },
  ];
}

/** Draws and stores a cover page's thumbnail (for the library). */
export async function refreshCoverThumbnail(page: Page): Promise<void> {
  const els = fromStored(await api.elements(page.id));
  await ensureFonts(["sans", "caveat", "kalam"]);
  const scale = 420 / page.width; // sharp: the library shows covers full-size
  const thumb = renderPage(page.template, page.paperColor, page.width, page.height, els, scale, new Map()).toDataURL("image/jpeg", 0.85);
  await api.setPageThumbnail(page.id, thumb);
}

const monthYear = () => new Date().toLocaleDateString(undefined, { month: "long", year: "numeric" });

/** Puts the title and today's month on a new notebook's cover, and draws its thumbnail. */
export async function setUpCover(nb: Notebook): Promise<void> {
  const [page] = await api.pages(nb.id);
  if (!page || !isCoverTemplate(page.template)) return;
  await ensureFonts(["sans", "caveat", "kalam"]);
  await api.saveElements(page.id, coverTextElements(coverDesignOf(page.template), nb.title, monthYear(), page.width, page.height).map(toStored));
  await refreshCoverThumbnail(page);
}

/**
 * Applies a cover design and colour to a notebook: switches the cover page's
 * design (moving its title and date into place, keeping their text), or adds
 * a cover page to a notebook that has none.
 */
export async function applyCover(nb: Notebook, design: CoverDesign, color: string): Promise<void> {
  await api.setNotebookCover(nb.id, color);
  await ensureFonts(["sans", "caveat", "kalam"]);
  const template = coverTemplate(design);
  const pages = await api.pages(nb.id);
  let page = pages[0];
  if (!page || !isCoverTemplate(page.template)) {
    page = await api.addPage(nb.id, 0, template);
    await api.setPageTemplate(page.id, template, color);
    await api.saveElements(page.id, coverTextElements(design, nb.title, monthYear(), page.width, page.height).map(toStored));
  } else {
    await api.setPageTemplate(page.id, template, color);
    const els = fromStored(await api.elements(page.id));
    // The title and date: tagged ones, or (older covers) the text matching them.
    const titleEl = els.find((e): e is TextEl => e.kind === "text" && (e.role === "cover-title" || (!e.role && e.text === nb.title)));
    const dateEl = els.find((e): e is TextEl => e.kind === "text" && e !== titleEl && (e.role === "cover-date" || (!e.role && DATE_RE.test(e.text.trim()))));
    const l = layoutText(design, titleEl?.text ?? nb.title, dateEl?.text ?? monthYear(), page.width, page.height);
    const next: El[] = els.map((e) => (e === titleEl ? { ...e, ...l.title, role: "cover-title" } : e === dateEl ? { ...e, ...l.date, role: "cover-date" } : e)) as El[];
    await api.saveElements(page.id, next.map(toStored));
  }
  page = (await api.pages(nb.id))[0];
  await refreshCoverThumbnail(page);
}

/** A small picture of a cover design with the given title and colour. */
export function coverPreview(design: CoverDesign, color: string, title: string, cssW: number, landscape = false): HTMLCanvasElement {
  const w = landscape ? 1123 : 794;
  const hgt = landscape ? 794 : 1123;
  const cssH = (cssW * hgt) / w;
  const dpr = Math.max(2, window.devicePixelRatio || 1);
  const els = coverTextElements(design, title || "Untitled", monthYear(), w, hgt);
  const c = renderPage(coverTemplate(design), color, w, hgt, els, (cssW * dpr) / w, new Map());
  c.style.width = `${cssW}px`;
  c.style.height = `${cssH}px`;
  return c;
}

/**
 * A row of cover design previews for a form (value: the design id). It
 * redraws when the title, colour or orientation fields change. `allowNone`
 * adds a "No cover" choice (for notebooks without one).
 */
export function coverPickerField(initial: CoverDesign | "none", allowNone = false) {
  return {
    name: "design",
    label: "Cover design",
    type: "custom" as const,
    value: initial,
    render: (ctx: { values: Record<string, string>; set(v: string): void; onChange(fn: () => void): void }) => {
      const row = h("div", { class: "cover-designs", role: "radiogroup", "aria-label": "Cover design" });
      const draw = () => {
        const v = ctx.values;
        const color = v.cover || "#2563eb";
        const landscape = v.orientation === "landscape";
        const tiles = [
          ...(allowNone ? [{ id: "none" as const, label: "No cover" }] : []),
          ...COVER_DESIGNS.map((d) => ({ id: d.id, label: d.label })),
        ].map((d) => {
          const picture = d.id === "none" ? h("div", { class: "cover-none" }, "Aa") : coverPreview(d.id, color, v.title ?? "", 64, landscape);
          return h(
            "button",
            {
              type: "button",
              class: "cover-design",
              role: "radio",
              "aria-label": `${d.label} cover`,
              "aria-checked": String(v.design === d.id),
              onclick: () => {
                ctx.set(d.id);
              },
            },
            picture,
            h("span", {}, d.label),
          );
        });
        row.replaceChildren(...tiles);
      };
      let pending = 0;
      ctx.onChange(() => {
        // Coalesce typing in the title into one redraw per frame.
        cancelAnimationFrame(pending);
        pending = requestAnimationFrame(draw);
      });
      void ensureFonts(["sans", "caveat", "kalam"]).then(draw);
      draw();
      return row;
    },
  };
}

/**
 * A large live preview of the notebook being created: its cover (design,
 * colour, title, orientation) and, peeking out behind it, its first paper page.
 */
export function coverPreviewPanel(ctx: { values: Record<string, string>; onChange(fn: () => void): void }): HTMLElement {
  const stage = h("div", { class: "nb-preview-stage" });
  const caption = h("div", { class: "nb-preview-caption" });
  const draw = () => {
    const v = ctx.values;
    const landscape = v.orientation === "landscape";
    const design = (v.design && v.design !== "none" ? v.design : "classic") as CoverDesign;
    const cover = coverPreview(design, v.cover || "#2563eb", v.title ?? "", landscape ? 250 : 200, landscape);
    cover.classList.add("nb-preview-cover");
    stage.replaceChildren(cover);
    caption.textContent = `${COVER_DESIGNS.find((d) => d.id === design)?.label ?? ""} cover`;
  };
  let pending = 0;
  ctx.onChange(() => {
    cancelAnimationFrame(pending);
    pending = requestAnimationFrame(draw);
  });
  void ensureFonts(["sans", "caveat", "kalam"]).then(draw);
  draw();
  return h("div", { class: "nb-preview" }, stage, caption);
}
