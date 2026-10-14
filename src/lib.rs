//! Standalone imgui-smash NRO. The public wrapper at
//! bb47b205ed0be2fb864269ee2d37fc886b81221e is adapted only enough to drop
//! imgui-api, prefix this NRO's bootstrap hook, and link a caller-supplied
//! static archive. Host builds do not create an ImGui context.

#[cfg(not(target_os = "switch"))]
pub const HOST_CREATES_IMGUI_CONTEXT: bool = false;

#[cfg(target_os = "switch")]
mod plugin {
    use skyline::hook;
    use skyline::libc::{c_char, c_void};
    use std::sync::Mutex;

    #[link(name = "imgui_smash", kind = "static")]
    unsafe extern "C" {
        fn imgui_smash_init(post_init: *const c_void, render: *const c_void);
        fn imgui_smash_bootstrap_hook(
            name: *const c_char,
            original_fn: *const c_void,
        ) -> *const c_void;
        fn imgui_smash_add_on_pre_init(pre_init: *const c_void);
        fn imgui_smash_add_on_new_frame(new_frame: *const c_void);
        fn imgui_smash_add_on_draw_frame(new_frame: *const c_void);
        fn imgui_smash_show_mouse(show_mouse: bool);
        fn imgui_smash_context_setup(callback: *const c_void);
        // ImGui::GetCurrentContext(), which cimgui's igGetCurrentContext wraps.
        // The Skyline target links with --no-gc-sections, so referencing any
        // cimgui symbol would keep all of cimgui.o (every ig* wrapper and the
        // code they reach) in this NRO although nothing here calls them.
        #[link_name = "_ZN5ImGui17GetCurrentContextEv"]
        fn igGetCurrentContext() -> *mut c_void;
    }

    #[cfg(feature = "frame-guard-stats")]
    #[link(name = "imgui_smash", kind = "static")]
    unsafe extern "C" {
        fn imgui_smash_frame_guard_stats(out: *mut FrameGuardStats) -> i32;
    }

    #[cfg(feature = "draw-stats")]
    #[link(name = "imgui_smash", kind = "static")]
    unsafe extern "C" {
        fn imgui_smash_draw_stats(out: *mut DrawStats) -> i32;
    }

    #[cfg(feature = "draw-stats")]
    const DRAW_STATS_LAYOUT_BYTES: u32 = 72;
    #[cfg(feature = "draw-stats")]
    const DRAW_STATS_STATUS_FILE: &str = "imgui-renderer-status.txt";
    #[cfg(feature = "draw-stats")]
    const DRAW_STATS_DATA_DIR: &str = "sd:/ultimate/ssbu_online_deluxe_rewrite";

    #[cfg(feature = "draw-stats")]
    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    struct DrawStats {
        version: u32,
        size: u32,
        submitted: u64,
        reused: u64,
        pending_skips: u64,
        failed: u64,
        max_command_bytes: u64,
        max_control_bytes: u64,
        geometry_skips: u64,
        command_overflow_skips: u64,
    }

    #[cfg(feature = "draw-stats")]
    const _: () = {
        assert!(std::mem::size_of::<DrawStats>() == DRAW_STATS_LAYOUT_BYTES as usize);
        assert!(same_archive(
            "8a06be16dbfd50d10bac81ee5bdd2ffa9cd3472773151cc45f1c2101541f8f7c",
            env!("IMGUI_SMASH_ARCHIVE_SHA256"),
        ));
    };

    #[cfg(feature = "frame-guard-stats")]
    const FRAME_GUARD_LAYOUT_VERSION: u32 = 1;
    #[cfg(feature = "frame-guard-stats")]
    const FRAME_GUARD_LAYOUT_BYTES: u32 = 24;
    #[cfg(feature = "frame-guard-stats")]
    const FRAME_GUARD_STATUS_CAP: usize = 512;
    #[cfg(feature = "frame-guard-stats")]
    const FRAME_GUARD_DATA_DIR: &str = "sd:/ultimate/ssbu_online_deluxe_rewrite";
    #[cfg(feature = "frame-guard-stats")]
    const FRAME_GUARD_STATUS_FILE: &str = "frame-guard-status.txt";
    #[cfg(feature = "frame-guard-stats")]
    const PINNED_ARCHIVE_SHA256: &str =
        "8a06be16dbfd50d10bac81ee5bdd2ffa9cd3472773151cc45f1c2101541f8f7c";

