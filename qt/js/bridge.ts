// What the native app uses from the web app's code: the same papers, covers,
// daftar pages, element drawing, text layout, geometry and recognisers, so
// both apps look and behave alike. Built by scripts/qt-bridge.mjs into
// qt/js/bridge.js (global `Inkleaf`).
import "./polyfills";
import "./shims";

export { drawTemplate, isDarkPaper, paperLayout, templateUsesText } from "../../src/editor/papers";
export { drawElement, drawStroke, layoutText, measureText, naturalTextWidth, fitTextToBox, textAnchors, drawGuides, steadyHighlight, flattenSmooth } from "../../src/editor/render";
export * as geometry from "../../src/editor/geometry";
export { beautify, recognizeShape, smoothPath, snapToAxis } from "../../src/editor/beautify";
export { matchSymbol, rankSymbols, symbolShape, SYMBOL_GLYPHS } from "../../src/editor/symbols";
export { COVER_DESIGNS, coverDesignOf, coverTemplate, coverText, isCoverTemplate, drawCoverDesign } from "../../src/editor/covers";
export { coverTextElements, coverPreview, setUpCover, applyCover, refreshCoverThumbnail } from "../../src/cover";
export * as textFormat from "../../src/editor/textFormat";
export * as snap from "../../src/editor/snap";
export * as daftar from "../../src/daftar";
export * as daftarPapers from "../../src/editor/daftarPapers";
export { FONT_PRESETS, fontCss, fontPreset, isRtl } from "../../src/fonts";
export { TEMPLATES, TEMPLATE_CATEGORIES, PAPER_SIZES, PAPER_COLORS } from "../../src/api";
export { PEN_STYLES } from "../../src/editor/model";
export { PALETTES } from "../../src/editor/toolkit";
export { INSTRUMENTS, MM, snapAngle } from "../../src/editor/ruler";
export { createEditor } from "./editorCore";
export { renderPage, drawSelection } from "../../src/editor/render";
export * as samples from "./samples";
