// @tauri-apps/api/core for the native app: commands go to the Rust core
// through the native bridge (camelCase names, as qt/native expects).
const N = (globalThis as any).__native;
const camel = (s: string) => s.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());

export async function invoke<T>(cmd: string, args?: unknown): Promise<T> {
  const out = N.call(camel(cmd), JSON.stringify(args ?? {}));
  const r = JSON.parse(out);
  if (r.error) throw new Error(r.error);
  return r.ok as T;
}
