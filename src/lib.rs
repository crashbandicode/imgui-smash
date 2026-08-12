#![feature(c_variadic)]

use std::ffi::CStr;
use imgui_api::bindings::igGetCurrentContext;
use skyline::hook;
use skyline::libc::{c_char, c_void, size_t};
use skyline::hooks::InlineCtx;

#[link(name = "imgui_smash")]
unsafe extern "C" {
    fn imgui_smash_init(post_init: *const c_void, render: *const c_void);
    fn imgui_smash_set_nvn_device(device: u64);
    fn imgui_smash_set_nvn_queue(device: u64);
    fn imgui_smash_set_proc_address(proc_address: u64);
    fn imgui_smash_lookup_original_functions();
    fn imgui_smash_get_proc_address() -> u64;
    fn imgui_smash_show_mouse(show_mouse: bool);

    fn imgui_smash_bootstrap_hook(name: *const c_char, original_fn: *const c_void) -> *const c_void;

    fn imgui_smash_add_on_pre_init(pre_init: *const c_void);
    fn imgui_smash_add_on_new_frame(new_frame: *const c_void);
    fn imgui_smash_add_on_draw_frame(new_frame: *const c_void);

    fn imgui_smash_set_logger(callback: *const c_void);
    fn imgui_smash_context_setup(callback: *const c_void);

    // ImGui::ShowDemoWindow(bool *), provided by the static lib
    #[link_name = "_ZN5ImGui14ShowDemoWindowEPb"]
    fn imgui_show_demo_window(open: *const bool);
}

#[no_mangle]
extern "C" fn imgui_smash_show_mouse_wrapper(show_mouse: bool) {
    unsafe { imgui_smash_show_mouse(show_mouse); }
}

#[no_mangle]
extern "C" fn imgui_smash_add_on_pre_init_wrapper(pre_init: *const c_void) {
    unsafe { imgui_smash_add_on_pre_init(pre_init); }
}
#[no_mangle]
extern "C" fn imgui_smash_add_on_new_frame_wrapper(new_frame: *const c_void) {
    unsafe { imgui_smash_add_on_new_frame(new_frame); }
}
#[no_mangle]
extern "C" fn imgui_smash_add_on_draw_frame_wrapper(new_frame: *const c_void) {
    unsafe { imgui_smash_add_on_draw_frame(new_frame); }
}
#[no_mangle]
extern "C" fn imgui_show_demo_window_wrapper(open: *const bool) {
    unsafe { imgui_show_demo_window(open); }
}

// Import nvnBootstrapLoader from main
extern "C" {
    fn nvnBootstrapLoader(name: *const c_char) -> *const c_void;
}

unsafe extern "C" fn imgui_log(msg: *const c_char, len: size_t) {
    let msg = {
        let bytes = std::slice::from_raw_parts(msg, len);
        CStr::from_bytes_with_nul_unchecked(bytes)
    };
    let msg = msg.to_str().unwrap();
    println!("{}\n", msg);
}


pub type SetupContextFn = unsafe extern "C" fn(*mut u64);

lazy_static::lazy_static! {
    pub static ref POST_INIT_FUNCS: std::sync::Mutex<Vec<SetupContextFn>> = std::sync::Mutex::new(Vec::new());
}

unsafe extern "C" fn imgui_post_context() {
    let context = igGetCurrentContext();
    let funcs = POST_INIT_FUNCS.lock().unwrap();
    for x in 0..funcs.len() {
        funcs[x](context as _);
    }
}

#[no_mangle]
unsafe extern "C" fn imgui_setup_context_export(func: SetupContextFn) {
    POST_INIT_FUNCS.lock().unwrap().push(func);
}
#[no_mangle]
unsafe extern "C" fn imgui_get_context_export() -> *mut u64 {
    igGetCurrentContext() as _
}

#[hook(replace = nvnBootstrapLoader)]
unsafe fn nvn_bootstrap_hook(name: *const c_char) -> *const c_void {
    imgui_smash_bootstrap_hook(name, original!() as *const c_void)
}

#[skyline::main(name = "imgui")]
pub fn main() {
    std::panic::set_hook(Box::new(|info| {
        let location = info.location().unwrap();

        let msg = match info.payload().downcast_ref::<&'static str>() {
            Some(s) => *s,
            None => match info.payload().downcast_ref::<String>() {
                Some(s) => &s[..],
                None => "Box<Any>",
            },
        };

        let err_msg = format!("imgui-smash has panicked at '{}', {}", msg, location);
        skyline::error::show_error(
            69,
            "imgui-smash has panicked! Please open the details and send a screenshot to the developer, then close the game.\n\0",
            err_msg.as_str(),
        );
    }));

    skyline::install_hook!(nvn_bootstrap_hook);

    unsafe {
        imgui_smash_context_setup(imgui_post_context as _);
        // imgui_smash_set_logger(imgui_log as *const c_void);
        imgui_smash_init(
            std::ptr::null(),
            std::ptr::null()
        );
    }
}
