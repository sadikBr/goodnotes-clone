// The pages of a daftar annossous (دفتر النصوص), modelled on the printed books
// Moroccan teachers use: title page, the teacher's card, timetable, school
// holidays, diagnostic report, student lists, ministerial memos, absences and
// the lesson pages themselves (date, class, time, activities, signature on
// squared paper, with a notes strip). Arabic first, right to left; table
// headings carry a small French line.
//
// Each page's layout is a function of the page size, used both to draw it and
// to place the text the app fills in (see `daftarSlots`).

import type { Template } from "../api";
import { fontCss, isRtl } from "../fonts";

type Ctx = CanvasRenderingContext2D;

// --- colours ------------------------------------------------------------------

/** Lesson page colours, one per class (like the coloured sections of the printed books). */
export const LESSON_TEMPLATES = ["daftar", "daftar-blue", "daftar-green", "daftar-pink", "daftar-navy", "daftar-orange"] as const satisfies readonly Template[];
const LESSON_ACCENTS: Record<(typeof LESSON_TEMPLATES)[number], string> = {
  daftar: "#5b2a5e",
  "daftar-blue": "#1f6fd1",
  "daftar-green": "#1f7a4a",
  "daftar-pink": "#d1245f",
  "daftar-navy": "#2d2f72",
  "daftar-orange": "#e0612a",
};
const PURPLE = "#5b2a5e";
const GREEN = "#1f7a4a";
const ORANGE = "#e0612a";
const INK = "#1f1d24";
const MUTED = "#6b6475";
const GRID = "rgba(70, 70, 90, 0.22)";

/** The other daftar pages. */
export const DAFTAR_PAGES = [
  "daftar-title",
  "daftar-profile",
  "daftar-profile-2",
  "daftar-timetable",
  "daftar-holidays",
  "daftar-diagnostic",
  "daftar-students",
  "daftar-memos",
  "daftar-absence",
] as const satisfies readonly Template[];

export const isLessonTemplate = (t: string): boolean => (LESSON_TEMPLATES as readonly string[]).includes(t);
export const isDaftarTemplate = (t: string): boolean => isLessonTemplate(t) || (DAFTAR_PAGES as readonly string[]).includes(t);
export const lessonTemplateFor = (classIndex: number): Template => LESSON_TEMPLATES[((classIndex % LESSON_TEMPLATES.length) + LESSON_TEMPLATES.length) % LESSON_TEMPLATES.length];

function accentOf(t: string): string {
  if (isLessonTemplate(t)) return LESSON_ACCENTS[t as (typeof LESSON_TEMPLATES)[number]];
  if (t === "daftar-students") return GREEN;
  if (t === "daftar-memos" || t === "daftar-absence") return ORANGE;
  return PURPLE;
}

/** Mixes a #rrggbb colour with white. */
function tint(hex: string, k: number): string {
  const n = parseInt(hex.slice(1), 16);
  const ch = (v: number) => Math.round(v + (255 - v) * k);
  return `rgb(${ch(n >> 16)}, ${ch((n >> 8) & 255)}, ${ch(n & 255)})`;
}

// --- text ----------------------------------------------------------------------

let measurer: CanvasRenderingContext2D | null = null;
/** Width of a label (estimated where there is no canvas, e.g. in tests). */
export function labelWidth(text: string, size: number, bold = false): number {
  if (typeof document !== "undefined") {
    measurer ??= document.createElement("canvas").getContext("2d");
    if (measurer) {
      measurer.font = fontCss("sans", size, { bold });
      return measurer.measureText(text).width;
    }
  }
  return text.length * size * 0.52;
}

interface TextOpts {
  size: number;
  bold?: boolean;
  color?: string;
  /** Where x is: the text's right edge (default, for Arabic), left edge or centre. */
  anchor?: "right" | "left" | "center";
}

function text(ctx: Ctx, s: string, x: number, y: number, o: TextOpts): number {
  ctx.save();
  ctx.font = fontCss("sans", o.size, { bold: o.bold });
  ctx.fillStyle = o.color ?? INK;
  ctx.textBaseline = "alphabetic";
  ctx.textAlign = "left";
  ctx.direction = isRtl(s) ? "rtl" : "ltr";
  const w = ctx.measureText(s).width;
  const a = o.anchor ?? "right";
  ctx.fillText(s, a === "right" ? x - w : a === "center" ? x - w / 2 : x, y);
  ctx.restore();
  return w;
}

