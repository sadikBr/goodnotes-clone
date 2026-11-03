//! Sync between devices through the user's Google Drive.
//!
//! Everything lives in Drive's hidden app folder (`appDataFolder`, which only
//! Inkleaf can see): one JSON file per notebook (`nb-<id>.json`: its row, pages
//! and elements), each imported PDF once (`asset-<id>.bin`, never changed),
//! and the folder list (`folders.json`).
//!
//! Each sync compares, per notebook, "changed here since the last sync"
//! (recorded by database triggers, see db.rs v4) with "changed in Drive"
//! (Drive's file version differs from the one last seen):
//!   - only here: upload; only in Drive: download and replace;
//!   - both: keep both — Drive's version comes in as a copy, ours goes up;
//!   - deleted for good here (tombstone): deleted in Drive; gone from Drive
//!     after being synced: deleted here (unless changed here meanwhile).
//!
//! The notebook open in the editor is never replaced underneath it.
//!
//! Sign-in is Google's flow for installed apps: the browser opens Google's
//! page and comes back to a one-off listener on 127.0.0.1 (with PKCE), the
//! same on desktop and Android. It needs a "Desktop app" OAuth client.

use base64::Engine as _;
use rusqlite::types::{Value as SqlValue, ValueRef};
use rusqlite::{params, params_from_iter, Connection, OptionalExtension, ToSql};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::{HashMap, HashSet};
use sha2::{Digest, Sha256};
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use crate::db::{new_id, now_ms, Db};
use crate::error::{AppError, AppResult};

pub const FOLDERS_ITEM: &str = "__folders__";
const FOLDERS_NAME: &str = "folders.json";
const FORMAT: u32 = 1;

fn nb_name(id: &str) -> String {
    format!("nb-{id}.json")
}
fn asset_name(id: &str) -> String {
    format!("asset-{id}.bin")
}

fn invalid(e: impl std::fmt::Display) -> AppError {
    AppError::Invalid(e.to_string())
}

// --- the remote side ---------------------------------------------------------

#[derive(Debug, Clone)]
pub struct RemoteFile {
    pub id: String,
    pub name: String,
    /// Changes whenever the file's content does.
    pub version: String,
    pub props: HashMap<String, String>,
}

/// Where synced files live (Google Drive; an in-memory store in tests).
pub trait RemoteStore {
    fn list(&mut self) -> AppResult<Vec<RemoteFile>>;
    fn get(&mut self, id: &str) -> AppResult<Vec<u8>>;
    /// Creates (`id` None) or replaces a file; returns its id and new version.
    fn put(&mut self, id: Option<&str>, name: &str, props: &HashMap<String, String>, bytes: &[u8]) -> AppResult<(String, String)>;
    fn delete(&mut self, id: &str) -> AppResult<()>;
}

#[derive(Debug, Default, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", default)]
pub struct SyncReport {
    pub uploaded: u32,
    pub downloaded: u32,
    pub deleted: u32,
    /// Titles of notebooks changed on both sides (Drive's version was kept as a copy).
    pub conflicts: Vec<String>,
    /// Titles of notebooks changed in Drive but open here (applied next time).
    pub deferred: Vec<String>,
}

// --- rows as JSON ------------------------------------------------------------

fn json_rows(conn: &Connection, sql: &str, args: &[&dyn ToSql]) -> AppResult<Vec<Map<String, Value>>> {
    let mut stmt = conn.prepare(sql)?;
    let names: Vec<String> = stmt.column_names().iter().map(|s| s.to_string()).collect();
    let rows = stmt.query_map(args, |r| {
        let mut m = Map::new();
        for (i, n) in names.iter().enumerate() {
            let v = match r.get_ref(i)? {
                ValueRef::Null => Value::Null,
                ValueRef::Integer(x) => Value::from(x),
                ValueRef::Real(f) => serde_json::Number::from_f64(f).map(Value::Number).unwrap_or(Value::Null),
                ValueRef::Text(t) => Value::String(String::from_utf8_lossy(t).into_owned()),
                ValueRef::Blob(b) => Value::String(base64::engine::general_purpose::STANDARD.encode(b)),
            };
            m.insert(n.clone(), v);
        }
        Ok(m)
    })?;
    Ok(rows.collect::<Result<_, _>>()?)
}

/// The table's columns here (rows from another version may have more or fewer).
fn columns(conn: &Connection, table: &str) -> AppResult<HashSet<String>> {
    let mut stmt = conn.prepare(&format!("PRAGMA table_info({table})"))?;
    let cols = stmt.query_map([], |r| r.get::<_, String>(1))?.collect::<Result<_, _>>()?;
    Ok(cols)
}

fn sql_value(v: &Value) -> SqlValue {
    match v {
        Value::Null => SqlValue::Null,
        Value::Bool(b) => SqlValue::Integer(*b as i64),
        Value::Number(n) => n.as_i64().map(SqlValue::Integer).unwrap_or_else(|| SqlValue::Real(n.as_f64().unwrap_or(0.0))),
        Value::String(s) => SqlValue::Text(s.clone()),
        other => SqlValue::Text(other.to_string()),
    }
}

/// Inserts a row, or updates it in place when its id exists (never
/// REPLACE, which would delete it and cascade to its pages).
fn upsert(conn: &Connection, table: &str, row: &Map<String, Value>, cols: &HashSet<String>) -> AppResult<()> {
    let id = row.get("id").and_then(Value::as_str).ok_or_else(|| invalid("synced row without an id"))?;
    let keys: Vec<&String> = row.keys().filter(|k| cols.contains(*k)).collect();
    let values: Vec<SqlValue> = keys.iter().map(|k| sql_value(&row[k.as_str()])).collect();
    let exists = conn.query_row(&format!("SELECT 1 FROM {table} WHERE id = ?1"), [id], |_| Ok(())).optional()?.is_some();
    let sql = if exists {
        let set: Vec<String> = keys.iter().enumerate().map(|(i, k)| format!("{k} = ?{}", i + 1)).collect();
        format!("UPDATE {table} SET {} WHERE id = ?{}", set.join(", "), keys.len() + 1)
    } else {
        let marks: Vec<String> = (1..=keys.len()).map(|i| format!("?{i}")).collect();
        format!("INSERT INTO {table} ({}) VALUES ({})", keys.iter().map(|k| k.as_str()).collect::<Vec<_>>().join(", "), marks.join(", "))
    };
    let mut all = values;
    if exists {
        all.push(SqlValue::Text(id.to_string()));
    }
    conn.execute(&sql, params_from_iter(all))?;
    Ok(())
}

