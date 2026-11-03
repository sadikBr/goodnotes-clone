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
