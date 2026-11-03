// Just enough of a browser for the web app's drawing and layout code to run
// in the native app's script engine (qt/src/Js.cpp provides `__native`).
/* eslint-disable @typescript-eslint/no-explicit-any */
const g = globalThis as any;
const N = g.__native;

const noop = () => {};
/** A do-nothing DOM element (for code paths the native app never shows). */
function stubElement(): any {
  const el: any = {
    style: {},
    dataset: {},
    classList: { add: noop, remove: noop, toggle: noop, contains: () => false },
    setAttribute: noop,
    getAttribute: () => null,
    removeAttribute: noop,
    addEventListener: noop,
    removeEventListener: noop,
    append: noop,
    appendChild: noop,
    replaceChildren: noop,
    remove: noop,
    querySelector: () => null,
    querySelectorAll: () => [],
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }),
    focus: noop,
    children: [],
  };
  return el;
}

g.window = g;
g.__TAURI_INTERNALS__ = {};
g.self = g;
g.devicePixelRatio = 1;
g.addEventListener = noop;
g.removeEventListener = noop;
g.matchMedia = () => ({ matches: false, addEventListener: noop, removeEventListener: noop });
g.navigator = { userAgent: "Inkleaf-Qt", language: "en" };
g.performance = { now: () => N.now() };
g.requestAnimationFrame = (fn: () => void) => (fn(), 0);
g.cancelAnimationFrame = noop;
// Timers: run by the app (it calls __runTimers while __timersPending()).
const timers = new Map<number, { due: number; fn: () => void }>();
let nextTimer = 1;
g.setTimeout = (fn: () => void, ms = 0) => {
  const id = nextTimer++;
  timers.set(id, { due: N.now() + ms, fn });
  return id;
};
g.clearTimeout = (id: number) => void timers.delete(id);
g.setInterval = () => 0;
g.clearInterval = noop;
g.__timersPending = () => timers.size > 0;
g.__runTimers = () => {
  const now = N.now();
  for (const [id, t] of [...timers]) {
    if (t.due > now) continue;
    timers.delete(id);
    try {
      t.fn();
    } catch (e) {
      N.log(String(e));
    }
  }
};
g.crypto = { randomUUID: () => N.uuid(), getRandomValues: (a: Uint8Array) => (a.forEach((_, i) => (a[i] = Math.floor(Math.random() * 256))), a) };

const store = new Map<string, string>();
g.localStorage = {
  getItem: (k: string) => (store.has(k) ? store.get(k)! : null),
  setItem: (k: string, v: string) => void store.set(k, String(v)),
  removeItem: (k: string) => void store.delete(k),
};

g.document = {
  createElement: (tag: string) => (tag === "canvas" ? N.canvas(300, 150) : stubElement()),
  createElementNS: () => stubElement(),
  addEventListener: noop,
  removeEventListener: noop,
  querySelector: () => null,
  querySelectorAll: () => [],
  body: stubElement(),
  documentElement: stubElement(),
  fonts: { load: () => Promise.resolve([]), check: () => true, ready: Promise.resolve() },
};

/** Pictures decode at once (from data: URLs or the app's assets). */
g.Image = class {
  native: any = null;
  onload: (() => void) | null = null;
  onerror: (() => void) | null = null;
  complete = false;
  naturalWidth = 0;
  naturalHeight = 0;
  set src(v: string) {
    this.native = N.image(v);
    this.complete = true;
    this.naturalWidth = this.native ? this.native.naturalWidth : 0;
    this.naturalHeight = this.native ? this.native.naturalHeight : 0;
    const cb = this.naturalWidth ? this.onload : this.onerror;
    if (cb) Promise.resolve().then(cb);
  }
  addEventListener(type: string, fn: () => void) {
    if (this.complete && (type === "load") === this.naturalWidth > 0) Promise.resolve().then(fn);
  }
};

/** ImageData for getImageData (built by the native canvas). */
g.__imageData = (buffer: ArrayBuffer, width: number, height: number) => ({ width, height, data: new Uint8ClampedArray(buffer) });