// --- notebook files ----------------------------------------------------------

#[derive(Debug, Serialize, Deserialize)]
struct NotebookFile {
    format: u32,
    notebook: Map<String, Value>,
    pages: Vec<Map<String, Value>>,
    elements: Vec<Map<String, Value>>,
    /// Imported PDFs: id, kind, created_at (the bytes are separate files).
    assets: Vec<Map<String, Value>>,
}

fn dump_notebook(conn: &Connection, id: &str) -> AppResult<Option<NotebookFile>> {
    let Some(notebook) = json_rows(conn, "SELECT * FROM notebooks WHERE id = ?1", &[&id])?.into_iter().next() else {
        return Ok(None);
    };
    Ok(Some(NotebookFile {
        format: FORMAT,
        notebook,
        pages: json_rows(conn, "SELECT * FROM pages WHERE notebook_id = ?1 ORDER BY position", &[&id])?,
        elements: json_rows(conn, "SELECT e.* FROM elements e JOIN pages p ON p.id = e.page_id WHERE p.notebook_id = ?1", &[&id])?,
        assets: json_rows(conn, "SELECT id, kind, created_at FROM assets WHERE notebook_id = ?1", &[&id])?,
    }))
}

fn str_of<'a>(m: &'a Map<String, Value>, k: &str) -> Option<&'a str> {
    m.get(k).and_then(Value::as_str)
}

/// Writes a notebook from Drive into the library (replacing its pages if it
/// exists), without marking it as changed here.
fn import_notebook(conn: &mut Connection, file: &NotebookFile, assets: &HashMap<String, Vec<u8>>) -> AppResult<()> {
    let id = str_of(&file.notebook, "id").ok_or_else(|| invalid("synced notebook without an id"))?.to_string();
    let tx = conn.transaction()?;
    let mut nb = file.notebook.clone();
    // Its folder may not exist here (yet): keep it at the top level then.
    if let Some(folder) = str_of(&nb, "folder_id").map(str::to_string) {
        let exists = tx.query_row("SELECT 1 FROM folders WHERE id = ?1", [&folder], |_| Ok(())).optional()?.is_some();
        if !exists {
            nb.insert("folder_id".into(), Value::Null);
        }
    }
    upsert(&tx, "notebooks", &nb, &columns(&tx, "notebooks")?)?;
    tx.execute("DELETE FROM pages WHERE notebook_id = ?1", [&id])?;
    let page_cols = columns(&tx, "pages")?;
    for p in &file.pages {
        upsert(&tx, "pages", p, &page_cols)?;
    }
    let el_cols = columns(&tx, "elements")?;
    for e in &file.elements {
        upsert(&tx, "elements", e, &el_cols)?;
    }
    for a in &file.assets {
        let Some(aid) = str_of(a, "id") else { continue };
        let Some(bytes) = assets.get(aid) else { continue };
        tx.execute(
            "INSERT OR IGNORE INTO assets (id, notebook_id, kind, data, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
            params![aid, id, str_of(a, "kind").unwrap_or("pdf"), bytes, a.get("created_at").and_then(Value::as_i64).unwrap_or_else(now_ms)],
        )?;
    }
    tx.execute("DELETE FROM sync_changes WHERE notebook_id = ?1", [&id])?;
    tx.commit()?;
    Ok(())
}

/// The same notebook under new ids and a new title (for a conflict copy).
fn as_copy(file: &NotebookFile, title_suffix: &str) -> NotebookFile {
    let nb_id = new_id();
    let mut notebook = file.notebook.clone();
    notebook.insert("id".into(), Value::String(nb_id.clone()));
    let title = str_of(&file.notebook, "title").unwrap_or("Notebook");
    notebook.insert("title".into(), Value::String(format!("{title} ({title_suffix})")));
    let asset_ids: HashMap<String, String> = file.assets.iter().filter_map(|a| str_of(a, "id")).map(|a| (a.to_string(), new_id())).collect();
    let mut page_ids = HashMap::new();
    let pages = file
        .pages
        .iter()
        .map(|p| {
            let mut p = p.clone();
            let new = new_id();
            if let Some(old) = str_of(&p, "id") {
                page_ids.insert(old.to_string(), new.clone());
            }
            p.insert("id".into(), Value::String(new));
            p.insert("notebook_id".into(), Value::String(nb_id.clone()));
            if let Some(a) = str_of(&p, "background_asset").and_then(|a| asset_ids.get(a)).cloned() {
                p.insert("background_asset".into(), Value::String(a));
            }
            p
        })
        .collect();
    let elements = file
        .elements
        .iter()
        .filter_map(|e| {
            let mut e = e.clone();
            let page = page_ids.get(str_of(&e, "page_id")?)?.clone();
            e.insert("id".into(), Value::String(new_id()));
            e.insert("page_id".into(), Value::String(page));
            Some(e)
        })
        .collect();
    let assets = file
        .assets
        .iter()
        .map(|a| {
            let mut a = a.clone();
            if let Some(n) = str_of(&a, "id").and_then(|x| asset_ids.get(x)).cloned() {
                a.insert("id".into(), Value::String(n));
            }
            a
        })
        .collect();
    NotebookFile { format: FORMAT, notebook, pages, elements, assets }
}

// --- folders -------------------------------------------------------------------

#[derive(Debug, Default, Serialize, Deserialize)]
struct FoldersFile {
    folders: Vec<Map<String, Value>>,
    /// Folders deleted for good on some device.
    deleted: Vec<String>,
}

fn updated(m: &Map<String, Value>) -> i64 {
    m.get("updated_at").and_then(Value::as_i64).unwrap_or(0)
}

// --- the sync itself -----------------------------------------------------------

struct Engine<'a, S: RemoteStore> {
    db: &'a Db,
    store: &'a mut S,
    device: String,
    files: HashMap<String, RemoteFile>,
    report: SyncReport,
}

fn state_of(db: &Db, item: &str) -> AppResult<Option<(String, String)>> {
    db.with_conn(|c| Ok(c.query_row("SELECT file_id, version FROM sync_state WHERE item = ?1", [item], |r| Ok((r.get(0)?, r.get(1)?))).optional()?))
}

