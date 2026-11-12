//! `sd:/ultimate/imgui_smash/config.ini`, read once at boot.
//!
//! `gamepad_nav_toggle = on` lets L3 + R3 toggle ImGui gamepad navigation
//! (1.0.1 always allowed it). Without the file, the key or a known value it is
//! off: the chord then does nothing, and ImGui gets no gamepad input.

pub const CONFIG_INI: &str = "sd:/ultimate/imgui_smash/config.ini";
pub const KEY_GAMEPAD_NAV_TOGGLE: &str = "gamepad_nav_toggle";

/// The `gamepad_nav_toggle` setting in `ini`. `key = value` lines; `#` or `;`
/// starts a comment; `[section]` lines are ignored; the key and the value
/// (`on`/`off`, `true`/`false`, `yes`/`no`, `1`/`0`) are case-insensitive.
/// The last known value wins.
pub fn gamepad_nav_toggle(ini: &str) -> bool {
    let mut enabled = false;
    for line in ini.lines() {
        let line = line.split(['#', ';']).next().unwrap_or("").trim();
        if line.starts_with('[') {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        if !key.trim().eq_ignore_ascii_case(KEY_GAMEPAD_NAV_TOGGLE) {
            continue;
        }
        match value.trim().to_ascii_lowercase().as_str() {
            "on" | "true" | "yes" | "1" => enabled = true,
            "off" | "false" | "no" | "0" => enabled = false,
            _ => {}
        }
    }
    enabled
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn off_without_the_key() {
        assert!(!gamepad_nav_toggle(""));
        assert!(!gamepad_nav_toggle("other = on\n"));
        assert!(!gamepad_nav_toggle("# gamepad_nav_toggle = on\n"));
    }

    #[test]
    fn on_only_when_set() {
        assert!(gamepad_nav_toggle("gamepad_nav_toggle=on"));
        assert!(gamepad_nav_toggle("[imgui_smash]\r\n  Gamepad_Nav_Toggle = TRUE  ; L3 + R3\r\n"));
        for value in ["on", "true", "yes", "1"] {
            assert!(gamepad_nav_toggle(&format!("gamepad_nav_toggle = {value}")), "{value}");
        }
        for value in ["off", "false", "no", "0"] {
            assert!(!gamepad_nav_toggle(&format!("gamepad_nav_toggle = {value}")), "{value}");
        }
    }

    #[test]
    fn unknown_values_keep_the_previous_one() {
        assert!(!gamepad_nav_toggle("gamepad_nav_toggle = maybe"));
        assert!(!gamepad_nav_toggle("gamepad_nav_toggle ="));
        assert!(gamepad_nav_toggle("gamepad_nav_toggle = on\ngamepad_nav_toggle = maybe"));
    }

    #[test]
    fn the_last_known_value_wins() {
        assert!(!gamepad_nav_toggle("gamepad_nav_toggle = on\ngamepad_nav_toggle = off"));
        assert!(gamepad_nav_toggle("gamepad_nav_toggle = off\ngamepad_nav_toggle = on"));
    }

    #[test]
    fn the_path_is_the_hosts_own_folder() {
        assert_eq!(CONFIG_INI, "sd:/ultimate/imgui_smash/config.ini");
    }
}
