//! The Qt app's door into Inkleaf's core (database, sync, backups, export,
//! ink geometry, handwriting recognition).
//!
//! One call does everything: a command name, its arguments as JSON (the
//! same names and camelCase keys as the web front end's commands) and,
//! for files, raw bytes. The answer is JSON, or raw bytes for commands
//! that return a file (`assetBytes`, `buildDocument`, `librarySnapshot`).
//! Errors come back as `{"error": "..."}` with `ok` false.

use std::ffi::{c_char, CStr};
use std::path::{Path, PathBuf};

use inkleaf_core::ai::{Ai, AiChange, AskRequest};
use inkleaf_core::backup::Backups;
use inkleaf_core::db::{Db, Element, NewNotebook, NewPdfNotebook};
use inkleaf_core::error::{AppError, AppResult};
use inkleaf_core::export::{self, ExportPage, Format};
use inkleaf_core::handwriting::{self, InkStroke};
use inkleaf_core::ink::{self, InkPoint};
use inkleaf_core::recognize;
use inkleaf_core::sync::SyncManager;
use serde::de::DeserializeOwned;
use serde::Serialize;
use serde_json::{json, Value};

/// Everything the app keeps open: the database, backup and sync settings.
pub struct Core {
    db: Db,
    backups: Backups,
    sync: SyncManager,
    ai: Ai,
    dir: PathBuf,
}

impl Core {
    /// Opens (or creates) the library in `dir`: the library's database and files.
    pub fn open(dir: &Path) -> AppResult<Core> {
        std::fs::create_dir_all(dir)?;
        Ok(Core {
            db: Db::open(&dir.join("inkleaf.db"))?,
            backups: Backups::load(dir.join("backup.json")),
            sync: SyncManager::load(dir.join("sync.json")),
            // Not in the library: an API key stays out of backups and sync.
            ai: Ai::load(dir.join("ai.json")),
            dir: dir.to_path_buf(),
        })
    }