fn set_state(db: &Db, item: &str, file_id: &str, version: &str) -> AppResult<()> {
    db.with_conn(|c| {
        c.execute("INSERT OR REPLACE INTO sync_state (item, file_id, version) VALUES (?1, ?2, ?3)", params![item, file_id, version])?;
        Ok(())
    })
}

impl<S: RemoteStore> Engine<'_, S> {
    fn props(&self, kind: &str) -> HashMap<String, String> {
        HashMap::from([("kind".to_string(), kind.to_string()), ("device".to_string(), self.device.clone())])
    }

    fn sync_folders(&mut self) -> AppResult<()> {
        let remote_file = self.files.get(FOLDERS_NAME).cloned();
        let state = state_of(self.db, FOLDERS_ITEM)?;
        let remote_changed = remote_file.as_ref().is_some_and(|f| state.as_ref().is_none_or(|s| s.1 != f.version));
        let dirty = self.db.with_conn(|c| Ok(c.query_row("SELECT 1 FROM sync_changes WHERE notebook_id = ?1", [FOLDERS_ITEM], |_| Ok(())).optional()?.is_some()))?;
        if !remote_changed && !dirty && remote_file.is_some() {
            return Ok(());
        }
        let remote: FoldersFile = match &remote_file {
            Some(f) => serde_json::from_slice(&self.store.get(&f.id)?).map_err(invalid)?,
            None => FoldersFile::default(),
        };
        let deleted_here: HashSet<String> = self.db.with_conn(|c| {
            let mut s = c.prepare("SELECT id FROM sync_tombstones WHERE kind = 'folder'")?;
            let ids = s.query_map([], |r| r.get(0))?.collect::<Result<_, _>>()?;
            Ok(ids)
        })?;
        let gone: HashSet<String> = remote.deleted.iter().cloned().chain(deleted_here.iter().cloned()).collect();
        let merged = self.db.with_conn(|conn| {
            let tx = conn.transaction()?;
            tx.execute_batch("PRAGMA defer_foreign_keys = ON;")?;
            let mut rows: HashMap<String, Map<String, Value>> = json_rows(&tx, "SELECT * FROM folders", &[])?
                .into_iter()
                .filter_map(|r| Some((str_of(&r, "id")?.to_string(), r)))
                .collect();
            for r in &remote.folders {
                let Some(id) = str_of(r, "id") else { continue };
                if gone.contains(id) {
                    continue;
                }
                let newer = rows.get(id).is_none_or(|l| updated(r) > updated(l));
                if newer {
                    rows.insert(id.to_string(), r.clone());
                }
            }
            // Deleted elsewhere: its notebooks and subfolders move to the top
            // level (their own deletions, if any, come as notebook tombstones).
            for id in &remote.deleted {
                if rows.remove(id).is_some() {
                    tx.execute("UPDATE notebooks SET folder_id = NULL WHERE folder_id = ?1", [id])?;
                    tx.execute("UPDATE folders SET parent_id = NULL WHERE parent_id = ?1", [id])?;
                    tx.execute("DELETE FROM folders WHERE id = ?1", [id])?;
                }
            }
            let cols = columns(&tx, "folders")?;
            for r in rows.values() {
                let mut r = r.clone();
                if str_of(&r, "parent_id").is_some_and(|p| !rows.contains_key(p)) {
                    r.insert("parent_id".into(), Value::Null);
                }
                upsert(&tx, "folders", &r, &cols)?;
            }
            tx.execute("DELETE FROM sync_changes WHERE notebook_id = ?1", [FOLDERS_ITEM])?;
            tx.execute("DELETE FROM sync_tombstones WHERE kind = 'folder'", [])?;
            tx.commit()?;
            json_rows(conn, "SELECT * FROM folders", &[])
        })?;
        let mut deleted: Vec<String> = gone.into_iter().collect();
        deleted.sort();
        let file = FoldersFile { folders: merged, deleted };
        let bytes = serde_json::to_vec(&file).map_err(invalid)?;
        let (fid, ver) = self.store.put(remote_file.as_ref().map(|f| f.id.as_str()), FOLDERS_NAME, &self.props("folders"), &bytes)?;
        set_state(self.db, FOLDERS_ITEM, &fid, &ver)?;
        Ok(())
    }

    fn upload(&mut self, id: &str) -> AppResult<()> {
        // Taken and marked clean together: edits made during the upload mark it again.
        let dumped = self.db.with_conn(|c| {
            let f = dump_notebook(c, id)?;
            c.execute("DELETE FROM sync_changes WHERE notebook_id = ?1", [id])?;
            Ok(f)
        })?;
        let Some(file) = dumped else { return Ok(()) };
        let result = (|| -> AppResult<()> {
            for a in &file.assets {
                let Some(aid) = str_of(a, "id") else { continue };
                if self.files.contains_key(&asset_name(aid)) {
                    continue;
                }
                let bytes: Vec<u8> = self.db.with_conn(|c| Ok(c.query_row("SELECT data FROM assets WHERE id = ?1", [aid], |r| r.get(0))?))?;
                let (fid, ver) = self.store.put(None, &asset_name(aid), &self.props("asset"), &bytes)?;
                self.files.insert(asset_name(aid), RemoteFile { id: fid, name: asset_name(aid), version: ver, props: HashMap::new() });
            }
            let bytes = serde_json::to_vec(&file).map_err(invalid)?;
            let existing = self.files.get(&nb_name(id)).map(|f| f.id.clone());
            let (fid, ver) = self.store.put(existing.as_deref(), &nb_name(id), &self.props("notebook"), &bytes)?;
            set_state(self.db, id, &fid, &ver)?;
            self.files.insert(nb_name(id), RemoteFile { id: fid, name: nb_name(id), version: ver, props: self.props("notebook") });
            Ok(())
        })();
        if result.is_err() {
            let _ = self.db.with_conn(|c| Ok(c.execute("INSERT OR IGNORE INTO sync_changes VALUES (?1)", [id])?));
        }
        result?;
        self.report.uploaded += 1;
        Ok(())
    }

    fn fetch(&mut self, f: &RemoteFile) -> AppResult<(NotebookFile, HashMap<String, Vec<u8>>)> {
        let file: NotebookFile = serde_json::from_slice(&self.store.get(&f.id)?).map_err(invalid)?;
        let mut assets = HashMap::new();
        for a in &file.assets {
            let Some(aid) = str_of(a, "id") else { continue };
            let have = self.db.with_conn(|c| Ok(c.query_row("SELECT 1 FROM assets WHERE id = ?1", [aid], |_| Ok(())).optional()?.is_some()))?;
            if have {
                continue;
            }
            if let Some(rf) = self.files.get(&asset_name(aid)).cloned() {
                assets.insert(aid.to_string(), self.store.get(&rf.id)?);
            }
        }
        Ok((file, assets))
    }

    fn run(&mut self, open: Option<&str>) -> AppResult<()> {
        self.files = self.store.list()?.into_iter().map(|f| (f.name.clone(), f)).collect();
        self.sync_folders()?;

        // Deleted for good here: delete in Drive.
        let tombs: Vec<String> = self.db.with_conn(|c| {
            let mut s = c.prepare("SELECT id FROM sync_tombstones WHERE kind = 'notebook'")?;
            let ids = s.query_map([], |r| r.get(0))?.collect::<Result<_, _>>()?;
            Ok(ids)
        })?;
        for id in &tombs {
            if let Some(f) = self.files.remove(&nb_name(id)) {
                self.store.delete(&f.id)?;
                self.report.deleted += 1;
            }
            self.db.with_conn(|c| {
                c.execute("DELETE FROM sync_tombstones WHERE kind = 'notebook' AND id = ?1", [id])?;
                c.execute("DELETE FROM sync_state WHERE item = ?1", [id])?;
                Ok(())
            })?;
        }

        let (local, dirty, titles) = self.db.with_conn(|c| {
            let mut s = c.prepare("SELECT id, title FROM notebooks")?;
            let rows: Vec<(String, String)> = s.query_map([], |r| Ok((r.get(0)?, r.get(1)?)))?.collect::<Result<_, _>>()?;
            let mut d = c.prepare("SELECT notebook_id FROM sync_changes")?;
            let dirty: HashSet<String> = d.query_map([], |r| r.get(0))?.collect::<Result<_, _>>()?;
            Ok((rows.iter().map(|r| r.0.clone()).collect::<HashSet<String>>(), dirty, rows.into_iter().collect::<HashMap<_, _>>()))
        })?;

        // Notebooks in Drive.
        let remote: Vec<RemoteFile> = self.files.values().filter(|f| f.name.starts_with("nb-") && f.name.ends_with(".json")).cloned().collect();
        let mut seen = HashSet::new();
        for f in remote {
            let id = f.name.trim_start_matches("nb-").trim_end_matches(".json").to_string();
            seen.insert(id.clone());
            let state = state_of(self.db, &id)?;
            let here = local.contains(&id);
            let changed_here = dirty.contains(&id);
            let changed_there = state.as_ref().is_none_or(|s| s.1 != f.version);
            if !changed_there {
                if here && changed_here {
                    self.upload(&id)?;
                }
                continue;
            }
            if here && open == Some(id.as_str()) {
                self.report.deferred.push(titles.get(&id).cloned().unwrap_or_default());
                continue;
            }
            if here && changed_here && state.is_some() {
                // Changed on both sides: Drive's version comes in as a copy, ours goes up.
                let (file, assets) = self.fetch(&f)?;
                let from = f.props.get("device").cloned().unwrap_or_else(|| "other device".into());
                let copy = as_copy(&file, &format!("copy from {from}"));
                let copy_id = str_of(&copy.notebook, "id").unwrap_or_default().to_string();
                let remapped: HashMap<String, Vec<u8>> = file
                    .assets
                    .iter()
                    .zip(copy.assets.iter())
                    .filter_map(|(old, new)| Some((str_of(new, "id")?.to_string(), assets.get(str_of(old, "id")?)?.clone())))
                    .collect();
                // The copy needs the PDFs too, even ones this device already had.
                let mut remapped = remapped;
                for (old, new) in file.assets.iter().zip(copy.assets.iter()) {
                    if let (Some(o), Some(n)) = (str_of(old, "id"), str_of(new, "id")) {
                        if !remapped.contains_key(n) {
                            if let Ok(bytes) = self.db.with_conn(|c| Ok(c.query_row("SELECT data FROM assets WHERE id = ?1", [o], |r| r.get::<_, Vec<u8>>(0))?)) {
                                remapped.insert(n.to_string(), bytes);
                            }
                        }
                    }
                }
                self.db.with_conn(|c| import_notebook(c, &copy, &remapped))?;
                self.db.with_conn(|c| Ok(c.execute("INSERT OR IGNORE INTO sync_changes VALUES (?1)", [&copy_id])?))?;
                self.report.conflicts.push(titles.get(&id).cloned().unwrap_or_default());
                self.upload(&id)?;
                self.upload(&copy_id)?;
            } else if here && changed_here {
                // Never synced from here (e.g. just restored): this device's version wins.
                self.upload(&id)?;
            } else {
                let (file, assets) = self.fetch(&f)?;
                self.db.with_conn(|c| import_notebook(c, &file, &assets))?;
                set_state(self.db, &id, &f.id, &f.version)?;
                self.report.downloaded += 1;
            }
        }

        // Notebooks here that aren't in Drive.
        for id in local.iter().filter(|id| !seen.contains(*id)) {
            let state = state_of(self.db, id)?;
            if state.is_some() && !dirty.contains(id) {
                if open == Some(id.as_str()) {
                    continue;
                }
                // Synced before and now gone from Drive: deleted on another device.
                self.db.with_conn(|c| {
                    c.execute("DELETE FROM notebooks WHERE id = ?1", [id])?;
                    c.execute("DELETE FROM sync_tombstones WHERE kind = 'notebook' AND id = ?1", [id])?;
                    c.execute("DELETE FROM sync_state WHERE item = ?1", [id])?;
                    Ok(())
                })?;
                self.report.deleted += 1;
            } else {
                self.upload(id)?;
            }
        }
        Ok(())
    }
}

