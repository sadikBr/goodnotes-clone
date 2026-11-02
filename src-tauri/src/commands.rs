//! Tauri command layer: thin wrappers that expose the database and ink
//! geometry to the webview frontend.

use std::path::PathBuf;

use tauri::{AppHandle, Manager, State};

use crate::db::{
    Db, Element, Folder, Library, NewNotebook, NewPdfNotebook, Notebook, Page, SearchHit,
    TrashContents,
};
use crate::error::{AppError, AppResult};
use crate::export::{self, ExportPage, Format};
use crate::handwriting::{self, InkStroke, Recognition};
use crate::ink::{self, InkPoint};

#[tauri::command]
pub fn library(db: State<'_, Db>) -> AppResult<Library> {
    db.library()
}

#[tauri::command]
pub fn trash(db: State<'_, Db>) -> AppResult<TrashContents> {
    db.trash()
}

#[tauri::command]
pub fn empty_trash(db: State<'_, Db>) -> AppResult<()> {
    db.empty_trash()
}

#[tauri::command]
pub fn create_folder(
    db: State<'_, Db>,
    name: String,
    parent_id: Option<String>,
    color: Option<String>,
) -> AppResult<Folder> {
    db.create_folder(&name, parent_id.as_deref(), color.as_deref())
}

#[tauri::command]
pub fn rename_folder(db: State<'_, Db>, id: String, name: String) -> AppResult<()> {
    db.rename_folder(&id, &name)
}

#[tauri::command]
pub fn move_folder(db: State<'_, Db>, id: String, parent_id: Option<String>) -> AppResult<()> {
    db.move_folder(&id, parent_id.as_deref())
}

#[tauri::command]
pub fn set_folder_trashed(db: State<'_, Db>, id: String, trashed: bool) -> AppResult<()> {
    db.set_folder_trashed(&id, trashed)
}

#[tauri::command]
pub fn delete_folder(db: State<'_, Db>, id: String) -> AppResult<()> {
    db.delete_folder(&id)
}

#[tauri::command]
pub fn set_folder_favourite(db: State<'_, Db>, id: String, favourite: bool) -> AppResult<()> {
    db.set_folder_favourite(&id, favourite)
}

#[tauri::command]
pub fn create_notebook(db: State<'_, Db>, input: NewNotebook) -> AppResult<Notebook> {
    db.create_notebook(&input)
}

#[tauri::command]
pub fn notebook(db: State<'_, Db>, id: String) -> AppResult<Notebook> {
    db.notebook(&id)
}

#[tauri::command]
pub fn rename_notebook(db: State<'_, Db>, id: String, title: String) -> AppResult<()> {
    db.rename_notebook(&id, &title)
}

#[tauri::command]
pub fn move_notebook(db: State<'_, Db>, id: String, folder_id: Option<String>) -> AppResult<()> {
    db.move_notebook(&id, folder_id.as_deref())
}

#[tauri::command]
pub fn set_notebook_cover(db: State<'_, Db>, id: String, color: String) -> AppResult<()> {
    db.set_notebook_cover(&id, &color)
}

#[tauri::command]
pub fn set_notebook_trashed(db: State<'_, Db>, id: String, trashed: bool) -> AppResult<()> {
    db.set_notebook_trashed(&id, trashed)
}

#[tauri::command]
pub fn delete_notebook(db: State<'_, Db>, id: String) -> AppResult<()> {
    db.delete_notebook(&id)
}

#[tauri::command]
pub fn set_notebook_favourite(db: State<'_, Db>, id: String, favourite: bool) -> AppResult<()> {
    db.set_notebook_favourite(&id, favourite)
}

#[tauri::command]
pub fn duplicate_notebook(db: State<'_, Db>, id: String) -> AppResult<Notebook> {
    db.duplicate_notebook(&id)
}

#[tauri::command]
pub fn pages(db: State<'_, Db>, notebook_id: String) -> AppResult<Vec<Page>> {
    db.pages(&notebook_id)
}

#[tauri::command]
pub fn add_page(
    db: State<'_, Db>,
    notebook_id: String,
    index: Option<i64>,
    template: Option<String>,
) -> AppResult<Page> {
    db.add_page(&notebook_id, index, template.as_deref())
}

#[tauri::command]
pub fn duplicate_page(db: State<'_, Db>, page_id: String) -> AppResult<Page> {
    db.duplicate_page(&page_id)
}