    #[cfg(feature = "frame-guard-stats")]
    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    struct FrameGuardStats {
        version: u32,
        size: u32,
        accepted: u64,
        busy_skips: u64,
    }

    #[cfg(any(feature = "frame-guard-stats", feature = "draw-stats"))]
    const fn same_archive(left: &str, right: &str) -> bool {
        let left = left.as_bytes();
        let right = right.as_bytes();
        if left.len() != right.len() {
            return false;
        }
        let mut index = 0;
        while index < left.len() {
            if left[index] != right[index] {
                return false;
            }
            index += 1;
        }
        true
    }

    #[cfg(feature = "frame-guard-stats")]
    const _: () = {
        assert!(std::mem::size_of::<FrameGuardStats>() == FRAME_GUARD_LAYOUT_BYTES as usize);
        assert!(same_archive(
            PINNED_ARCHIVE_SHA256,
            env!("IMGUI_SMASH_ARCHIVE_SHA256"),
        ));
    };

    unsafe extern "C" {
        fn nvnBootstrapLoader(name: *const c_char) -> *const c_void;
    }

    pub type SetupContextFn = unsafe extern "C" fn(*mut u64);

    static POST_INIT_FUNCS: Mutex<Vec<SetupContextFn>> = Mutex::new(Vec::new());

    /// Digest of the static archive this NRO was linked against.
    static LINKED_ARCHIVE_SHA256: &str = env!("IMGUI_SMASH_ARCHIVE_SHA256");

    unsafe extern "C" fn imgui_post_context() {
        let context = igGetCurrentContext();
        let funcs = POST_INIT_FUNCS.lock().unwrap();
        for callback in funcs.iter() {
            callback(context.cast());
        }
    }

    #[no_mangle]
    pub extern "C" fn imgui_smash_show_mouse_wrapper(show_mouse: bool) {
        unsafe { imgui_smash_show_mouse(show_mouse) }
    }

    #[no_mangle]
    pub extern "C" fn imgui_smash_add_on_pre_init_wrapper(pre_init: *const c_void) {
        unsafe { imgui_smash_add_on_pre_init(pre_init) }
    }

    #[no_mangle]
    pub extern "C" fn imgui_smash_add_on_new_frame_wrapper(new_frame: *const c_void) {
        unsafe { imgui_smash_add_on_new_frame(new_frame) }
    }

    #[no_mangle]
    pub extern "C" fn imgui_smash_add_on_draw_frame_wrapper(new_frame: *const c_void) {
        unsafe { imgui_smash_add_on_draw_frame(new_frame) }
    }

    /// Kept so a plugin that imports this upstream export still resolves it.
    /// The archive is built with IMGUI_DISABLE_DEMO_WINDOWS, so the demo is
    /// not linked and this draws nothing.
    #[no_mangle]
    pub extern "C" fn imgui_show_demo_window_wrapper(_open: *const bool) {}

    #[no_mangle]
    pub unsafe extern "C" fn imgui_setup_context_export(func: SetupContextFn) {
        POST_INIT_FUNCS.lock().unwrap().push(func);
    }

    #[no_mangle]
    pub unsafe extern "C" fn imgui_get_context_export() -> *mut u64 {
        igGetCurrentContext().cast()
    }

    #[hook(replace = nvnBootstrapLoader)]
    unsafe fn standalone_host_nvn_bootstrap_hook(name: *const c_char) -> *const c_void {
        imgui_smash_bootstrap_hook(name, original!() as *const c_void)
    }