    /// Runs a command. `Ok(Answer)` is JSON or bytes.
    pub fn call(&self, cmd: &str, args: &Value, bytes: &[u8]) -> AppResult<Answer> {
        let a = Args(args);
        let db = &self.db;
        let ok = |v: Value| Ok(Answer::Json(v));
        let to = |v: &dyn erased::Ser| -> AppResult<Answer> { Ok(Answer::Json(v.to_value()?)) };
        match cmd {
            // library and trash
            "library" => to(&db.library()?),
            "trash" => to(&db.trash()?),
            "emptyTrash" => to(&db.empty_trash()?),
            "search" => to(&db.search(&a.s("query")?)?),
            // folders
            "createFolder" => to(&db.create_folder(&a.s("name")?, a.opt_s("parentId").as_deref(), a.opt_s("color").as_deref())?),
            "renameFolder" => to(&db.rename_folder(&a.s("id")?, &a.s("name")?)?),
            "moveFolder" => to(&db.move_folder(&a.s("id")?, a.opt_s("parentId").as_deref())?),
            "setFolderTrashed" => to(&db.set_folder_trashed(&a.s("id")?, a.b("trashed")?)?),
            "deleteFolder" => to(&db.delete_folder(&a.s("id")?)?),
            "setFolderColor" => to(&db.set_folder_color(&a.s("id")?, &a.s("color")?)?),
            "setFolderFavourite" => to(&db.set_folder_favourite(&a.s("id")?, a.b("favourite")?)?),
            // notebooks
            "createNotebook" => to(&db.create_notebook(&a.de::<NewNotebook>("input")?)?),
            "notebook" => to(&db.notebook(&a.s("id")?)?),
            "renameNotebook" => to(&db.rename_notebook(&a.s("id")?, &a.s("title")?)?),
            "moveNotebook" => to(&db.move_notebook(&a.s("id")?, a.opt_s("folderId").as_deref())?),
            "setNotebookCover" => to(&db.set_notebook_cover(&a.s("id")?, &a.s("color")?)?),
            "setNotebookMeta" => to(&db.set_notebook_meta(&a.s("id")?, a.opt_s("meta").as_deref())?),
            "setNotebookTrashed" => to(&db.set_notebook_trashed(&a.s("id")?, a.b("trashed")?)?),
            "deleteNotebook" => to(&db.delete_notebook(&a.s("id")?)?),
            "setNotebookFavourite" => to(&db.set_notebook_favourite(&a.s("id")?, a.b("favourite")?)?),
            "duplicateNotebook" => to(&db.duplicate_notebook(&a.s("id")?)?),
            // pages and their content
            "pages" => to(&db.pages(&a.s("notebookId")?)?),
            "addPage" => to(&db.add_page(&a.s("notebookId")?, a.opt_i("index"), a.opt_s("template").as_deref())?),
            "duplicatePage" => to(&db.duplicate_page(&a.s("pageId")?)?),
            "deletePage" => to(&db.delete_page(&a.s("pageId")?)?),
            "movePage" => to(&db.move_page(&a.s("pageId")?, a.i("newIndex")?)?),
            "setPageTemplate" => to(&db.set_page_template(&a.s("pageId")?, &a.s("template")?, a.opt_s("paperColor").as_deref())?),
            "setPageFavourite" => to(&db.set_page_favourite(&a.s("pageId")?, a.b("favourite")?)?),
            "setPageThumbnail" => to(&db.set_page_thumbnail(&a.s("pageId")?, &a.s("thumbnail")?)?),
            "setPageSection" => to(&db.set_page_section(&a.s("pageId")?, a.opt_s("section").as_deref())?),
            "elements" => to(&db.elements(&a.s("pageId")?)?),
            "snippets" => to(&db.snippets()?),
            "saveSnippet" => to(&db.save_snippet(
                &a.s("name")?,
                &a.s("data")?,
                a.opt_s("preview").as_deref(),
                a.f("width")?,
                a.f("height")?,
                a.opt_s("notebookId").as_deref(),
            )?),
            "renameSnippet" => to(&db.rename_snippet(&a.s("id")?, &a.s("name")?)?),
            "deleteSnippet" => to(&db.delete_snippet(&a.s("id")?)?),
            "useSnippet" => to(&db.use_snippet(&a.s("id")?)?),
            "aiStatus" => to(&self.ai.status()),
            "aiSetup" => to(&self.ai.change(a.de::<AiChange>("change")?)?),
            "aiTest" => to(&self.ai.test()?),
            "aiAsk" => to(&self.ai.ask(&a.de::<AskRequest>("request")?)?),
            "notesForAi" => to(&db.notes_for_ai(a.opt_s("notebookId").as_deref(), &a.opt_s("question").unwrap_or_default(), a.opt_i("budget").unwrap_or(60_000).max(1000) as usize)?),
            "pagesToRead" => to(&db.pages_to_read(a.opt_i("limit").unwrap_or(5), a.opt_i("settleMs").unwrap_or(60_000))?),
            "readingProgress" => {
                let (done, total) = db.reading_progress()?;
                ok(json!({ "done": done, "total": total }))
            }
            "setPageInkText" => to(&db.set_page_ink_text(&a.s("pageId")?, &a.s("text")?)?),
            "saveElements" => to(&db.save_elements(&a.s("pageId")?, &a.de::<Vec<Element>>("elements")?)?),
            // shape and symbol recognition (points: [x, y, …])
            "recognizeShape" => to(&recognize::beautify::recognize_shape(&p2s(&a.de::<Vec<Vec<f64>>>("points")?))),
            "beautify" => to(&recognize::beautify::beautify(&p2s(&a.de::<Vec<Vec<f64>>>("points")?))),
            "snapToAxis" => {
                let p = p2s(&a.de::<Vec<Vec<f64>>>("points")?);
                if p.len() < 2 {
                    return Err(AppError::Invalid("snapToAxis: two points".into()));
                }
                to(&recognize::beautify::snap_to_axis(p[0], p[1], a.get("tolerance").and_then(Value::as_f64).unwrap_or(4.0)))
            }
            "matchSymbol" => to(&recognize::symbols::match_symbol(&a.de::<Vec<Vec<Vec<f64>>>>("strokes")?.iter().map(|s| p2s(s)).collect::<Vec<_>>())),
            "rankSymbols" => to(&recognize::symbols::rank_symbols(&a.de::<Vec<Vec<Vec<f64>>>>("strokes")?.iter().map(|s| p2s(s)).collect::<Vec<_>>())),
            "symbolShape" => to(&recognize::symbols::symbol_shape(&a.s("name")?, a.f("x")?, a.f("y")?, a.f("w")?, a.f("h")?)),
            "simplifyStroke" => to(&ink::simplify(&a.de::<Vec<InkPoint>>("points")?, a.f("epsilon")?)),
            // PDFs
            "storePdfAsset" => to(&db.store_pdf_asset(bytes)?),
            "createPdfNotebook" => to(&db.create_pdf_notebook(&a.de::<NewPdfNotebook>("input")?)?),
            "assetBytes" => Ok(Answer::Bytes(db.asset_bytes(&a.s("id")?)?)),
            // export, handwriting
            "buildDocument" => Ok(Answer::Bytes(export::build(Format::parse(&a.s("format")?)?, &a.de::<Vec<ExportPage>>("pages")?)?)),
            "recognizeHandwriting" => to(&handwriting::recognize(&a.de::<Vec<InkStroke>>("strokes")?).map_err(AppError::Invalid)?),
            // backups
            "backupStatus" => to(&self.backups.status()),
            "setBackupSettings" => to(&self.backups.configure(a.opt_s("folder"), a.b("daily")?, a.i("keep")? as u32)?),
            "backupNow" => to(&self.backups.backup_now(db, &a.s("label")?)?),
            "backupIfDue" => to(&self.backups.backup_if_due(db, &a.s("label")?)),
            "exportLibrary" => {
                let mut path = PathBuf::from(a.s("path")?);
                if path.extension().and_then(|e| e.to_str()) != Some("inkleaf") {
                    path.set_extension("inkleaf");
                }
                db.snapshot_to(&path)?;
                ok(json!(path.to_string_lossy()))
            }
            "inspectBackup" => to(&Db::inspect_backup(Path::new(&a.s("path")?))?),
            "restoreBackup" => to(&db.restore_from(Path::new(&a.s("path")?))?),
            // sync
            "syncStatus" => to(&self.sync.status()),
            "syncSignInStart" => to(&self.sync.sign_in_start()?),
            "syncSignInWait" => to(&self.sync.sign_in_wait(&a.s("device")?)?),
            "syncSignInCancel" => {
                self.sync.sign_in_cancel();
                ok(Value::Null)
            }
            "syncSignOut" => to(&self.sync.sign_out(db)?),
            "syncNow" => to(&self.sync.sync_now(db, a.opt_s("open").as_deref())?),
            "dataDir" => ok(json!(self.dir.to_string_lossy())),
            // passwords (encryption: core/src/lock.rs)
            "lockNotebook" => to(&db.lock_notebook(&a.s("id")?, &a.s("password")?)?),
            "lockFolder" => to(&db.lock_folder(&a.s("id")?, &a.s("password")?)?),
            "unlock" => to(&db.unlock(&a.s("kind")?, &a.s("id")?, &a.s("password")?)?),
            "lockAll" => to(&db.lock_all()),
            "anyUnlocked" => ok(json!(db.any_unlocked())),
            "changePassword" => to(&db.change_password(&a.s("kind")?, &a.s("id")?, &a.s("password")?)?),
            "removeLock" => to(&db.remove_lock(&a.s("kind")?, &a.s("id")?)?),
            _ => Err(AppError::Invalid(format!("unknown command: {cmd}"))),
        }
    }
}