#[tauri::command]
pub fn delete_page(db: State<'_, Db>, page_id: String) -> AppResult<()> {
    db.delete_page(&page_id)
}

#[tauri::command]
pub fn move_page(db: State<'_, Db>, page_id: String, new_index: i64) -> AppResult<()> {
    db.move_page(&page_id, new_index)
}

#[tauri::command]
pub fn set_page_template(
    db: State<'_, Db>,
    page_id: String,
    template: String,
    paper_color: Option<String>,
) -> AppResult<()> {
    db.set_page_template(&page_id, &template, paper_color.as_deref())
}

#[tauri::command]
pub fn set_page_favourite(db: State<'_, Db>, page_id: String, favourite: bool) -> AppResult<()> {
    db.set_page_favourite(&page_id, favourite)
}

#[tauri::command]
pub fn set_page_thumbnail(db: State<'_, Db>, page_id: String, thumbnail: String) -> AppResult<()> {
    db.set_page_thumbnail(&page_id, &thumbnail)
}

#[tauri::command]
pub fn elements(db: State<'_, Db>, page_id: String) -> AppResult<Vec<Element>> {
    db.elements(&page_id)
}

#[tauri::command]
pub fn save_elements(db: State<'_, Db>, page_id: String, elements: Vec<Element>) -> AppResult<()> {
    db.save_elements(&page_id, &elements)
}

#[tauri::command]
pub fn search(db: State<'_, Db>, query: String) -> AppResult<Vec<SearchHit>> {
    db.search(&query)
}

#[tauri::command]
pub fn simplify_stroke(points: Vec<InkPoint>, epsilon: f64) -> Vec<InkPoint> {
    ink::simplify(&points, epsilon)
}

/// Stores an imported PDF. The bytes arrive as the raw request body (not
/// JSON) so large files stay fast. Returns the stored asset's id.
#[tauri::command]
pub fn store_pdf_asset(db: State<'_, Db>, request: tauri::ipc::Request<'_>) -> AppResult<String> {
    match request.body() {
        tauri::ipc::InvokeBody::Raw(bytes) => db.store_pdf_asset(bytes),
        _ => Err(AppError::Invalid("expected the PDF as raw bytes".into())),
    }
}

#[tauri::command]
pub fn create_pdf_notebook(db: State<'_, Db>, input: NewPdfNotebook) -> AppResult<Notebook> {
    db.create_pdf_notebook(&input)
}

/// Returns a stored PDF as raw bytes (an ArrayBuffer in the webview).
#[tauri::command]
pub fn asset_bytes(db: State<'_, Db>, id: String) -> AppResult<tauri::ipc::Response> {
    Ok(tauri::ipc::Response::new(db.asset_bytes(&id)?))
}

/// Recognises handwritten strokes as text using the OS recognizer (Windows).
/// Runs off the main thread because recognition blocks until it finishes.
#[tauri::command]
pub async fn recognize_handwriting(strokes: Vec<InkStroke>) -> Result<Recognition, String> {
    tauri::async_runtime::spawn_blocking(move || handwriting::recognize(&strokes))
        .await
        .map_err(|e| format!("recognition task failed: {e}"))?
}

/// Suggested save location for an export: `~/Documents/Inkleaf Exports/<name>.<ext>`.
/// The folder is created so the save dialog can open inside it.
#[tauri::command]
pub fn default_export_dir(app: AppHandle, file_name: String, format: String) -> AppResult<String> {
    let format = Format::parse(&format)?;
    let dir = app
        .path()
        .document_dir()
        .or_else(|_| app.path().home_dir())
        .map_err(|e| AppError::Invalid(format!("no documents directory: {e}")))?
        .join("Inkleaf Exports");
    std::fs::create_dir_all(&dir)?;
    Ok(dir
        .join(sanitize_file_name(&file_name, format.extension()))
        .to_string_lossy()
        .into_owned())
}

/// Builds a PDF, PPTX or PNG from rendered pages and writes it to `path`
/// (chosen by the user in the native save dialog). Returns the final path.
#[tauri::command]
pub fn export_document(path: String, format: String, pages: Vec<ExportPage>) -> AppResult<String> {
    let format = Format::parse(&format)?;
    let path = with_extension(PathBuf::from(path), format.extension());
    if path.file_name().is_none() {
        return Err(AppError::Invalid("choose a file name to export to".into()));
    }
    let bytes = export::build(format, &pages)?;
    std::fs::write(&path, bytes)?;
    Ok(path.to_string_lossy().into_owned())
}