// --- shapes ----------------------------------------------------------------------

function roundRect(ctx: Ctx, x: number, y: number, w: number, h: number, r: number) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

function line(ctx: Ctx, x0: number, y0: number, x1: number, y1: number, color: string, width = 1) {
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo(x0, y0);
  ctx.lineTo(x1, y1);
  ctx.stroke();
}

function dotted(ctx: Ctx, x0: number, x1: number, y: number, color = "rgba(60, 55, 70, 0.55)") {
  ctx.fillStyle = color;
  for (let x = Math.max(x0, x1); x > Math.min(x0, x1); x -= 4) ctx.fillRect(x - 1.1, y, 1.1, 1.1);
}

/** The strip of small diamonds along the top of the printed pages. */
function topStrip(ctx: Ctx, w: number, accent: string) {
  const hgt = 24;
  ctx.fillStyle = tint(accent, 0.82);
  ctx.fillRect(0, 0, w, hgt);
  ctx.fillStyle = tint(accent, 0.62);
  ctx.beginPath();
  for (let x = 8; x < w; x += 16) {
    for (const [cx, cy] of [[x, 6], [x + 8, 18]]) {
      ctx.moveTo(cx, cy - 5);
      ctx.lineTo(cx + 5, cy);
      ctx.lineTo(cx, cy + 5);
      ctx.lineTo(cx - 5, cy);
      ctx.closePath();
    }
  }
  ctx.fill();
}

/** A page heading: Arabic, large and bold, with a small French line under it. */
function heading(ctx: Ctx, ar: string, fr: string, w: number, y: number) {
  text(ctx, ar, w / 2, y, { size: 26, bold: true, anchor: "center" });
  if (fr) text(ctx, fr, w / 2, y + 22, { size: 12.5, color: MUTED, anchor: "center" });
}

/** A rounded band across the page with a white section name (like "المعلومات الشخصية"). */
function sectionBand(ctx: Ctx, label: string, fr: string, x0: number, x1: number, y: number, accent: string) {
  ctx.fillStyle = accent;
  roundRect(ctx, x0, y, x1 - x0, 32, 14);
  ctx.fill();
  const tw = text(ctx, label, (x0 + x1) / 2 + (fr ? 40 : 0), y + 22, { size: 15, bold: true, color: "#ffffff", anchor: "center" });
  if (fr) text(ctx, fr, (x0 + x1) / 2 + 40 - tw / 2 - 12, y + 21, { size: 11.5, color: "rgba(255,255,255,0.82)" });
}

export interface Col {
  ar: string;
  fr?: string;
  /** Share of the table's width. */
  f: number;
}

/** Column edges, right to left (the first column is the rightmost). */
export function columns(cols: Col[], x0: number, x1: number): { x0: number; x1: number }[] {
  const total = cols.reduce((s, c) => s + c.f, 0);
  let right = x1;
  return cols.map((c) => {
    const w = ((x1 - x0) * c.f) / total;
    const out = { x0: right - w, x1: right };
    right -= w;
    return out;
  });
}

interface TableOpts {
  headH?: number;
  stripes?: boolean;
  /** Faint numbers in the first column, starting at this number. */
  numberFrom?: number;
}