/// One sync pass with `store`. `open` is the notebook in the editor (never replaced).
pub fn sync_with<S: RemoteStore>(db: &Db, store: &mut S, device: &str, open: Option<&str>) -> AppResult<SyncReport> {
    let mut e = Engine { db, store, device: device.to_string(), files: HashMap::new(), report: SyncReport::default() };
    e.run(open)?;
    Ok(e.report)
}

// --- Google Drive --------------------------------------------------------------

const CLIENT_ID: Option<&str> = option_env!("INKLEAF_GOOGLE_CLIENT_ID");
const CLIENT_SECRET: Option<&str> = option_env!("INKLEAF_GOOGLE_CLIENT_SECRET");
const SCOPES: &str = "openid email https://www.googleapis.com/auth/drive.appdata";

fn client() -> AppResult<(&'static str, &'static str)> {
    match (CLIENT_ID, CLIENT_SECRET) {
        (Some(id), Some(secret)) if !id.is_empty() => Ok((id, secret)),
        _ => Err(invalid("this build of Inkleaf has no Google sign-in key")),
    }
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout_connect(Duration::from_secs(15)).timeout(Duration::from_secs(180)).try_proxy_from_env(true).build()
}

fn read_all(resp: ureq::Response) -> AppResult<Vec<u8>> {
    let mut buf = Vec::new();
    resp.into_reader().read_to_end(&mut buf)?;
    Ok(buf)
}

