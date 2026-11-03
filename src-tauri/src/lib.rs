pub mod backup;
pub mod commands;
pub mod db;
pub mod error;
pub mod export;
pub mod handwriting;
pub mod ink;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let builder = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init());
    // In-app updates are for the desktop; Android updates come as a new APK.
    #[cfg(desktop)]
    let builder = builder
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_process::init());
    builder
        .setup(|app| {
            let dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&dir)?;
            let database = db::Db::open(&dir.join("inkleaf.db"))?;
            app.manage(database);
            app.manage(backup::Backups::load(dir.join("backup.json")));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::library,
            commands::trash,
            commands::empty_trash,
            commands::create_folder,
            commands::rename_folder,
            commands::move_folder,
            commands::set_folder_trashed,
            commands::delete_folder,
            commands::set_folder_favourite,
            commands::set_folder_color,
            commands::create_notebook,
            commands::notebook,
            commands::rename_notebook,
            commands::move_notebook,
            commands::set_notebook_cover,
            commands::set_notebook_meta,
            commands::set_page_section,
            commands::set_notebook_trashed,
            commands::delete_notebook,
            commands::set_notebook_favourite,
            commands::duplicate_notebook,
            commands::pages,
            commands::add_page,
            commands::duplicate_page,
            commands::delete_page,
            commands::move_page,
            commands::set_page_template,
            commands::set_page_favourite,
            commands::set_page_thumbnail,
            commands::elements,
            commands::save_elements,
            commands::search,
            commands::simplify_stroke,
            commands::default_export_dir,
            commands::export_document,
            commands::save_pdf_export,
            commands::recognize_handwriting,
            commands::store_pdf_asset,
            commands::create_pdf_notebook,
            commands::asset_bytes,
            commands::backup_status,
            commands::set_backup_settings,
            commands::backup_now,
            commands::backup_if_due,
            commands::export_library,
            commands::inspect_backup,
            commands::restore_backup,
        ])
        .run(tauri::generate_context!())
        .expect("error while running Inkleaf");
}
