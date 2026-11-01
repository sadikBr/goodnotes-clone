pub mod commands;
pub mod db;
pub mod error;
pub mod export;
pub mod ink;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&dir)?;
            let database = db::Db::open(&dir.join("inkleaf.db"))?;
            app.manage(database);
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
            commands::create_notebook,
            commands::notebook,
            commands::rename_notebook,
            commands::move_notebook,
            commands::set_notebook_cover,
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
            commands::recognize_shape,
            commands::simplify_stroke,
            commands::default_export_dir,
            commands::export_document,
        ])
        .run(tauri::generate_context!())
        .expect("error while running Inkleaf");
}