/// A request's result: the response, or Google's error as a message.
fn call(r: Result<ureq::Response, ureq::Error>) -> AppResult<ureq::Response> {
    match r {
        Ok(resp) => Ok(resp),
        Err(ureq::Error::Status(code, resp)) => {
            let body = resp.into_string().unwrap_or_default();
            let msg = serde_json::from_str::<Value>(&body)
                .ok()
                .and_then(|v| v.pointer("/error/message").or_else(|| v.get("error_description")).or_else(|| v.get("error")).and_then(Value::as_str).map(str::to_string))
                .unwrap_or(body);
            Err(invalid(format!("Google Drive ({code}): {msg}")))
        }
        Err(e) => Err(invalid(format!("can't reach Google (are you online?): {e}"))),
    }
}

fn json_of(resp: ureq::Response) -> AppResult<Value> {
    serde_json::from_slice(&read_all(resp)?).map_err(invalid)
}

pub struct Drive {
    agent: ureq::Agent,
    token: String,
}

impl Drive {
    fn auth(&self) -> String {
        format!("Bearer {}", self.token)
    }

    /// Sends `bytes` to a resumable upload session started by `start`.
    fn upload_to(&self, start: ureq::Request, meta: Value, bytes: &[u8]) -> AppResult<(String, String)> {
        let session = call(start.set("Authorization", &self.auth()).set("X-Upload-Content-Type", "application/octet-stream").set("Content-Type", "application/json; charset=UTF-8").send_string(&meta.to_string()))?;
        let url = session.header("Location").ok_or_else(|| invalid("Google Drive didn't start the upload"))?.to_string();
        let done = json_of(call(self.agent.put(&url).set("Content-Type", "application/octet-stream").send_bytes(bytes))?)?;
        let id = done.get("id").and_then(Value::as_str).ok_or_else(|| invalid("Google Drive returned no file id"))?;
        let version = done.get("version").and_then(Value::as_str).unwrap_or("0");
        Ok((id.to_string(), version.to_string()))
    }
}

impl RemoteStore for Drive {
    fn list(&mut self) -> AppResult<Vec<RemoteFile>> {
        let mut out = Vec::new();
        let mut page: Option<String> = None;
        loop {
            let mut req = self
                .agent
                .get("https://www.googleapis.com/drive/v3/files")
                .query("spaces", "appDataFolder")
                .query("pageSize", "1000")
                .query("fields", "nextPageToken,files(id,name,version,appProperties)")
                .set("Authorization", &self.auth());
            if let Some(p) = &page {
                req = req.query("pageToken", p);
            }
            let v = json_of(call(req.call())?)?;
            for f in v.get("files").and_then(Value::as_array).into_iter().flatten() {
                let props = f
                    .get("appProperties")
                    .and_then(Value::as_object)
                    .map(|m| m.iter().filter_map(|(k, v)| Some((k.clone(), v.as_str()?.to_string()))).collect())
                    .unwrap_or_default();
                if let (Some(id), Some(name)) = (f.get("id").and_then(Value::as_str), f.get("name").and_then(Value::as_str)) {
                    out.push(RemoteFile { id: id.into(), name: name.into(), version: f.get("version").and_then(Value::as_str).unwrap_or("0").into(), props });
                }
            }
            page = v.get("nextPageToken").and_then(Value::as_str).map(str::to_string);
            if page.is_none() {
                return Ok(out);
            }
        }
    }

    fn get(&mut self, id: &str) -> AppResult<Vec<u8>> {
        read_all(call(self.agent.get(&format!("https://www.googleapis.com/drive/v3/files/{id}")).query("alt", "media").set("Authorization", &self.auth()).call())?)
    }

    fn put(&mut self, id: Option<&str>, name: &str, props: &HashMap<String, String>, bytes: &[u8]) -> AppResult<(String, String)> {
        match id {
            None => {
                let start = self.agent.post("https://www.googleapis.com/upload/drive/v3/files").query("uploadType", "resumable").query("fields", "id,version");
                self.upload_to(start, serde_json::json!({ "name": name, "parents": ["appDataFolder"], "appProperties": props }), bytes)
            }
            Some(id) => {
                let start = self.agent.request("PATCH", &format!("https://www.googleapis.com/upload/drive/v3/files/{id}")).query("uploadType", "resumable").query("fields", "id,version");
                self.upload_to(start, serde_json::json!({ "appProperties": props }), bytes)
            }
        }
    }

    fn delete(&mut self, id: &str) -> AppResult<()> {
        match self.agent.delete(&format!("https://www.googleapis.com/drive/v3/files/{id}")).set("Authorization", &self.auth()).call() {
            Ok(_) | Err(ureq::Error::Status(404, _)) => Ok(()),
            other => call(other).map(|_| ()),
        }
    }
}

// --- sign-in and settings --------------------------------------------------------

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
struct SyncConfig {
    refresh_token: Option<String>,
    email: Option<String>,
    device: String,
    last_sync_at: Option<i64>,
    last_error: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncStatus {
    /// This build can sign in to Google.
    pub available: bool,
    pub signed_in: bool,
    pub email: Option<String>,
    pub device: String,
    pub last_sync_at: Option<i64>,
    pub last_error: Option<String>,
}

/// How to sign in: the page to open (`verification_url`). `user_code` is
/// empty: the browser brings the answer back by itself.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceCode {
    pub user_code: String,
    pub verification_url: String,
    pub expires_in: u64,
}

struct Pending {
    /// Where the browser comes back to.
    listener: TcpListener,
    redirect: String,
    /// PKCE: the secret whose hash went with the request.
    verifier: String,
    /// Ties the answer to this sign-in.
    state: String,
    deadline: Instant,
}