    #[skyline::main(name = "imgui")]
    pub fn main() {
        std::panic::set_hook(Box::new(|info| {
            let location = info.location().unwrap();

            let msg = match info.payload().downcast_ref::<&'static str>() {
                Some(text) => *text,
                None => match info.payload().downcast_ref::<String>() {
                    Some(text) => &text[..],
                    None => "Box<Any>",
                },
            };

            let err_msg = format!("imgui-smash has panicked at '{msg}', {location}");
            skyline::error::show_error(
                69,
                "imgui-smash has panicked! Please open the details and send a screenshot to the developer, then close the game.\n\0",
                err_msg.as_str(),
            );
        }));

        skyline::install_hook!(standalone_host_nvn_bootstrap_hook);

        unsafe {
            imgui_smash_context_setup(imgui_post_context as *const c_void);
            imgui_smash_init(std::ptr::null(), std::ptr::null());
        }
        #[cfg(feature = "frame-guard-stats")]
        start_frame_guard_logger();
        #[cfg(feature = "draw-stats")]
        start_draw_stats_logger();
        let _ = std::hint::black_box(LINKED_ARCHIVE_SHA256);
    }

    #[cfg(feature = "frame-guard-stats")]
    fn start_frame_guard_logger() {
        if let Ok(handle) = std::thread::Builder::new()
            .name("standalone-frame-guard".into())
            .spawn(frame_guard_logger)
        {
            drop(handle);
        }
    }

    #[cfg(feature = "frame-guard-stats")]
    fn frame_guard_logger() {
        loop {
            std::thread::sleep(std::time::Duration::from_secs(2));
            publish_frame_guard();
        }
    }

    #[cfg(feature = "frame-guard-stats")]
    fn publish_frame_guard() {
        if !frame_guard_dir_present() {
            return;
        }
        let mut stats = FrameGuardStats::default();
        let wrote = unsafe { imgui_smash_frame_guard_stats(&mut stats) };
        if wrote != 1 {
            return;
        }
        let (buf, len) = format_frame_guard(stats);
        overwrite_frame_guard(&buf[..len]);
    }

    #[cfg(feature = "frame-guard-stats")]
    fn format_frame_guard(stats: FrameGuardStats) -> ([u8; FRAME_GUARD_STATUS_CAP], usize) {
        use std::io::{Cursor, Write};
        let mut buf = [0u8; FRAME_GUARD_STATUS_CAP];
        let mut cursor = Cursor::new(&mut buf[..]);
        let _ = if stats.version == FRAME_GUARD_LAYOUT_VERSION
            && stats.size == FRAME_GUARD_LAYOUT_BYTES
        {
            write!(
                cursor,
                "archive_sha256={PINNED_ARCHIVE_SHA256}\nversion={}\nsize={}\naccepted={}\nbusy_skips={}\n",
                stats.version, stats.size, stats.accepted, stats.busy_skips,
            )
        } else {
            write!(
                cursor,
                "archive_sha256={PINNED_ARCHIVE_SHA256}\nversion={}\nsize={}\naccepted=0\n",
                stats.version, stats.size,
            )
        };
        let len = usize::try_from(cursor.position()).unwrap_or(0);
        (buf, len.min(FRAME_GUARD_STATUS_CAP))
    }

    #[cfg(feature = "frame-guard-stats")]
    fn frame_guard_dir_present() -> bool {
        let bytes = FRAME_GUARD_DATA_DIR.as_bytes();
        let mut raw = [0u8; 64];
        if bytes.len() >= raw.len() {
            return false;
        }
        raw[..bytes.len()].copy_from_slice(bytes);
        let mut kind = 0u32;
        let rc = unsafe { skyline::nn::fs::GetEntryType(&mut kind, raw.as_ptr()) };
        rc == 0 && kind == skyline::nn::fs::DirectoryEntryType_DirectoryEntryType_Directory
    }

    #[cfg(feature = "frame-guard-stats")]
    fn overwrite_frame_guard(bytes: &[u8]) {
        use std::io::Write;
        let path = std::path::Path::new(FRAME_GUARD_DATA_DIR).join(FRAME_GUARD_STATUS_FILE);
        let Ok(mut file) = std::fs::File::create(&path) else {
            return;
        };
        if file.write_all(bytes).is_ok() {
            let _ = file.set_len(u64::try_from(bytes.len()).unwrap_or(0));
            let _ = file.sync_all();
        }
    }

    #[cfg(feature = "draw-stats")]
    fn start_draw_stats_logger() {
        if let Ok(handle) = std::thread::Builder::new()
            .name("standalone-draw-stats".into())
            .spawn(draw_stats_logger)
        {
            drop(handle);
        }
    }

    #[cfg(feature = "draw-stats")]
    fn draw_stats_logger() {
        loop {
            std::thread::sleep(std::time::Duration::from_secs(2));
            publish_draw_stats();
        }
    }

    #[cfg(feature = "draw-stats")]
    fn publish_draw_stats() {
        if !draw_stats_dir_present() {
            return;
        }
        let mut stats = DrawStats::default();
        let wrote = unsafe { imgui_smash_draw_stats(&mut stats) };
        if wrote != 1 {
            return;
        }
        let (buf, len) = format_draw_stats(stats);
        overwrite_draw_stats(&buf[..len]);
    }

    #[cfg(feature = "draw-stats")]
    fn format_draw_stats(stats: DrawStats) -> ([u8; 512], usize) {
        use std::io::{Cursor, Write};
        let mut buf = [0u8; 512];
        let mut cursor = Cursor::new(&mut buf[..]);
        let digest = "8a06be16dbfd50d10bac81ee5bdd2ffa9cd3472773151cc45f1c2101541f8f7c";
        let _ = if stats.version == 1 && stats.size == DRAW_STATS_LAYOUT_BYTES {
            write!(
                cursor,
                "archive_sha256={digest}\nversion={}\nsize={}\nsubmitted={}\nreused={}\npending_skips={}\nfailed={}\nmax_command_bytes={}\nmax_control_bytes={}\ngeometry_skips={}\ncommand_overflow_skips={}\n",
                stats.version,
                stats.size,
                stats.submitted,
                stats.reused,
                stats.pending_skips,
                stats.failed,
                stats.max_command_bytes,
                stats.max_control_bytes,
                stats.geometry_skips,
                stats.command_overflow_skips,
            )
        } else {
            write!(
                cursor,
                "archive_sha256={digest}\nversion={}\nsize={}\naccepted=0\n",
                stats.version, stats.size,
            )
        };
        let len = usize::try_from(cursor.position()).unwrap_or(0);
        (buf, len.min(512))
    }

    #[cfg(feature = "draw-stats")]
    fn draw_stats_dir_present() -> bool {
        let bytes = DRAW_STATS_DATA_DIR.as_bytes();
        let mut raw = [0u8; 64];
        if bytes.len() >= raw.len() {
            return false;
        }
        raw[..bytes.len()].copy_from_slice(bytes);
        let mut kind = 0u32;
        let rc = unsafe { skyline::nn::fs::GetEntryType(&mut kind, raw.as_ptr()) };
        rc == 0 && kind == skyline::nn::fs::DirectoryEntryType_DirectoryEntryType_Directory
    }

    #[cfg(feature = "draw-stats")]
    fn overwrite_draw_stats(bytes: &[u8]) {
        use std::io::Write;
        let path = std::path::Path::new(DRAW_STATS_DATA_DIR).join(DRAW_STATS_STATUS_FILE);
        let Ok(mut file) = std::fs::File::create(&path) else {
            return;
        };
        if file.write_all(bytes).is_ok() {
            let _ = file.set_len(u64::try_from(bytes.len()).unwrap_or(0));
            let _ = file.sync_all();
        }
    }
}