/// What a command returns.
pub enum Answer {
    Json(Value),
    Bytes(Vec<u8>),
}

mod erased {
    use super::*;
    /// Anything serializable, as a JSON value (so the match arms stay one line each).
    pub trait Ser {
        fn to_value(&self) -> AppResult<Value>;
    }
    impl<T: Serialize> Ser for T {
        fn to_value(&self) -> AppResult<Value> {
            serde_json::to_value(self).map_err(|e| AppError::Invalid(e.to_string()))
        }
    }
}

/// Reading command arguments.
/// [x, y] from [x, y, …] points.
fn p2s(v: &[Vec<f64>]) -> Vec<inkleaf_core::recognize::P2> {
    v.iter().map(|p| [p.first().copied().unwrap_or(0.0), p.get(1).copied().unwrap_or(0.0)]).collect()
}

struct Args<'a>(&'a Value);

impl Args<'_> {
    fn get(&self, k: &str) -> Option<&Value> {
        self.0.get(k).filter(|v| !v.is_null())
    }
    fn missing(k: &str) -> AppError {
        AppError::Invalid(format!("missing argument: {k}"))
    }
    fn s(&self, k: &str) -> AppResult<String> {
        self.get(k).and_then(Value::as_str).map(str::to_string).ok_or_else(|| Self::missing(k))
    }
    fn opt_s(&self, k: &str) -> Option<String> {
        self.get(k).and_then(Value::as_str).map(str::to_string)
    }
    fn b(&self, k: &str) -> AppResult<bool> {
        self.get(k).and_then(Value::as_bool).ok_or_else(|| Self::missing(k))
    }
    fn i(&self, k: &str) -> AppResult<i64> {
        self.get(k).and_then(Value::as_i64).ok_or_else(|| Self::missing(k))
    }
    fn opt_i(&self, k: &str) -> Option<i64> {
        self.get(k).and_then(Value::as_i64)
    }
    fn f(&self, k: &str) -> AppResult<f64> {
        self.get(k).and_then(Value::as_f64).ok_or_else(|| Self::missing(k))
    }
    fn de<T: DeserializeOwned>(&self, k: &str) -> AppResult<T> {
        let v = self.get(k).ok_or_else(|| Self::missing(k))?;
        serde_json::from_value(v.clone()).map_err(|e| AppError::Invalid(format!("{k}: {e}")))
    }
}