/** A table with a coloured header (white labels) and ruled rows. */
function table(ctx: Ctx, cols: Col[], x0: number, x1: number, y0: number, rowH: number, rows: number, accent: string, o: TableOpts = {}) {
  const headH = o.headH ?? 44;
  const edges = columns(cols, x0, x1);
  const bottom = y0 + headH + rows * rowH;
  if (o.stripes) {
    ctx.fillStyle = "rgba(40, 30, 60, 0.06)";
    for (let r = 1; r < rows; r += 2) ctx.fillRect(x0, y0 + headH + r * rowH, x1 - x0, rowH);
  }
  ctx.fillStyle = accent;
  ctx.fillRect(x0, y0, x1 - x0, headH);
  edges.forEach((e, i) => {
    const c = cols[i];
    const cx = (e.x0 + e.x1) / 2;
    const two = !!c.fr && headH >= 38;
    // Long Arabic labels wrap onto two lines, as in the printed headers.
    const words = c.ar.split(" ");
    const fits = labelWidth(c.ar, 12.5, true) < e.x1 - e.x0 - 10 || words.length < 2;
    if (fits) {
      text(ctx, c.ar, cx, y0 + (two ? headH / 2 - 1 : headH / 2 + 5), { size: 12.5, bold: true, color: "#ffffff", anchor: "center" });
    } else {
      const half = Math.ceil(words.length / 2);
      text(ctx, words.slice(0, half).join(" "), cx, y0 + headH / 2 - (two ? 11 : 3), { size: 11.5, bold: true, color: "#ffffff", anchor: "center" });
      text(ctx, words.slice(half).join(" "), cx, y0 + headH / 2 + (two ? 2 : 11), { size: 11.5, bold: true, color: "#ffffff", anchor: "center" });
    }
    if (two) text(ctx, c.fr!, cx, y0 + headH - (fits ? 8 : 5), { size: 9.5, color: "rgba(255,255,255,0.8)", anchor: "center" });
    if (i > 0) line(ctx, e.x1, y0 + headH, e.x1, bottom, accent, 1);
  });
  for (let r = 1; r < rows; r++) line(ctx, x0, y0 + headH + r * rowH, x1, y0 + headH + r * rowH, tint(accent, 0.45), 0.8);
  if (o.numberFrom !== undefined) {
    const e = edges[0];
    for (let r = 0; r < rows; r++) text(ctx, String(o.numberFrom + r), (e.x0 + e.x1) / 2, y0 + headH + r * rowH + rowH / 2 + 4, { size: 10, color: tint(INK, 0.55), anchor: "center" });
  }
  ctx.strokeStyle = accent;
  ctx.lineWidth = 1.4;
  ctx.strokeRect(x0, y0, x1 - x0, bottom - y0);
}

/** "label :" then a dotted line to `xEnd`; returns where its value goes. */
function field(ctx: Ctx, label: string, xRight: number, xEnd: number, y: number, size = 13.5): Slot {
  const lw = text(ctx, `${label} :`, xRight, y, { size, bold: true });
  dotted(ctx, xRight - lw - 6, xEnd, y + 2);
  return { x: xEnd, y: y - size - 3, w: Math.max(30, xRight - lw - 10 - xEnd) };
}

/** Signature lines for the teacher, the head and the inspector. */
function signatures(ctx: Ctx, w: number, y: number, m: number) {
  text(ctx, "السيد(ة) الأستاذ(ة):", w - m, y, { size: 13, bold: true });
  text(ctx, "السيد(ة) المدير(ة):", w / 2 + 50, y + 14, { size: 13, bold: true });
  text(ctx, "السيد(ة) المفتش(ة):", m + 130, y + 28, { size: 13, bold: true });
}

