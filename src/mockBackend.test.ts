import { beforeEach, describe, expect, it } from "vitest";

// The mock keeps its data in localStorage.
beforeEach(() => {
  const store = new Map<string, string>();
  (globalThis as { localStorage?: unknown }).localStorage = {
    getItem: (k: string) => store.get(k) ?? null,
    setItem: (k: string, v: string) => void store.set(k, v),
    removeItem: (k: string) => void store.delete(k),
  };
});

describe("trash in the browser preview", () => {
  it("a trashed folder hides and then deletes everything inside it", async () => {
    const { MockBackend } = await import("./mockBackend");
    const api = new MockBackend();
    const nb = (title: string, folderId: string | null) => api.createNotebook({ title, folderId, coverColor: "#2563eb", template: "ruled", paperColor: "#ffffff" });
    const cls = await api.createFolder("Class", null);
    const unit = await api.createFolder("Unit 1", cls.id);
    await nb("Lesson A", cls.id);
    await nb("Lesson B", unit.id);
    await nb("Loose", null);

    await api.setFolderTrashed(cls.id, true);
    let lib = await api.library();
    expect(lib.folders).toHaveLength(0);
    expect(lib.notebooks.map((n) => n.title)).toEqual(["Loose"]);
    expect(await api.search("Lesson")).toHaveLength(0);

    await api.setFolderTrashed(cls.id, false);
    expect((await api.library()).notebooks).toHaveLength(3);

    await api.setFolderTrashed(cls.id, true);
    await api.emptyTrash();
    lib = await api.library();
    expect(lib.folders).toHaveLength(0);
    expect(lib.notebooks.map((n) => n.title)).toEqual(["Loose"]);
    expect((await api.trash()).notebooks).toHaveLength(0);
    // Nothing comes back as an orphan.
    expect(JSON.parse(localStorage.getItem("inkleaf-mock-db")!).notebooks).toHaveLength(1);
  });
});