// --- C interface ---------------------------------------------------------------

/// A command's answer for C: `ok` false means `data` is `{"error": ...}`.
/// `is_json` tells JSON text from raw bytes. Free it with `inkleaf_free`.
#[repr(C)]
pub struct InkleafResult {
    pub ok: bool,
    pub is_json: bool,
    pub data: *mut u8,
    pub len: usize,
}

fn result(ok: bool, is_json: bool, bytes: Vec<u8>) -> InkleafResult {
    let mut b = bytes.into_boxed_slice();
    let (data, len) = (b.as_mut_ptr(), b.len());
    std::mem::forget(b);
    InkleafResult { ok, is_json, data, len }
}

fn error_result(e: impl std::fmt::Display) -> InkleafResult {
    result(false, true, json!({ "error": e.to_string() }).to_string().into_bytes())
}

/// Opens the library in `data_dir` (UTF-8). Null on failure; `error` (if not
/// null) then receives the reason, to be freed with `inkleaf_free`.
///
/// # Safety
/// `data_dir` must be a valid NUL-terminated string; `error` null or writable.
#[no_mangle]
pub unsafe extern "C" fn inkleaf_open(data_dir: *const c_char, error: *mut InkleafResult) -> *mut Core {
    let dir = match CStr::from_ptr(data_dir).to_str() {
        Ok(d) => d,
        Err(e) => {
            if !error.is_null() {
                *error = error_result(e);
            }
            return std::ptr::null_mut();
        }
    };
    match Core::open(Path::new(dir)) {
        Ok(c) => Box::into_raw(Box::new(c)),
        Err(e) => {
            if !error.is_null() {
                *error = error_result(e);
            }
            std::ptr::null_mut()
        }
    }
}