pub struct SyncManager {
    file: PathBuf,
    config: Mutex<SyncConfig>,
    running: Mutex<()>,
    pending: Mutex<Option<Pending>>,
    cancel: AtomicBool,
}

fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

/// Percent-encodes a query value.
fn url_encode(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

fn url_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < b.len() => {
                if let Some(v) = std::str::from_utf8(&b[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok()) {
                    out.push(v);
                    i += 2;
                } else {
                    out.push(b'%');
                }
            }
            c => out.push(c),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// Reads the browser's visit to the listener and answers it with a short
/// page. None: not the sign-in answer (e.g. a favicon request); else the
/// code, or why there is none.
fn browser_answer(mut stream: TcpStream, state: &str) -> Option<AppResult<String>> {
    let _ = stream.set_nonblocking(false);
    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let mut buf = [0u8; 8192];
    let mut req = Vec::new();
    while !req.windows(4).any(|w| w == b"\r\n\r\n") && req.len() < buf.len() {
        match stream.read(&mut buf) {
            Ok(0) | Err(_) => break,
            Ok(n) => req.extend_from_slice(&buf[..n]),
        }
    }
    let req = String::from_utf8_lossy(&req);
    let target = req.lines().next()?.split_whitespace().nth(1)?.to_string();
    let reply = |stream: &mut TcpStream, ok: bool| {
        let (title, line) = if ok {
            ("Signed in", "Inkleaf can now keep your notebooks in Google Drive. You can close this tab and go back to Inkleaf.")
        } else {
            ("Not signed in", "Inkleaf wasn't given access to Google Drive. You can close this tab and try again from Inkleaf.")
        };
        let body = format!(
            "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>{title}</title>\
             <body style='font-family:system-ui,sans-serif;display:grid;place-items:center;min-height:90vh;margin:0;background:#f6f5f1;color:#1f2328'>\
             <div style='max-width:420px;padding:24px;text-align:center'><h1 style='font-size:22px'>{title}</h1><p style='line-height:1.5;color:#59606b'>{line}</p></div>"
        );
        let _ = write!(stream, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len());
        let _ = stream.flush();
    };
    let query = target.split_once('?').map(|(_, q)| q).unwrap_or("");
    let params: HashMap<String, String> = query
        .split('&')
        .filter_map(|kv| kv.split_once('='))
        .map(|(k, v)| (url_decode(k), url_decode(v)))
        .collect();
    if !params.contains_key("code") && !params.contains_key("error") {
        let _ = write!(stream, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return None;
    }
    if params.get("state").map(String::as_str) != Some(state) {
        reply(&mut stream, false);
        return Some(Err(invalid("the sign-in answer didn't match: try again")));
    }
    if let Some(e) = params.get("error") {
        reply(&mut stream, false);
        return Some(Err(invalid(if e == "access_denied" { "access to Google Drive wasn't allowed".to_string() } else { format!("Google sign-in failed: {e}") })));
    }
    reply(&mut stream, true);
    Some(Ok(params["code"].clone()))
}

/// The email in a Google id_token (its middle part is base64 JSON).
fn email_of(id_token: &str) -> Option<String> {
    let payload = id_token.split('.').nth(1)?;
    let bytes = base64::engine::general_purpose::URL_SAFE_NO_PAD.decode(payload.trim_end_matches('=')).ok()?;
    serde_json::from_slice::<Value>(&bytes).ok()?.get("email")?.as_str().map(str::to_string)
}

impl SyncManager {
    pub fn load(file: PathBuf) -> Self {
        let config = std::fs::read_to_string(&file).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default();
        SyncManager { file, config: Mutex::new(config), running: Mutex::new(()), pending: Mutex::new(None), cancel: AtomicBool::new(false) }
    }

    fn save(&self, c: &SyncConfig) {
        if let Ok(json) = serde_json::to_string_pretty(c) {
            let _ = std::fs::write(&self.file, json);
        }
    }

    pub fn status(&self) -> SyncStatus {
        let c = lock(&self.config);
        SyncStatus {
            available: client().is_ok(),
            signed_in: c.refresh_token.is_some(),
            email: c.email.clone(),
            device: c.device.clone(),
            last_sync_at: c.last_sync_at,
            last_error: c.last_error.clone(),
        }
    }

    /// Step 1: Google's sign-in page to open in the browser. The browser comes
    /// back to a one-off listener on this computer (127.0.0.1) with the
    /// answer: Google's sign-in for installed apps, with PKCE.
    pub fn sign_in_start(&self) -> AppResult<DeviceCode> {
        let (id, _) = client()?;
        let listener = TcpListener::bind(("127.0.0.1", 0)).map_err(|e| invalid(format!("can't start signing in: {e}")))?;
        listener.set_nonblocking(true)?;
        let port = listener.local_addr()?.port();
        let redirect = format!("http://127.0.0.1:{port}");
        let verifier = format!("{}{}", uuid::Uuid::new_v4().simple(), uuid::Uuid::new_v4().simple());
        let challenge = base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(Sha256::digest(verifier.as_bytes()));
        let state = uuid::Uuid::new_v4().simple().to_string();
        let query = [
            ("client_id", id),
            ("redirect_uri", redirect.as_str()),
            ("response_type", "code"),
            ("scope", SCOPES),
            ("code_challenge", challenge.as_str()),
            ("code_challenge_method", "S256"),
            ("state", state.as_str()),
            ("access_type", "offline"),
            ("prompt", "consent"),
        ];
        let url = format!(
            "https://accounts.google.com/o/oauth2/v2/auth?{}",
            query.iter().map(|(k, v)| format!("{k}={}", url_encode(v))).collect::<Vec<_>>().join("&")
        );
        let expires_in = 600;
        *lock(&self.pending) = Some(Pending { listener, redirect, verifier, state, deadline: Instant::now() + Duration::from_secs(expires_in) });
        self.cancel.store(false, Ordering::SeqCst);
        Ok(DeviceCode { user_code: String::new(), verification_url: url, expires_in })
    }

    /// Step 2: waits for the browser to come back (allowed or refused), then
    /// trades the answer for lasting access.
    pub fn sign_in_wait(&self, device: &str) -> AppResult<SyncStatus> {
        let (id, secret) = client()?;
        let pending = lock(&self.pending).take().ok_or_else(|| invalid("start signing in first"))?;
        let code = loop {
            if self.cancel.load(Ordering::SeqCst) {
                return Err(invalid("sign-in cancelled"));
            }
            if Instant::now() > pending.deadline {
                return Err(invalid("signing in took too long: try again"));
            }
            match pending.listener.accept() {
                Ok((stream, _)) => {
                    if let Some(answer) = browser_answer(stream, &pending.state) {
                        break answer?;
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => std::thread::sleep(Duration::from_millis(150)),
                Err(e) => return Err(invalid(format!("signing in failed: {e}"))),
            }
        };
        let r = agent().post("https://oauth2.googleapis.com/token").send_form(&[
            ("client_id", id),
            ("client_secret", secret),
            ("code", &code),
            ("code_verifier", &pending.verifier),
            ("redirect_uri", &pending.redirect),
            ("grant_type", "authorization_code"),
        ]);
        let v = json_of(call(r)?)?;
        let refresh = v.get("refresh_token").and_then(Value::as_str).ok_or_else(|| invalid("Google didn't allow offline access"))?;
        let mut c = lock(&self.config);
        c.refresh_token = Some(refresh.to_string());
        c.email = v.get("id_token").and_then(Value::as_str).and_then(email_of);
        c.device = device.trim().chars().take(40).collect();
        c.last_error = None;
        self.save(&c);
        drop(c);
        Ok(self.status())
    }

    pub fn sign_in_cancel(&self) {
        self.cancel.store(true, Ordering::SeqCst);
    }

    /// Signs out: forgets (and revokes) the access. Synced data stays in Drive and here.
    pub fn sign_out(&self, db: &Db) -> AppResult<SyncStatus> {
        let token = lock(&self.config).refresh_token.take();
        if let Some(t) = token {
            let _ = agent().post("https://oauth2.googleapis.com/revoke").send_form(&[("token", t.as_str())]);
        }
        {
            let mut c = lock(&self.config);
            c.email = None;
            c.last_error = None;
            c.last_sync_at = None;
            self.save(&c);
        }
        // Another account might be used next: start over from "nothing synced".
        db.with_conn(|c| {
            c.execute_batch(
                "DELETE FROM sync_state; DELETE FROM sync_tombstones;
                 INSERT OR IGNORE INTO sync_changes SELECT id FROM notebooks;
                 INSERT OR IGNORE INTO sync_changes VALUES ('__folders__');",
            )?;
            Ok(())
        })?;
        Ok(self.status())
    }

    fn access_token(&self) -> AppResult<String> {
        let (id, secret) = client()?;
        let refresh = lock(&self.config).refresh_token.clone().ok_or_else(|| invalid("sign in to Google Drive first"))?;
        match agent().post("https://oauth2.googleapis.com/token").send_form(&[("client_id", id), ("client_secret", secret), ("refresh_token", &refresh), ("grant_type", "refresh_token")]) {
            Ok(resp) => json_of(resp)?.get("access_token").and_then(Value::as_str).map(str::to_string).ok_or_else(|| invalid("Google returned no access token")),
            Err(ureq::Error::Status(code, resp)) => {
                let body = resp.into_string().unwrap_or_default();
                if body.contains("invalid_grant") {
                    // Revoked, or unused for too long: a new sign-in is needed.
                    let mut c = lock(&self.config);
                    c.refresh_token = None;
                    self.save(&c);
                    return Err(invalid("Google Drive access ended: sign in again"));
                }
                Err(invalid(format!("Google sign-in ({code}): {body}")))
            }
            Err(e) => Err(invalid(format!("can't reach Google (are you online?): {e}"))),
        }
    }

    /// One sync with Google Drive (skipped when one is already running).
    pub fn sync_now(&self, db: &Db, open: Option<&str>) -> AppResult<SyncReport> {
        let Ok(_guard) = self.running.try_lock() else { return Ok(SyncReport::default()) };
        let result = (|| {
            let mut drive = Drive { agent: agent(), token: self.access_token()? };
            let device = lock(&self.config).device.clone();
            sync_with(db, &mut drive, if device.is_empty() { "other device" } else { &device }, open)
        })();
        let mut c = lock(&self.config);
        match &result {
            Ok(_) => {
                c.last_sync_at = Some(now_ms());
                c.last_error = None;
            }
            Err(e) => c.last_error = Some(e.to_string()),
        }
        self.save(&c);
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::{Element, NewNotebook};

    /// Drive in memory: files with versions that change on every write.
    #[derive(Default)]
    struct Mem {
        files: HashMap<String, (RemoteFile, Vec<u8>)>,
        next: u64,
    }

    impl RemoteStore for Mem {
        fn list(&mut self) -> AppResult<Vec<RemoteFile>> {
            Ok(self.files.values().map(|f| f.0.clone()).collect())
        }
        fn get(&mut self, id: &str) -> AppResult<Vec<u8>> {
            self.files.get(id).map(|f| f.1.clone()).ok_or_else(|| AppError::NotFound(id.into()))
        }
        fn put(&mut self, id: Option<&str>, name: &str, props: &HashMap<String, String>, bytes: &[u8]) -> AppResult<(String, String)> {
            self.next += 1;
            let id = id.map(str::to_string).unwrap_or_else(|| format!("f{}", self.next));
            let version = self.next.to_string();
            self.files.insert(id.clone(), (RemoteFile { id: id.clone(), name: name.into(), version: version.clone(), props: props.clone() }, bytes.to_vec()));
            Ok((id, version))
        }
        fn delete(&mut self, id: &str) -> AppResult<()> {
            self.files.remove(id);
            Ok(())
        }
    }

    fn device() -> Db {
        Db::open_in_memory().unwrap()
    }

    fn notebook(db: &Db, title: &str) -> String {
        db.create_notebook(&NewNotebook { title: title.into(), folder_id: None, cover_color: None, template: None, paper_color: None, width: None, height: None, cover_page: false }).unwrap().id
    }

    fn write(db: &Db, nb: &str, text: &str) {
        let page = db.pages(nb).unwrap()[0].id.clone();
        let el = Element { id: new_id(), kind: "text".into(), z: 0, data: serde_json::json!({ "text": text }) };
        db.save_elements(&page, &[el]).unwrap();
    }

    fn texts(db: &Db, nb: &str) -> Vec<String> {
        let page = db.pages(nb).unwrap()[0].id.clone();
        db.elements(&page).unwrap().iter().map(|e| e.data["text"].as_str().unwrap_or("").to_string()).collect()
    }

    fn titles(db: &Db) -> Vec<String> {
        let mut t: Vec<String> = db.library().unwrap().notebooks.into_iter().map(|n| n.title).collect();
        t.sort();
        t
    }

    #[test]
    fn a_notebook_travels_to_the_other_device_and_back() {
        let (pc, tablet, mut drive) = (device(), device(), Mem::default());
        let nb = notebook(&pc, "Maths");
        write(&pc, &nb, "x = 2");
        assert_eq!(sync_with(&pc, &mut drive, "PC", None).unwrap().uploaded, 1);
        assert_eq!(sync_with(&tablet, &mut drive, "Tablet", None).unwrap().downloaded, 1);
        assert_eq!(texts(&tablet, &nb), vec!["x = 2"]);
        // Nothing changed: nothing moves.
        let quiet = sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        assert_eq!((quiet.uploaded, quiet.downloaded), (0, 0));
        // Edit on the tablet, back on the PC.
        write(&tablet, &nb, "x = 3");
        assert_eq!(sync_with(&tablet, &mut drive, "Tablet", None).unwrap().uploaded, 1);
        assert_eq!(sync_with(&pc, &mut drive, "PC", None).unwrap().downloaded, 1);
        assert_eq!(texts(&pc, &nb), vec!["x = 3"]);
        // Downloading doesn't count as a change here.
        assert_eq!(sync_with(&pc, &mut drive, "PC", None).unwrap().uploaded, 0);
    }

    #[test]
    fn changes_on_both_sides_keep_both_versions() {
        let (pc, tablet, mut drive) = (device(), device(), Mem::default());
        let nb = notebook(&pc, "Daftar");
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        write(&pc, &nb, "from the PC");
        write(&tablet, &nb, "from the tablet");
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        let r = sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        assert_eq!(r.conflicts, vec!["Daftar"]);
        assert_eq!(titles(&tablet), vec!["Daftar", "Daftar (copy from PC)"]);
        assert_eq!(texts(&tablet, &nb), vec!["from the tablet"]);
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        assert_eq!(titles(&pc), vec!["Daftar", "Daftar (copy from PC)"]);
        assert_eq!(texts(&pc, &nb), vec!["from the tablet"]);
    }

    #[test]
    fn deleting_for_good_reaches_the_other_device() {
        let (pc, tablet, mut drive) = (device(), device(), Mem::default());
        let nb = notebook(&pc, "Old");
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        pc.delete_notebook(&nb).unwrap();
        assert_eq!(sync_with(&pc, &mut drive, "PC", None).unwrap().deleted, 1);
        assert_eq!(sync_with(&tablet, &mut drive, "Tablet", None).unwrap().deleted, 1);
        assert!(titles(&tablet).is_empty());
        // It doesn't come back.
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        assert!(titles(&pc).is_empty());
    }

    #[test]
    fn the_open_notebook_is_not_replaced() {
        let (pc, tablet, mut drive) = (device(), device(), Mem::default());
        let nb = notebook(&pc, "Open");
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        write(&pc, &nb, "new");
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        let r = sync_with(&tablet, &mut drive, "Tablet", Some(&nb)).unwrap();
        assert_eq!(r.deferred, vec!["Open"]);
        assert!(texts(&tablet, &nb).is_empty());
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        assert_eq!(texts(&tablet, &nb), vec!["new"]);
    }

    #[test]
    fn folders_and_pdfs_come_along() {
        let (pc, tablet, mut drive) = (device(), device(), Mem::default());
        let folder = pc.create_folder("Classes", None, None).unwrap();
        let asset = pc.store_pdf_asset(b"%PDF-1.7 lesson").unwrap();
        let nb = pc
            .create_pdf_notebook(&crate::db::NewPdfNotebook { title: "Worksheet".into(), folder_id: Some(folder.id.clone()), cover_color: None, asset_id: asset.clone(), pages: vec![crate::db::PdfPageSize { width: 600.0, height: 800.0 }] })
            .unwrap();
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        let lib = tablet.library().unwrap();
        assert_eq!(lib.folders.len(), 1);
        assert_eq!(lib.notebooks[0].folder_id.as_deref(), Some(folder.id.as_str()));
        let page = tablet.pages(&nb.id).unwrap()[0].clone();
        assert_eq!(tablet.asset_bytes(page.background_asset.as_deref().unwrap()).unwrap(), b"%PDF-1.7 lesson");
        // A folder renamed on the tablet is renamed on the PC.
        tablet.rename_folder(&folder.id, "Classes 2026").unwrap();
        sync_with(&tablet, &mut drive, "Tablet", None).unwrap();
        sync_with(&pc, &mut drive, "PC", None).unwrap();
        assert_eq!(pc.library().unwrap().folders[0].name, "Classes 2026");
    }
}

#[cfg(test)]
mod sign_in_tests {
    use super::*;

    fn visit(path: &str, state: &str) -> (Option<AppResult<String>>, String) {
        let l = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let port = l.local_addr().unwrap().port();
        let path = path.to_string();
        let browser = std::thread::spawn(move || {
            let mut s = TcpStream::connect(("127.0.0.1", port)).unwrap();
            write!(s, "GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").unwrap();
            let mut page = String::new();
            let _ = s.read_to_string(&mut page);
            page
        });
        let (stream, _) = l.accept().unwrap();
        let answer = browser_answer(stream, state);
        (answer, browser.join().unwrap())
    }

    #[test]
    fn the_browser_brings_the_code_back() {
        let (a, page) = visit("/?state=abc&code=4%2F0Ab-x_y&scope=email", "abc");
        assert_eq!(a.unwrap().unwrap(), "4/0Ab-x_y");
        assert!(page.starts_with("HTTP/1.1 200") && page.contains("Signed in"));
    }

    #[test]
    fn refusals_and_strangers_are_not_codes() {
        let (a, page) = visit("/?state=abc&error=access_denied", "abc");
        assert!(a.unwrap().is_err() && page.contains("Not signed in"));
        let (a, _) = visit("/?state=zzz&code=1", "abc");
        assert!(a.unwrap().is_err());
        let (a, page) = visit("/favicon.ico", "abc");
        assert!(a.is_none() && page.starts_with("HTTP/1.1 404"));
    }

    #[test]
    fn query_values_round_trip() {
        let s = "http://127.0.0.1:1234 openid email";
        assert_eq!(url_decode(&url_encode(s)), s);
    }
}