/// Writes a PDF built in the webview (vector export) to `path`, chosen in the
/// native save dialog. The file is the raw request body; the path travels in
/// the `x-export-path` header (URI-encoded). Returns the final path.
#[tauri::command]
pub fn save_pdf_export(request: tauri::ipc::Request<'_>) -> AppResult<String> {
    let path = request
        .headers()
        .get("x-export-path")
        .and_then(|v| v.to_str().ok())
        .and_then(percent_decode)
        .ok_or_else(|| AppError::Invalid("missing export path".into()))?;
    let tauri::ipc::InvokeBody::Raw(bytes) = request.body() else {
        return Err(AppError::Invalid("expected the PDF as raw bytes".into()));
    };
    if !bytes.starts_with(b"%PDF-") {
        return Err(AppError::Invalid("export produced an invalid PDF".into()));
    }
    let path = with_extension(PathBuf::from(path), "pdf");
    if path.file_name().is_none() {
        return Err(AppError::Invalid("choose a file name to export to".into()));
    }
    std::fs::write(&path, bytes)?;
    Ok(path.to_string_lossy().into_owned())
}

/// Decodes `%XX` escapes (as produced by encodeURIComponent).
fn percent_decode(s: &str) -> Option<String> {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' {
            let hex = std::str::from_utf8(b.get(i + 1..i + 3)?).ok()?;
            out.push(u8::from_str_radix(hex, 16).ok()?);
            i += 3;
        } else {
            out.push(b[i]);
            i += 1;
        }
    }
    String::from_utf8(out).ok()
}

/// Ensures the path ends in `.ext` (case-insensitive), appending it otherwise.
fn with_extension(path: PathBuf, ext: &str) -> PathBuf {
    let matches = path
        .extension()
        .map(|e| e.to_string_lossy().eq_ignore_ascii_case(ext))
        .unwrap_or(false);
    if matches {
        path
    } else {
        let mut s = path.into_os_string();
        s.push(format!(".{ext}"));
        PathBuf::from(s)
    }
}

fn sanitize_file_name(name: &str, ext: &str) -> String {
    let cleaned: String = name
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || " -_.".contains(c) {
                c
            } else {
                '_'
            }
        })
        .collect();
    let cleaned = cleaned.trim().trim_start_matches('.').to_string();
    let base = if cleaned.is_empty() {
        "export".to_string()
    } else {
        cleaned
    };
    if base.to_lowercase().ends_with(&format!(".{ext}")) {
        base
    } else {
        format!("{base}.{ext}")
    }
}

#[cfg(test)]
mod tests {
    use super::{percent_decode, sanitize_file_name, with_extension};
    use std::path::PathBuf;

    #[test]
    fn export_paths_are_uri_decoded() {
        assert_eq!(
            percent_decode("%2Fhome%2Fme%2FBiologie%20%C3%A9t%C3%A9.pdf").as_deref(),
            Some("/home/me/Biologie été.pdf")
        );
        assert_eq!(percent_decode("C%3A%5CUsers%5Cx.pdf").as_deref(), Some("C:\\Users\\x.pdf"));
        assert_eq!(percent_decode("bad%2"), None);
        assert_eq!(percent_decode("bad%zz"), None);
    }

    #[test]
    fn file_names_cannot_escape_the_export_directory() {
        assert_eq!(
            sanitize_file_name("../../etc/passwd", "png"),
            "_.._etc_passwd.png"
        );
        assert_eq!(
            sanitize_file_name("Lecture 1 - page 2", "pdf"),
            "Lecture 1 - page 2.pdf"
        );
        assert_eq!(sanitize_file_name("", "pptx"), "export.pptx");
        assert_eq!(sanitize_file_name("a.PNG", "png"), "a.PNG");
    }

    #[test]
    fn export_paths_get_the_right_extension() {
        assert_eq!(
            with_extension(PathBuf::from("/tmp/notes"), "pdf"),
            PathBuf::from("/tmp/notes.pdf")
        );
        assert_eq!(
            with_extension(PathBuf::from("/tmp/notes.PDF"), "pdf"),
            PathBuf::from("/tmp/notes.PDF")
        );
        assert_eq!(
            with_extension(PathBuf::from("/tmp/notes.png"), "pptx"),
            PathBuf::from("/tmp/notes.png.pptx")
        );
    }
}