function star8(ctx: Ctx, cx: number, cy: number, r: number, turn: number) {
  ctx.beginPath();
  for (let i = 0; i < 16; i++) {
    const a = turn + (i * Math.PI) / 8;
    const rr = i % 2 ? r * 0.7654 : r;
    const x = cx + Math.cos(a) * rr;
    const y = cy + Math.sin(a) * rr;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
}

// --- layouts ------------------------------------------------------------------------

/** Where a filled-in value goes: a text box (x, top y, width). */
export interface Slot {
  x: number;
  y: number;
  w: number;
}

const M = 48;

// Pages are designed on A4 portrait and scaled to fit other sizes (centred).
const A4 = { w: 794, h: 1123 };
function fit(w: number, h: number) {
  const k = Math.min(w / A4.w, h / A4.h);
  return { k, ox: (w - A4.w * k) / 2, oy: (h - A4.h * k) / 2 };
}

/** Lesson page geometry (page coordinates): columns right to left, the squared body. */
export function lessonLayout(w: number, h: number) {
  const L = lessonBase(A4.w, A4.h);
  const { k, ox, oy } = fit(w, h);
  const X = (x: number) => ox + x * k;
  const Y = (y: number) => oy + y * k;
  return {
    ...L,
    inset: { x0: X(L.inset.x0), y0: Y(L.inset.y0), x1: X(L.inset.x1), y1: Y(L.inset.y1) },
    x0: X(L.x0),
    x1: X(L.x1),
    cols: L.cols.map((c) => ({ ...c, x0: X(c.x0), x1: X(c.x1) })),
    headTop: Y(L.headTop),
    headH: L.headH * k,
    bodyTop: Y(L.bodyTop),
    bodyBottom: Y(L.bodyBottom),
    step: L.step * k,
    fieldsY: Y(L.fieldsY),
  };
}

function lessonBase(w: number, h: number) {
  const inset = { x0: 20, y0: 20, x1: w - 20, y1: h - 20 };
  const x0 = inset.x0 + 14;
  const x1 = inset.x1 - 14;
  const cols: (Col & { key: "date" | "class" | "time" | "activities" | "sign" })[] = [
    { key: "date", ar: "التاريخ", fr: "Date", f: 0.13 },
    { key: "class", ar: "القسم", fr: "Classe", f: 0.11 },
    { key: "time", ar: "التوقيت", fr: "Horaire", f: 0.13 },
    { key: "activities", ar: "طبيعة الأنشطة المنجزة", fr: "Activités réalisées", f: 0.5 },
    { key: "sign", ar: "التوقيع", fr: "Signature", f: 0.13 },
  ];
  const edges = columns(cols, x0, x1);
  const headTop = inset.y0 + 64;
  const headH = 46;
  const bodyTop = headTop + headH;
  /** 5 mm squares. */
  const step = (5 / 25.4) * 96;
  const rows = Math.floor((h - 210 - bodyTop) / step);
  const bodyBottom = bodyTop + rows * step;
  return { inset, x0, x1, cols: cols.map((c, i) => ({ ...c, ...edges[i] })), headTop, headH, bodyTop, bodyBottom, step, rows, fieldsY: inset.y0 + 40 };
}

/** Timetable page geometry (page coordinates): Monday to Saturday right to left, 08:00 to 18:00. */
export function timetableLayout(w: number, h: number) {
  const T = timetableBase(A4.w, A4.h);
  const { k, ox, oy } = fit(w, h);
  return {
    ...T,
    days: T.days.map((d) => ({ ...d, x0: ox + d.x0 * k, x1: ox + d.x1 * k })),
    yOf: (hhmm: string) => oy + T.yOf(hhmm) * k,
  };
}

function timetableBase(w: number, h: number) {
  const top = 150;
  const bottom = h - 130;
  const timeW = 58;
  const right = w - M - timeW;
  const days = [
    { day: 1, ar: "الإثنين", fr: "Lundi" },
    { day: 2, ar: "الثلاثاء", fr: "Mardi" },
    { day: 3, ar: "الأربعاء", fr: "Mercredi" },
    { day: 4, ar: "الخميس", fr: "Jeudi" },
    { day: 5, ar: "الجمعة", fr: "Vendredi" },
    { day: 6, ar: "السبت", fr: "Samedi" },
  ];
  const colW = (right - M) / days.length;
  const headH = 44;
  const bodyTop = top + headH;
  const from = 8;
  const to = 18;
  const yOf = (hhmm: string) => {
    const [hh, mm] = hhmm.split(":").map(Number);
    const t = Math.min(to, Math.max(from, hh + (mm || 0) / 60));
    return bodyTop + ((t - from) / (to - from)) * (bottom - bodyTop);
  };
  return { m: M, top, bottom, timeX0: right, timeX1: w - M, headH, bodyTop, from, to, yOf, days: days.map((d, i) => ({ ...d, x0: right - (i + 1) * colW, x1: right - i * colW })) };
}

const PROFILE_SECTIONS: { ar: string; fr: string; rows: [string, string][] }[] = [
  {
    ar: "المعلومات الشخصية",
    fr: "Informations personnelles",
    rows: [
      ["الإسم الشخصي", "الإسم العائلي"],
      ["تاريخ الازدياد", "مكان الازدياد"],
      ["الإطار التربوي", "الوحدة المدرسية"],
      ["رقم التأجير", "رقم الضمان الاجتماعي"],
      ["أكاديمية التعيين", "الأقسام المسندة"],
      ["المديرية الإقليمية", "الموسم الدراسي"],
      ["رقم بطاقة التعريف الوطنية", ""],
    ],
  },
  {
    ar: "المعلومات الإدارية",
    fr: "Informations administratives",
    rows: [
      ["تاريخ التوظيف", "تاريخ الترسيم"],
      ["الإطار", ""],
      ["الدرجة", "تاريخها"],
      ["الرتبة", "تاريخها"],
    ],
  },
  {
    ar: "المعلومات المهنية",
    fr: "Informations professionnelles",
    rows: [
      ["المؤسسة الحالية", "تاريخ الالتحاق بالمؤسسة"],
      ["تاريخ التعيين بالمديرية", "تاريخ التعيين بالأكاديمية"],
      ["نقطة آخر تفتيش وتاريخها", "ممنوحة من السيد(ة) المفتش(ة)"],
      ["لغة التكوين", "مركز التكوين"],
      ["لغة التدريس", ""],
    ],
  },
];

/** Profile labels the app can fill in, and the setting each one shows. */
const PROFILE_SLOTS: Record<string, string> = {
  "رقم التأجير": "payroll",
  "أكاديمية التعيين": "region",
  "الأقسام المسندة": "classes",
  "المديرية الإقليمية": "directorate",
  "الموسم الدراسي": "year",
  "المؤسسة الحالية": "school",
};

// --- drawing ------------------------------------------------------------------------

/** Draws a daftar page and returns the slots where its values go. */
function drawPage(ctx: Ctx, t: string, w: number, h: number): Record<string, Slot> {
  const accent = accentOf(t);
  const slots: Record<string, Slot> = {};
  const mid = w / 2;
  switch (t) {
    case "daftar-title": {
      ctx.strokeStyle = tint(accent, 0.35);
      ctx.lineWidth = 2;
      ctx.strokeRect(28, 28, w - 56, h - 56);
      ctx.lineWidth = 0.8;
      ctx.strokeRect(34, 34, w - 68, h - 68);
      text(ctx, "بسم الله الرحمن الرحيم", mid, 92, { size: 22, bold: true, anchor: "center" });
      slots.region = field(ctx, "الجهة", w - 70, mid + 30, 150, 14);
      slots.directorate = field(ctx, "المديرية الإقليمية", w - 70, mid + 30, 186, 14);
      text(ctx, "وثائقي التربوية", 70, 170, { size: 18, bold: true, anchor: "left" });
      star8(ctx, mid, 330, 26, Math.PI / 8);
      ctx.fillStyle = tint(accent, 0.75);
      ctx.fill();
      star8(ctx, mid, 330, 13, 0);
      ctx.fillStyle = accent;
      ctx.fill();
      text(ctx, "دفتر النصوص", mid, 470, { size: 82, bold: true, color: accent, anchor: "center" });
      text(ctx, "Cahier de textes", mid, 520, { size: 22, color: MUTED, anchor: "center" });
      text(ctx, "خاص بالتعليم العمومي والخصوصي", mid, 600, { size: 20, bold: true, anchor: "center" });
      slots.school = field(ctx, "المؤسسة", w - 70, mid + 20, 860);
      slots.teacher = field(ctx, "الأستاذ(ة)", mid - 20, 70, 860);
      slots.classes = field(ctx, "القسم", w - 70, mid + 20, 912);
      slots.payroll = field(ctx, "رقم التأجير", mid - 20, 70, 912);
      slots.year = field(ctx, "الموسم الدراسي", mid + 150, mid - 150, 970);
      break;
    }
    case "daftar-profile": {
      heading(ctx, "بطاقة المعلومات الشخصية للأستاذ(ة)", "Fiche de renseignements de l'enseignant(e)", w, 66);
      ctx.strokeStyle = INK;
      ctx.lineWidth = 1.3;
      ctx.strokeRect(mid - 52, 110, 104, 124);
      text(ctx, "صورة", mid, 178, { size: 14, bold: true, anchor: "center" });
      let y = 262;
      for (const s of PROFILE_SECTIONS) {
        sectionBand(ctx, s.ar, s.fr, M, w - M, y, accent);
        y += 66;
        for (const [right, left] of s.rows) {
          const a = field(ctx, right, w - M, mid + 14, y, 13);
          if (PROFILE_SLOTS[right]) slots[PROFILE_SLOTS[right]] = a;
          if (left) {
            const b = field(ctx, left, mid - 14, M, y, 13);
            if (PROFILE_SLOTS[left]) slots[PROFILE_SLOTS[left]] = b;
          }
          y += 34;
        }
        y += 10;
      }
      break;
    }
    case "daftar-profile-2": {
      heading(ctx, "بطاقة المعلومات الشخصية للأستاذ(ة)", "Fiche de renseignements (suite)", w, 66);
      sectionBand(ctx, "الشواهد الدراسية والمؤهلات المهنية", "Diplômes et qualifications", M, w - M, 112, accent);
      table(ctx, [{ ar: "الرقم الترتيبي", fr: "N°", f: 0.12 }, { ar: "الشهادة", fr: "Diplôme", f: 0.56 }, { ar: "تاريخها", fr: "Date", f: 0.32 }], M, w - M, 158, 32, 9, accent, { stripes: true, numberFrom: 1 });
      sectionBand(ctx, "الخدمات السابقة", "Services antérieurs", M, w - M, 528, accent);
      table(
        ctx,
        [
          { ar: "الرقم الترتيبي", fr: "N°", f: 0.1 },
          { ar: "المؤسسات السابقة", fr: "Établissements", f: 0.24 },
          { ar: "المديرية", fr: "Direction", f: 0.15 },
          { ar: "الأكاديمية", fr: "Académie", f: 0.16 },
          { ar: "من ... إلى", fr: "Du … au", f: 0.2 },
          { ar: "تعيين أو تكليف", fr: "Affectation", f: 0.15 },
        ],
        M,
        w - M,
        574,
        32,
        13,
        accent,
        { stripes: true, numberFrom: 1 },
      );
      break;
    }
    case "daftar-timetable": {
      topStrip(ctx, w, accent);
      heading(ctx, "استعمال الزمن", "Emploi du temps", w, 86);
      const L = timetableBase(w, h);
      ctx.fillStyle = accent;
      ctx.fillRect(L.m, L.top, w - 2 * L.m, L.headH);
      for (const d of L.days) {
        const cx = (d.x0 + d.x1) / 2;
        text(ctx, d.ar, cx, L.top + 20, { size: 13, bold: true, color: "#fff", anchor: "center" });
        text(ctx, d.fr, cx, L.top + 36, { size: 9.5, color: "rgba(255,255,255,0.8)", anchor: "center" });
        if (d.x0 > L.m + 1) line(ctx, d.x0, L.bodyTop, d.x0, L.bottom, tint(accent, 0.45), 1);
      }
      for (let hr = L.from; hr <= L.to; hr++) {
        const y = L.yOf(`${hr}:00`);
        if (hr > L.from && hr < L.to) line(ctx, L.m, y, w - L.m, y, tint(accent, 0.55), 0.8);
        if (hr < L.to) line(ctx, L.m, L.yOf(`${hr}:30`), L.timeX0, L.yOf(`${hr}:30`), tint(accent, 0.85), 0.6);
        text(ctx, `${String(hr).padStart(2, "0")}:00`, (L.timeX0 + L.timeX1) / 2, Math.min(L.bottom - 4, y + 14), { size: 11, color: MUTED, anchor: "center" });
      }
      line(ctx, L.timeX0, L.top, L.timeX0, L.bottom, accent, 1.2);
      ctx.strokeStyle = accent;
      ctx.lineWidth = 1.4;
      ctx.strokeRect(L.m, L.top, w - 2 * L.m, L.bottom - L.top);
      signatures(ctx, w, h - 92, M);
      break;
    }
    case "daftar-holidays": {
      topStrip(ctx, w, accent);
      heading(ctx, "لائحة العطل المدرسية", "Vacances scolaires", w, 86);
      const top = 140;
      const rows = 14;
      const rowH = (h - 150 - top - 46) / rows;
      table(
        ctx,
        [
          { ar: "الرقم الترتيبي", fr: "N°", f: 0.1 },
          { ar: "العطلة", fr: "Vacances", f: 0.34 },
          { ar: "من", fr: "Du", f: 0.16 },
          { ar: "إلى", fr: "Au", f: 0.16 },
          { ar: "المدة", fr: "Durée", f: 0.1 },
          { ar: "ملاحظات", fr: "Remarques", f: 0.14 },
        ],
        M,
        w - M,
        top,
        rowH,
        rows,
        accent,
        { headH: 46, numberFrom: 1 },
      );
      signatures(ctx, w, h - 92, M);
      break;
    }
    case "daftar-diagnostic": {
      topStrip(ctx, w, accent);
      heading(ctx, "تقرير التقويم التشخيصي", "Rapport de l'évaluation diagnostique", w, 86);
      slots.region = field(ctx, "الأكاديمية", w - M, mid + 20, 160, 13);
      slots.directorate = field(ctx, "المديرية الإقليمية", w - M, mid + 20, 192, 13);
      slots.school = field(ctx, "المؤسسة", w - M, mid + 20, 224, 13);
      slots.teacher = field(ctx, "الأستاذ(ة)", mid - 20, M, 160, 13);
      slots.year = field(ctx, "الموسم الدراسي", mid - 20, M, 192, 13);
      slots.level = field(ctx, "المستوى", mid - 20, M, 224, 13);
      for (let y = 272, i = 0; y < h - 120; y += 15, i++) line(ctx, M, y, w - M, y, i % 2 ? "rgba(40,40,60,0.18)" : "rgba(40,40,60,0.45)", i % 2 ? 0.6 : 0.9);
      signatures(ctx, w, h - 80, M);
      break;
    }
    case "daftar-students": {
      topStrip(ctx, w, accent);
      heading(ctx, "لائحة أسماء المتعلمين والمتعلمات", "Liste des élèves", w, 86);
      const top = 140;
      const gap = 18;
      const tw = (w - 2 * M - gap) / 2;
      const rows = 30;
      const rowH = (h - 50 - top - 72) / rows;
      [w - M, w - M - tw - gap].forEach((right, k) => {
        const x0 = right - tw;
        // "الفوج / المستوى" strip above the column headings.
        ctx.fillStyle = accent;
        ctx.fillRect(x0, top, tw, 28);
        // Label, then a white box for its value to the left of it.
        const labelled = (label: string, xr: number, boxW: number, key: string) => {
          const lw = text(ctx, label, xr, top + 19, { size: 12, bold: true, color: "#fff" });
          ctx.fillStyle = "#ffffff";
          ctx.fillRect(xr - lw - 8 - boxW, top + 5, boxW, 18);
          slots[key] = { x: xr - lw - 8 - boxW, y: top + 4, w: boxW };
        };
        labelled("الفوج", right - 10, 64, `group${k}`);
        labelled("المستوى", right - tw * 0.5, 70, `level${k}`);
        table(ctx, [{ ar: "الرقم الترتيبي", f: 0.16 }, { ar: "الرقم الوطني", f: 0.28 }, { ar: "أسماء المتعلمين والمتعلمات", f: 0.56 }], x0, right, top + 28, rowH, rows, accent, { headH: 44, numberFrom: 1 + k * rows });
      });
      break;
    }
    case "daftar-memos": {
      topStrip(ctx, w, accent);
      const cols: Col[] = [
        { ar: "الرقم الترتيبي", fr: "N°", f: 0.12 },
        { ar: "رقم المذكرة", fr: "Réf.", f: 0.13 },
        { ar: "تاريخها", fr: "Date", f: 0.17 },
        { ar: "مصدرها", fr: "Source", f: 0.19 },
        { ar: "موضوعها", fr: "Objet", f: 0.39 },
      ];
      heading(ctx, "المذكرات الوزارية", "Notes ministérielles", w, 78);
      table(ctx, cols, M, w - M, 112, 40, 9, accent, { headH: 48, numberFrom: 1 });
      heading(ctx, "المذكرات الأكاديمية والإقليمية", "Notes académiques et provinciales", w, 586);
      table(ctx, cols, M, w - M, 620, 40, 9, accent, { headH: 48, numberFrom: 10 });
      break;
    }
    case "daftar-absence": {
      topStrip(ctx, w, accent);
      heading(ctx, "غياب الأستاذ(ة): الرخص الطبية والشخصية", "Absences : congés de maladie et personnels", w, 86);
      table(
        ctx,
        [
          { ar: "الرقم الترتيبي", fr: "N°", f: 0.1 },
          { ar: "نوع الغياب", fr: "Absence", f: 0.14 },
          { ar: "نوع الرخصة", fr: "Congé", f: 0.22 },
          { ar: "المدة", fr: "Durée", f: 0.18 },
          { ar: "من ......... إلى", fr: "Du … au", f: 0.36 },
        ],
        M,
        w - M,
        140,
        46,
        11,
        accent,
        { headH: 48, numberFrom: 1 },
      );
      text(ctx, "ملاحظات", mid, 760, { size: 24, bold: true, anchor: "center" });
      for (let y = 796; y < h - 50; y += 24) line(ctx, M, y, w - M, y, "rgba(40,40,60,0.3)", 0.8);
      break;
    }
    default: {
      if (!isLessonTemplate(t)) break;
      const L = lessonBase(w, h);
      // Coloured frame around the page, like the printed books' edges.
      ctx.fillStyle = accent;
      ctx.fillRect(0, 0, w, h);
      ctx.fillStyle = "#ffffff";
      roundRect(ctx, L.inset.x0, L.inset.y0, L.inset.x1 - L.inset.x0, L.inset.y1 - L.inset.y0, 10);
      ctx.fill();
      // المادة / القسم.
      slots.subject = field(ctx, "المادة", L.x1, mid + 60, L.fieldsY, 13.5);
      slots.class = field(ctx, "القسم", mid + 20, L.x0 + 10, L.fieldsY, 13.5);
      // Squared paper in every column but the signature.
      const sign = L.cols.find((c) => c.key === "sign")!;
      ctx.strokeStyle = GRID;
      ctx.lineWidth = 0.6;
      ctx.beginPath();
      for (let y = L.bodyTop + L.step; y < L.bodyBottom - 0.5; y += L.step) {
        ctx.moveTo(sign.x1, y);
        ctx.lineTo(L.x1, y);
      }
      for (let x = L.x1 - L.step; x > sign.x1 + 0.5; x -= L.step) {
        ctx.moveTo(x, L.bodyTop);
        ctx.lineTo(x, L.bodyBottom);
      }
      ctx.stroke();
      // Header band and columns.
      ctx.fillStyle = accent;
      ctx.fillRect(L.x0, L.headTop, L.x1 - L.x0, L.headH);
      for (const c of L.cols) {
        const cx = (c.x0 + c.x1) / 2;
        const big = c.key === "activities";
        text(ctx, c.ar, cx, L.headTop + (big ? 26 : 23), { size: big ? 17 : 13, bold: true, color: "#ffffff", anchor: "center" });
        text(ctx, c.fr!, cx, L.headTop + 40, { size: 9.5, color: "rgba(255,255,255,0.8)", anchor: "center" });
        if (c.x0 > L.x0 + 1) line(ctx, c.x0, L.headTop, c.x0, L.bodyBottom, accent, 1.1);
      }
      ctx.strokeStyle = accent;
      ctx.lineWidth = 1.4;
      ctx.strokeRect(L.x0, L.headTop, L.x1 - L.x0, L.bodyBottom - L.headTop);
      // The notes strip: a slanted tab, then ruled lines.
      const ty = L.bodyBottom + 20;
      ctx.fillStyle = accent;
      ctx.beginPath();
      ctx.moveTo(L.x1, ty);
      ctx.lineTo(L.x1 - 190, ty);
      ctx.lineTo(L.x1 - 206, ty + 28);
      ctx.lineTo(L.x1, ty + 28);
      ctx.closePath();
      ctx.fill();
      text(ctx, "ملاحظات", L.x1 - 24, ty + 20, { size: 14, bold: true, color: "#ffffff" });
      text(ctx, "Remarques", L.x1 - 108, ty + 19, { size: 10.5, color: "rgba(255,255,255,0.8)" });
      line(ctx, L.x0, ty + 28, L.x1 - 206, ty + 28, accent, 1.2);
      for (let y = ty + 54; y < L.inset.y1 - 10; y += 22) line(ctx, L.x0, y, L.x1, y, "rgba(40,40,60,0.28)", 0.7);
      break;
    }
  }
  return slots;
}

/** Draws a daftar page (paper first), scaled from A4 to the page. */
export function drawDaftarPage(ctx: Ctx, template: string, w: number, h: number, paper: string) {
  ctx.fillStyle = paper;
  ctx.fillRect(0, 0, w, h);
  const { k, ox, oy } = fit(w, h);
  ctx.save();
  ctx.translate(ox, oy);
  ctx.scale(k, k);
  drawPage(ctx, template, A4.w, A4.h);
  ctx.restore();
}

/** A drawing context that draws nothing, to work out a page's slots. */
function nullCtx(): Ctx {
  const noop = () => {};
  const handler: ProxyHandler<object> = {
    get: (_t, k) => (k === "measureText" ? (s: string) => ({ width: labelWidth(s, sizeOf(current), /\bbold\b/.test(current)) }) : noop),
    set: (_t, k, v) => {
      if (k === "font") current = String(v);
      return true;
    },
  };
  let current = "13px sans-serif";
  const sizeOf = (font: string) => Number(/(\d+(?:\.\d+)?)px/.exec(font)?.[1] ?? 13);
  return new Proxy({}, handler) as Ctx;
}

/** Where a daftar page's filled-in values go (keys like "school", "class", "year"). */
export function daftarSlots(template: string, w: number, h: number): Record<string, Slot> {
  const { k, ox, oy } = fit(w, h);
  const out: Record<string, Slot> = {};
  for (const [key, s] of Object.entries(drawPage(nullCtx(), template, A4.w, A4.h))) out[key] = { x: ox + s.x * k, y: oy + s.y * k, w: s.w * k };
  return out;
}
