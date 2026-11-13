//! The hooks this NRO installs (`nvnBootstrapLoader`, and the NVN calls the
//! archive wraps through it) run on game threads, while plugin threads call its
//! exports. A hook must never wait on a lock a plugin thread can hold; this
//! NRO holds to that by using no lock at all, in the Rust shell or in the
//! archive's C++. The scan below fails on any lock primitive in their sources.

use std::path::{Path, PathBuf};

/// Primitives that can make a thread wait for another (Rust, C++, Horizon).
const PRIMITIVES: [&str; 20] = [
    "Mutex",
    "RwLock",
    "Condvar",
    // std::sync::Barrier; NVN's GPU `cmd->Barrier(...)` is no lock.
    "sync::Barrier",
    "OnceLock",
    "LazyLock",
    "std::sync::Once",
    ".lock()",
    "std::mutex",
    "lock_guard",
    "unique_lock",
    "scoped_lock",
    "condition_variable",
    "call_once",
    "pthread_mutex",
    "nn::os::LockMutex",
    "nn::os::SdkMutex",
    "nn::os::WaitEvent",
    "nn::os::WaitConditionVariable",
    "nn::os::AcquireSemaphore",
];

/// `(line, primitive)` for every lock primitive in the code (comments skipped).
fn lock_uses(text: &str) -> Vec<(usize, &'static str)> {
    let mut found = Vec::new();
    for (index, line) in text.lines().enumerate() {
        let trimmed = line.trim_start();
        if trimmed.starts_with("//") || trimmed.starts_with('*') || trimmed.starts_with("/*") {
            continue;
        }
        let code = match line.find("//") {
            Some(at) if !line[..at].ends_with(':') => &line[..at],
            _ => line,
        };
        for primitive in PRIMITIVES {
            if code.contains(primitive) {
                found.push((index + 1, primitive));
            }
        }
    }
    found
}

fn sources(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            sources(&path, out);
        } else if path
            .extension()
            .and_then(|ext| ext.to_str())
            .is_some_and(|ext| matches!(ext, "rs" | "c" | "cpp" | "h" | "hpp" | "inl"))
        {
            out.push(path);
        }
    }
}

#[test]
fn a_seeded_lock_is_found() {
    let rust = "static CHOICE: Mutex<Option<u32>> = Mutex::new(None);\nlet x = CHOICE.lock().unwrap();\n";
    assert_eq!(lock_uses(rust), [(1, "Mutex"), (2, ".lock()")]);
    let cpp = "  std::lock_guard<std::mutex> guard(m);\n  nn::os::LockMutex(&m_Mutex);\n";
    assert_eq!(
        lock_uses(cpp),
        [(1, "std::mutex"), (1, "lock_guard"), (2, "Mutex"), (2, "nn::os::LockMutex")]
    );
    let comments = "/// read without a Mutex\n// no lock_guard here\n * std::mutex in a block comment\nlet a = 1; // .lock() later\n";
    assert!(lock_uses(comments).is_empty());
}

#[test]
fn no_source_of_this_nro_uses_a_lock() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut files = Vec::new();
    for dir in ["src", "imgui-smash/src", "imgui-smash/include"] {
        sources(&root.join(dir), &mut files);
    }
    files.push(root.join("imgui-smash/cimgui_old.cpp"));
    let names: Vec<String> = files.iter().map(|path| path.display().to_string()).collect();
    for expected in ["src/lib.rs", "src/clients.rs", "imgui_backend/imgui_impl_nvn.cpp", "frame_guard.hpp"] {
        assert!(names.iter().any(|name| name.ends_with(expected)), "{expected} not scanned");
    }
    let mut uses = Vec::new();
    for path in &files {
        let text = std::fs::read_to_string(path).unwrap_or_else(|err| panic!("{}: {err}", path.display()));
        for (line, primitive) in lock_uses(&text) {
            uses.push(format!("{}:{line}: {primitive}", path.display()));
        }
    }
    assert!(uses.is_empty(), "lock primitives in {} scanned files:\n{}", files.len(), uses.join("\n"));
}
