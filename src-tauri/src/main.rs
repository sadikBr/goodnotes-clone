// Prevents an extra console window on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    #[cfg(target_os = "linux")]
    choose_webkit_renderer();
    inkleaf_lib::run()
}

/// Picks how WebKitGTK hands frames to the screen. Must run before any other
/// thread starts. Anything the user set themselves is left alone.
#[cfg(target_os = "linux")]
fn choose_webkit_renderer() {
    use std::env;
    use std::path::Path;
    if env::var_os("WEBKIT_DISABLE_DMABUF_RENDERER").is_some() {
        return;
    }
    let wayland = env::var_os("WAYLAND_DISPLAY").is_some()
        || env::var("XDG_SESSION_TYPE").is_ok_and(|v| v == "wayland");
    let nvidia = Path::new("/proc/driver/nvidia/version").exists() || Path::new("/sys/module/nvidia_drm").exists();
    if wayland && nvidia {
        // NVIDIA on Wayland: the GPU (DMA-BUF) renderer is by far the
        // smoothest, but with the driver's explicit sync it crashes with
        // "Error 71 (Protocol error) dispatching to Wayland display". Keep
        // the GPU renderer and turn explicit sync off instead.
        if env::var_os("__NV_DISABLE_EXPLICIT_SYNC").is_none() {
            env::set_var("__NV_DISABLE_EXPLICIT_SYNC", "1");
        }
    } else if wayland {
        // Other Wayland setups have also shown that crash; the shared-memory
        // renderer is slower but safe.
        env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }
}
