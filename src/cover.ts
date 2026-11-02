// Cover pages for new notebooks: the title and date are ordinary text boxes
// placed on the cover's label plate (so they can be edited like any text),
// and the cover's thumbnail is drawn straight away so the library shows it.

import { api, type Notebook, type Page } from "./api";
import { ensureFonts } from "./fonts";
import { coverLayout } from "./editor/papers";
import { fromStored, newId, toStored, type TextEl } from "./editor/model";
import { measureText, renderPage } from "./editor/render";

const TITLE_COLOR = "#1f2937";
const DATE_COLOR = "#6b7280";

/** The title and date text boxes for a cover of the given size. */
export function coverText(title: string, date: string, w: number, h: number): TextEl[] {
  const L = coverLayout(w, h);
  const box = L.title;
  // Largest size (34 down to 18) that fits the title on at most two lines.
  let size = 34;
  while (size > 18 && measureText(title, "sans", size) * 1.06 > box.w * (size > 26 ? 1 : 2)) size -= 2;
  const lines = measureText(title, "sans", size) * 1.06 > box.w ? 2 : 1;
  const lineH = size * 1.35;
  const y = Math.max(box.y - 6, box.y + (box.h - lines * lineH) / 2);
  return [
    { id: newId(), z: 0, kind: "text", x: box.x, y, w: box.w, text: title, fontSize: size, color: TITLE_COLOR, font: "sans", bold: true, align: "center" },
    { id: newId(), z: 1, kind: "text", x: L.date.x, y: L.date.y, w: L.date.w, text: date, fontSize: 15, color: DATE_COLOR, font: "sans", align: "center" },
  ];
}

/** Draws and stores a cover page's thumbnail (for the library). */
export async function refreshCoverThumbnail(page: Page): Promise<void> {
  const els = fromStored(await api.elements(page.id));
  await ensureFonts(["sans"]);
  const scale = 420 / page.width; // sharp: the library shows covers full-size
  const thumb = renderPage("cover", page.paperColor, page.width, page.height, els, scale, new Map()).toDataURL("image/jpeg", 0.85);
  await api.setPageThumbnail(page.id, thumb);
}

/** Puts the title and today's month on a new notebook's cover, and draws its thumbnail. */
export async function setUpCover(nb: Notebook): Promise<void> {
  const [page] = await api.pages(nb.id);
  if (page?.template !== "cover") return;
  await ensureFonts(["sans"]);
  const date = new Date().toLocaleDateString(undefined, { month: "long", year: "numeric" });
  await api.saveElements(page.id, coverText(nb.title, date, page.width, page.height).map(toStored));
  await refreshCoverThumbnail(page);
}

/** After the cover colour changes: redraw the cover page's thumbnail. */
export async function recolourCover(notebookId: string): Promise<void> {
  const [page] = await api.pages(notebookId);
  if (page?.template === "cover") await refreshCoverThumbnail(page);
}
