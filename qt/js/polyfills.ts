// Newer built-ins the web code uses that older Qt script engines lack.
/* eslint-disable @typescript-eslint/no-explicit-any */
const A = Array.prototype as any;
if (!A.flat)
  A.flat = function (this: any[], depth = 1): any[] {
    return depth < 1 ? this.slice() : this.reduce((out: any[], v: any) => out.concat(Array.isArray(v) ? v.flat(depth - 1) : [v]), []);
  };
if (!A.flatMap)
  A.flatMap = function (this: any[], fn: (v: any, i: number, a: any[]) => any, self?: any) {
    return this.map(fn, self).flat(1);
  };
if (!A.at)
  A.at = function (this: any[], i: number) {
    const n = Math.trunc(i) || 0;
    return this[n < 0 ? this.length + n : n];
  };
if (!A.findLast)
  A.findLast = function (this: any[], fn: (v: any, i: number, a: any[]) => boolean) {
    for (let i = this.length - 1; i >= 0; i--) if (fn(this[i], i, this)) return this[i];
    return undefined;
  };
if (!A.findLastIndex)
  A.findLastIndex = function (this: any[], fn: (v: any, i: number, a: any[]) => boolean) {
    for (let i = this.length - 1; i >= 0; i--) if (fn(this[i], i, this)) return i;
    return -1;
  };
const S = String.prototype as any;
if (!S.trimStart) S.trimStart = function (this: string) { return this.replace(/^\s+/, ""); };
if (!S.trimEnd) S.trimEnd = function (this: string) { return this.replace(/\s+$/, ""); };
if (!S.at) S.at = function (this: string, i: number) { const n = Math.trunc(i) || 0; return this.charAt(n < 0 ? this.length + n : n) || undefined; };
if (!S.replaceAll)
  S.replaceAll = function (this: string, find: string | RegExp, rep: any) {
    return typeof find === "string" ? this.split(find).join(rep) : this.replace(find, rep);
  };
if (!S.matchAll)
  S.matchAll = function* (this: string, re: RegExp) {
    const r = new RegExp(re.source, re.flags.includes("g") ? re.flags : re.flags + "g");
    let m;
    while ((m = r.exec(this))) {
      yield m;
      if (m[0] === "") r.lastIndex++;
    }
  };
const O = Object as any;
if (!O.fromEntries) O.fromEntries = (entries: Iterable<[string, any]>) => { const o: any = {}; for (const [k, v] of entries) o[k] = v; return o; };
if (!O.hasOwn) O.hasOwn = (o: object, k: string) => Object.prototype.hasOwnProperty.call(o, k);
const G = (function (this: any) { return this; })() as any;
if (!G.structuredClone) G.structuredClone = (v: any) => JSON.parse(JSON.stringify(v));

// Fast array paths for TypeScript's iteration helpers (see scripts/qt-bridge.mjs).
G.__fast = {
  __values(o: any) {
    if (Array.isArray(o)) {
      let i = 0;
      const r = { value: undefined as any, done: false };
      return {
        next() {
          if (i < o.length) r.value = o[i++];
          else {
            r.value = undefined;
            r.done = true;
          }
          return r;
        },
      };
    }
    const s = typeof Symbol === "function" && o[Symbol.iterator];
    if (s) return s.call(o);
    if (o && typeof o.length === "number") {
      let i = 0;
      return { next: () => ({ value: o && o[i++], done: !o || i > o.length }) };
    }
    throw new TypeError(s ? "Object is not iterable." : "Symbol.iterator is not defined.");
  },
  __read(o: any, n?: number) {
    if (Array.isArray(o)) return o;
    const m = typeof Symbol === "function" && o[Symbol.iterator];
    if (!m) return o;
    const it = m.call(o);
    const ar: any[] = [];
    let r;
    while ((n === void 0 || n-- > 0) && !(r = it.next()).done) ar.push(r.value);
    return ar;
  },
  __spreadArray(to: any[], from: any, pack?: boolean) {
    if (Array.isArray(from)) return to.concat(pack ? Array.prototype.slice.call(from) : from);
    return to.concat(Array.prototype.slice.call(from));
  },
};

// Month and year in words (Qt's engine ignores toLocaleDateString's options).
const MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const DP = Date.prototype as any;
const nativeLocaleDate = DP.toLocaleDateString;
DP.toLocaleDateString = function (this: Date, locales?: any, options?: any) {
  if (options && (options.month === "long" || options.month === "short")) {
    const m = MONTHS[this.getMonth()];
    const month = options.month === "short" ? m.slice(0, 3) : m;
    const parts: string[] = [];
    if (options.weekday) parts.push(["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"][this.getDay()]);
    if (options.day) parts.push(String(this.getDate()));
    parts.push(month);
    if (options.year) parts.push(String(this.getFullYear()));
    return parts.join(" ");
  }
  return nativeLocaleDate.call(this, locales, options);
};

// WeakMap and WeakSet without the engine's own: Qt's JavaScript engine can
// free a key object that a WeakMap still points at, and the next lookup
// then reads freed memory and crashes the app. Here the value lives on the
// key itself, in a hidden property, so nothing outlives what it refers to
// (and the entry goes away with the object, as a WeakMap's does).
let weakIds = 0;
class SafeWeakMap<K extends object, V> {
  private readonly slot = `__weak${++weakIds}`;
  // Keys that can't take a property (frozen objects): held strongly.
  private readonly sealed = new Map<K, V>();
  constructor(entries?: Iterable<readonly [K, V]> | null) {
    if (entries) for (const [k, v] of entries as any) this.set(k, v);
  }
  get(k: K): V | undefined {
    if (!k || (typeof k !== "object" && typeof k !== "function")) return undefined;
    const own = Object.prototype.hasOwnProperty.call(k, this.slot);
    return own ? (k as any)[this.slot] : this.sealed.get(k);
  }
  has(k: K): boolean {
    if (!k || (typeof k !== "object" && typeof k !== "function")) return false;
    return Object.prototype.hasOwnProperty.call(k, this.slot) || this.sealed.has(k);
  }
  set(k: K, v: V): this {
    if (!k || (typeof k !== "object" && typeof k !== "function")) throw new TypeError("Invalid value used as weak map key");
    if (Object.prototype.hasOwnProperty.call(k, this.slot)) (k as any)[this.slot] = v;
    else if (Object.isExtensible(k)) Object.defineProperty(k, this.slot, { value: v, writable: true, configurable: true, enumerable: false });
    else this.sealed.set(k, v);
    return this;
  }
  delete(k: K): boolean {
    if (!k || (typeof k !== "object" && typeof k !== "function")) return false;
    if (Object.prototype.hasOwnProperty.call(k, this.slot)) return delete (k as any)[this.slot];
    return this.sealed.delete(k);
  }
}
class SafeWeakSet<T extends object> {
  private readonly map = new SafeWeakMap<T, true>();
  constructor(values?: Iterable<T> | null) {
    if (values) for (const v of values as any) this.add(v);
  }
  add(v: T): this {
    this.map.set(v, true);
    return this;
  }
  has(v: T): boolean {
    return this.map.has(v);
  }
  delete(v: T): boolean {
    return this.map.delete(v);
  }
}
G.WeakMap = SafeWeakMap;
G.WeakSet = SafeWeakSet;
