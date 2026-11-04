// Calls into the Rust core from the app's scripts: the Qt app's script engine
// provides `__native` (qt/src/Js.cpp). Command names are camelCase, as
// qt/native expects.
const camel = (s: string) => s.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());

export async function invoke<T>(cmd: string, args?: unknown): Promise<T> {
  const N = (globalThis as { __native?: { call(cmd: string, args: string): string } }).__native;
  if (!N) throw new Error(`no native core (calling ${cmd})`);
  const r = JSON.parse(N.call(camel(cmd), JSON.stringify(args ?? {})));
  if (r.error) throw new Error(r.error);
  return r.ok as T;
}
