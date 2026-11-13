//! Standalone imgui-smash NRO. The public wrapper at
//! bb47b205ed0be2fb864269ee2d37fc886b81221e is adapted only enough to drop
//! imgui-api, prefix this NRO's bootstrap hook, and link a caller-supplied
//! static archive. Since 1.0.2 it also reads `config.ini` for the L3 + R3
//! gamepad-navigation toggle (`config`). Since 1.1.0 the archive is the one
//! Online Deluxe links. Since 1.2.0 the fonts index glyphs in pages when every
//! client declared a paged reader (`clients`), and the archive's counters are
//! exported for a client that draws through this host. Host builds do not
//! create an ImGui context.

pub mod clients;
pub mod config;

#[cfg(not(target_os = "switch"))]
pub const HOST_CREATES_IMGUI_CONTEXT: bool = false;

#[cfg(test)]
mod tests {
    #[test]
    fn version_is_the_release() {
        // 1.1.0: the archive Online Deluxe links (shared fonts, paged index) plus
        // 1.0.2's texture-table guard and L3 + R3 toggle behind config.ini.
        // 1.2.0: the same archive; the paged index when every client reads it,
        // and the archive's counters exported.
        assert_eq!(env!("CARGO_PKG_VERSION"), "1.2.0");
    }
}

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
        fn imgui_smash_set_gamepad_nav_toggle(enabled: bool);
        fn imgui_smash_font_use_paged_index(enabled: i32) -> i32;
        // The archive's counters (include/imgui_smash_font_stats.h,
        // imgui_smash_draw_stats.h, src/imgui_backend/frame_guard.hpp).
        fn imgui_smash_font_stats(out: *mut c_void) -> i32;
        fn imgui_smash_font_dynamic_set(out: *mut u32, cap: u32) -> i32;
        fn imgui_smash_draw_stats(out: *mut c_void) -> i32;
        fn imgui_smash_frame_guard_stats(out: *mut c_void) -> i32;
        // ImGui::GetCurrentContext(), which cimgui's igGetCurrentContext wraps.
        // The Skyline target links with --no-gc-sections, so referencing any
        // cimgui symbol would keep all of cimgui.o (every ig* wrapper and the
        // code they reach) in this NRO although nothing here calls them.
        #[link_name = "_ZN5ImGui17GetCurrentContextEv"]
        fn igGetCurrentContext() -> *mut c_void;
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
            "c1e4471b354565ab97b2c8d5b5698a70f774a991c540fce7fc12511fb42c0d88",
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
        "c1e4471b354565ab97b2c8d5b5698a70f774a991c540fce7fc12511fb42c0d88";

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

    /// Runs first in the archive's context setup, before the backend adds the
    /// fonts (`InitImGui`: CreateContext, the context-setup queue, then
    /// InitBackend), so the index mode is set before the only bake exists.
    unsafe extern "C" fn imgui_post_context() {
        choose_index();
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

    /// A client declares that the Dear ImGui code it draws this host's context
    /// with reads the paged codepoint index (the archive's
    /// `0001-paged-codepoint-index.patch`). `probe` is any function in the
    /// client's own text. Returns 1 when recorded before the index mode was
    /// chosen, 2 when the choice was already made (the declaration then
    /// changes nothing), 0 when `probe` is not in a loaded plugin's text.
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_declare_paged_index_reader_v1(probe: *const c_void) -> u32 {
        let Some((base, _)) = (unsafe { plugin_of(probe as usize) }) else {
            return 0;
        };
        {
            let mut declared = DECLARED.lock().unwrap();
            let (bases, count) = &mut *declared;
            if !bases[..*count].contains(&base) && *count < bases.len() {
                bases[*count] = base;
                *count += 1;
            }
        }
        if CHOICE.lock().unwrap().is_some() {
            2
        } else {
            1
        }
    }

    /// How the index mode was chosen (`clients::HostClients`, version 1).
    /// Returns 1 after the choice, 0 before it or for a null pointer.
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_host_clients_v1(out: *mut crate::clients::HostClients) -> i32 {
        if out.is_null() {
            return 0;
        }
        match *CHOICE.lock().unwrap() {
            Some(record) => {
                unsafe { out.write(record) };
                1
            }
            None => 0,
        }
    }

    /// The archive's font counters (`ImguiSmashFontStats`), for a client that
    /// draws through this host and reports the overlay's fonts.
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_font_stats_wrapper(out: *mut c_void) -> i32 {
        unsafe { imgui_smash_font_stats(out) }
    }

    /// The dynamic codepoints in the atlas (`imgui_smash_font_dynamic_set`).
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_font_dynamic_set_wrapper(out: *mut u32, cap: u32) -> i32 {
        unsafe { imgui_smash_font_dynamic_set(out, cap) }
    }

    /// The renderer's draw-slot counters (`ImguiSmashDrawStats`).
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_draw_stats_wrapper(out: *mut c_void) -> i32 {
        unsafe { imgui_smash_draw_stats(out) }
    }

    /// The frame guard's counters (`ImguiSmashFrameGuardStats`).
    #[no_mangle]
    pub unsafe extern "C" fn imgui_smash_frame_guard_stats_wrapper(out: *mut c_void) -> i32 {
        unsafe { imgui_smash_frame_guard_stats(out) }
    }

    /// Module bases of the declared paged readers (fixed storage: no heap).
    static DECLARED: Mutex<([usize; MAX_DECLARED], usize)> = Mutex::new(([0; MAX_DECLARED], 0));
    const MAX_DECLARED: usize = 16;
    /// The index-mode choice, once made.
    static CHOICE: Mutex<Option<crate::clients::HostClients>> = Mutex::new(None);
    const MAX_PLUGINS: usize = 64;

    /// The clients (plugins other than this one that name a context export),
    /// checked against the declarations; the archive's fonts follow.
    unsafe fn choose_index() {
        let own = unsafe { plugin_of(choose_index as *const () as usize) }.map(|(base, _)| base);
        let mut clients = [0usize; MAX_PLUGINS];
        let mut count = 0;
        let mut seen = 0;
        let mut overflow = false;
        unsafe {
            for_each_plugin(|base, segments| {
                seen += 1;
                if Some(base) == own || !names_export(base, segments) {
                    return;
                }
                if count < clients.len() {
                    clients[count] = base;
                    count += 1;
                } else {
                    overflow = true;
                }
            });
        }
        let (declared, declared_count) = *DECLARED.lock().unwrap();
        let mut record = crate::clients::host_clients(&clients[..count], &declared[..declared_count], seen);
        if overflow {
            // More clients than the table holds: one of them is unchecked.
            record.paged = 0;
            record.clients = record.clients.saturating_add(1);
        }
        unsafe {
            imgui_smash_font_use_paged_index(record.paged as i32);
        }
        *CHOICE.lock().unwrap() = Some(record);
    }

    unsafe fn names_export(base: usize, segments: crate::clients::NroSegments) -> bool {
        let Some(start) = base.checked_add(segments.ro_offset as usize) else {
            return true;
        };
        let size = segments.ro_size as usize;
        if !unsafe { readable(start, size) } {
            // Unreadable: count it as a client, which keeps the index dense.
            return true;
        }
        crate::clients::names_context_export(unsafe { std::slice::from_raw_parts(start as *const u8, size) })
    }

    /// Horizon QueryMemory (libnx `svcQueryMemory`, svc 0x6).
    #[repr(C)]
    #[derive(Default)]
    struct MemoryInfo {
        address: u64,
        size: u64,
        kind: u32,
        attributes: u32,
        permission: u32,
        ipc_references: u32,
        device_references: u32,
        padding: u32,
    }

    unsafe fn query(address: usize) -> Option<MemoryInfo> {
        let mut info = MemoryInfo::default();
        let result: u64;
        unsafe {
            core::arch::asm!(
                "svc 0x6",
                inout("x0") &mut info as *mut MemoryInfo as u64 => result,
                lateout("x1") _,
                in("x2") address as u64,
                clobber_abi("C"),
                options(nostack),
            );
        }
        (result as u32 == 0).then_some(info)
    }

    unsafe fn readable(address: usize, size: usize) -> bool {
        let Some(end) = address.checked_add(size) else {
            return false;
        };
        if address < 0x10000 || size == 0 {
            return false;
        }
        unsafe { query(address) }.is_some_and(|info| {
            info.permission & 1 == 1
                && info.address <= address as u64
                && info.address.checked_add(info.size).is_some_and(|mapped| end as u64 <= mapped)
        })
    }

    /// The plugin image whose text holds `address`: Skyline maps an NRO's text
    /// at its header, so the text mapping's base is the module base.
    unsafe fn plugin_of(address: usize) -> Option<(usize, crate::clients::NroSegments)> {
        let info = unsafe { query(address) }?;
        if info.permission != 5 {
            return None;
        }
        let base = info.address as usize;
        let segments = unsafe { plugin_header(base, info.size as usize) }?;
        ((address - base) < segments.text_size as usize).then_some((base, segments))
    }

    unsafe fn plugin_header(base: usize, mapped: usize) -> Option<crate::clients::NroSegments> {
        if mapped < 0x80 || !unsafe { readable(base, 0x80) } {
            return None;
        }
        let header = unsafe { std::slice::from_raw_parts(base as *const u8, 0x80) };
        crate::clients::parse_nro_header(header).filter(|segments| segments.text_size as usize <= mapped)
    }

    /// Every executable mapping that starts with a plugin's NRO header.
    unsafe fn for_each_plugin(mut visit: impl FnMut(usize, crate::clients::NroSegments)) {
        let mut address = 0usize;
        for _ in 0..0x10000 {
            let Some(info) = (unsafe { query(address) }) else {
                return;
            };
            if info.size == 0 {
                return;
            }
            if info.permission == 5 {
                if let Some(segments) = unsafe { plugin_header(info.address as usize, info.size as usize) } {
                    visit(info.address as usize, segments);
                }
            }
            let Some(next) = info.address.checked_add(info.size) else {
                return;
            };
            if next as usize <= address {
                return;
            }
            address = next as usize;
        }
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

        // L3 + R3 toggles gamepad navigation only if config.ini enables it.
        let nav_toggle = std::fs::read_to_string(crate::config::CONFIG_INI)
            .is_ok_and(|ini| crate::config::gamepad_nav_toggle(&ini));

        unsafe {
            imgui_smash_set_gamepad_nav_toggle(nav_toggle);
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
        let wrote = unsafe { imgui_smash_frame_guard_stats((&mut stats as *mut FrameGuardStats).cast()) };
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
        let wrote = unsafe { imgui_smash_draw_stats((&mut stats as *mut DrawStats).cast()) };
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
        let digest = "c1e4471b354565ab97b2c8d5b5698a70f774a991c540fce7fc12511fb42c0d88";
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