/// Runs `cmd` with JSON `args` (UTF-8, may be null for none) and optional
/// raw bytes. Safe to call from several threads at once.
///
/// # Safety
/// `core` from `inkleaf_open`; `cmd` and `args` NUL-terminated; `bytes`
/// null or `len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn inkleaf_call(core: *const Core, cmd: *const c_char, args: *const c_char, bytes: *const u8, len: usize) -> InkleafResult {
    let core = &*core;
    let cmd = CStr::from_ptr(cmd).to_string_lossy();
    let args: Value = if args.is_null() {
        Value::Null
    } else {
        match serde_json::from_str(&CStr::from_ptr(args).to_string_lossy()) {
            Ok(v) => v,
            Err(e) => return error_result(format!("bad arguments: {e}")),
        }
    };
    let bytes = if bytes.is_null() { &[][..] } else { std::slice::from_raw_parts(bytes, len) };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| core.call(&cmd, &args, bytes))) {
        Ok(Ok(Answer::Json(v))) => result(true, true, v.to_string().into_bytes()),
        Ok(Ok(Answer::Bytes(b))) => result(true, false, b),
        Ok(Err(e)) => error_result(e),
        Err(_) => error_result(format!("{cmd} failed unexpectedly")),
    }
}

/// Asks the AI (`args`: `{ "request": AskRequest }`) with the answer streamed:
/// `on_text(user, text, len)` gets each new piece of UTF-8 text as it arrives,
/// on this thread, and returns false to stop. The result is the whole answer
/// (JSON string) or the error.
///
/// # Safety
/// `core` from `inkleaf_open`; `args` NUL-terminated; `on_text` callable with `user`.
#[no_mangle]
pub unsafe extern "C" fn inkleaf_ai_stream(
    core: *const Core,
    args: *const c_char,
    on_text: extern "C" fn(user: *mut std::ffi::c_void, text: *const u8, len: usize) -> bool,
    user: *mut std::ffi::c_void,
) -> InkleafResult {
    let core = &*core;
    let args: Value = match serde_json::from_str(&CStr::from_ptr(args).to_string_lossy()) {
        Ok(v) => v,
        Err(e) => return error_result(format!("bad arguments: {e}")),
    };
    let req: AskRequest = match args.get("request").cloned().map(serde_json::from_value) {
        Some(Ok(r)) => r,
        _ => return error_result("aiStream: a request"),
    };
    let run = || core.ai.ask_stream(&req, &mut |t: &str| on_text(user, t.as_ptr(), t.len()));
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(run)) {
        Ok(Ok(text)) => result(true, true, Value::String(text).to_string().into_bytes()),
        Ok(Err(e)) => error_result(e),
        Err(_) => error_result("the AI request failed unexpectedly"),
    }
}

/// Frees a result's data.
///
/// # Safety
/// `r` must come from `inkleaf_call` or `inkleaf_open` and be freed once.
#[no_mangle]
pub unsafe extern "C" fn inkleaf_free(r: InkleafResult) {
    if !r.data.is_null() {
        drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(r.data, r.len)));
    }
}

/// Closes the library.
///
/// # Safety
/// `core` from `inkleaf_open`, not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn inkleaf_close(core: *mut Core) {
    if !core.is_null() {
        drop(Box::from_raw(core));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn commands_work_end_to_end() {
        let dir = std::env::temp_dir().join(format!("inkleaf-native-{}", inkleaf_core::db::new_id()));
        let core = Core::open(&dir).unwrap();
        let json = |cmd: &str, args: Value| match core.call(cmd, &args, &[]).unwrap() {
            Answer::Json(v) => v,
            Answer::Bytes(_) => panic!("bytes"),
        };
        let nb = json("createNotebook", json!({ "input": { "title": "Maths", "folderId": null, "coverColor": "#2563eb", "template": "ruled", "paperColor": "#ffffff" } }));
        let pages = json("pages", json!({ "notebookId": nb["id"] }));
        let page = pages[0]["id"].as_str().unwrap().to_string();
        json("saveElements", json!({ "pageId": page, "elements": [{ "id": "e1", "kind": "stroke", "z": 0, "data": { "points": [[1, 2, 0.5]] } }] }));
        let els = json("elements", json!({ "pageId": page }));
        assert_eq!(els[0]["kind"], "stroke");
        assert_eq!(json("library", Value::Null)["notebooks"][0]["title"], "Maths");
        assert!(core.call("nope", &Value::Null, &[]).is_err());
        assert!(core.call("notebook", &json!({}), &[]).is_err());
        std::fs::remove_dir_all(dir).ok();
    }
}
