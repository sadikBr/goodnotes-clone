// Prevents an extra console window on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    // WebKitGTK's DMA-BUF renderer crashes on several Wayland setups
    // (Hyprland/Omarchy, NVIDIA) with "Error 71 (Protocol error) dispatching
    // to Wayland display". Use the shared-memory renderer instead unless the
    // user chose otherwise. Must run before any other thread starts.
    #[cfg(target_os = "linux")]
    if std::env::var_os("WEBKIT_DISABLE_DMABUF_RENDERER").is_none() {
        std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }
    inkleaf_lib::run()
}
