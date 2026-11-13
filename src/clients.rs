//! Which plugins can read this host's ImGui context, and whether its fonts may
//! index glyphs in 256-codepoint pages (`imgui_smash_font_use_paged_index`).
//!
//! A paged index is read correctly only by Dear ImGui code built with the
//! archive's `0001-paged-codepoint-index.patch`; an unpatched copy reads
//! `IndexLookup` as one array and gets wrong glyphs or reads out of bounds.
//! Online Deluxe's built-in host always pages its index (0.10.4 on). This host
//! pages it only when every client has declared, through
//! `imgui_smash_declare_paged_index_reader_v1`, that its compiled ImGui reads
//! the paged index. A client is any loaded plugin, other than this one, whose
//! read-only segment names one of the host exports below: a plugin needs one
//! of them (as a dynamic import or a `LookupSymbol` string) to reach the
//! context at all. Skyline loads every plugin before the context exists, so
//! the set is complete when the choice is made.

/// The exports through which a plugin gets the context or runs inside the
/// host's frame. `imgui_smash_show_mouse_wrapper` and the stats getters give
/// no context and are not listed.
pub const CONTEXT_EXPORTS: [&[u8]; 5] = [
    b"imgui_setup_context_export",
    b"imgui_get_context_export",
    b"imgui_smash_add_on_draw_frame_wrapper",
    b"imgui_smash_add_on_new_frame_wrapper",
    b"imgui_smash_add_on_pre_init_wrapper",
];

/// True when `ro` (a plugin's read-only segment) names a context export.
pub fn names_context_export(ro: &[u8]) -> bool {
    CONTEXT_EXPORTS
        .iter()
        .any(|name| ro.len() >= name.len() && ro.windows(name.len()).any(|window| window == *name))
}

/// The host's choice for its fonts' codepoint index. `clients` and `declared`
/// are module bases. Paged when every client declared a paged reader (also
/// when there is no client: then only this host's own patched code reads it).
pub fn paged_index(clients: &[usize], declared: &[usize]) -> bool {
    clients.iter().all(|client| declared.contains(client))
}

/// `ImguiSmashHostClients` version 1, filled by
/// `imgui_smash_host_clients_v1`: how the choice was made.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct HostClients {
    pub version: u32,
    pub size: u32,
    /// Plugins that name a context export (this host excluded).
    pub clients: u32,
    /// Of them, those that declared a paged reader.
    pub declared_clients: u32,
    /// Declarations received, from any module.
    pub declarations: u32,
    /// 1 once the choice was made (when the context was created).
    pub decided: u32,
    /// 1 when the fonts index in pages.
    pub paged: u32,
    /// Loaded plugin images found when the choice was made (this host included).
    pub plugins_seen: u32,
}

pub const HOST_CLIENTS_VERSION: u32 = 1;
const _: () = assert!(std::mem::size_of::<HostClients>() == 32);

/// The record of a choice: `clients` and `declared` as for [`paged_index`],
/// `declarations` the declarations received, `plugins` the images seen.
pub fn host_clients(clients: &[usize], declared: &[usize], plugins: usize) -> HostClients {
    let declared_clients = clients.iter().filter(|client| declared.contains(client)).count();
    HostClients {
        version: HOST_CLIENTS_VERSION,
        size: std::mem::size_of::<HostClients>() as u32,
        clients: clients.len() as u32,
        declared_clients: declared_clients as u32,
        declarations: declared.len() as u32,
        decided: 1,
        paged: u32::from(paged_index(clients, declared)),
        plugins_seen: plugins as u32,
    }
}

/// File extents from an NRO header (`nn::ro::NroHeader`): text and ro.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NroSegments {
    pub size: u32,
    pub text_offset: u32,
    pub text_size: u32,
    pub ro_offset: u32,
    pub ro_size: u32,
}

/// The NRO header at the start of a mapping, if `header` is one whose text
/// starts at the header (Skyline plugins) and whose ro segment lies in the file.
pub fn parse_nro_header(header: &[u8]) -> Option<NroSegments> {
    if header.len() < 0x80 || header[0x10..0x14] != *b"NRO0" {
        return None;
    }
    let u32_at = |offset: usize| u32::from_le_bytes(header[offset..offset + 4].try_into().unwrap());
    let segments = NroSegments {
        size: u32_at(0x18),
        text_offset: u32_at(0x20),
        text_size: u32_at(0x24),
        ro_offset: u32_at(0x28),
        ro_size: u32_at(0x2c),
    };
    let inside = |offset: u32, len: u32| {
        len > 0 && offset.checked_add(len).is_some_and(|end| end <= segments.size)
    };
    (segments.text_offset == 0
        && inside(segments.text_offset, segments.text_size)
        && inside(segments.ro_offset, segments.ro_size))
    .then_some(segments)
}

#[cfg(test)]
mod tests {
    use super::*;

    const DELUXE: usize = 0x1000_0000;
    const OLD_DELUXE: usize = 0x1100_0000;
    const OTHER: usize = 0x2000_0000;

    /// The standalone's choice next to Online Deluxe's built-in host, which
    /// always pages its index, for one table of client sets.
    #[test]
    fn standalone_index_mode_matches_deluxe_embedded_when_every_client_reads_pages() {
        let embedded_paged = true;
        let cases: [(&str, &[usize], &[usize], bool); 6] = [
            ("Deluxe with the declaration (the card: no other client)", &[DELUXE], &[DELUXE], true),
            ("no client: only this host's patched code reads", &[], &[], true),
            ("Deluxe 0.10.4/0.10.5, which never declares", &[OLD_DELUXE], &[], false),
            ("Deluxe and an undeclared plugin", &[DELUXE, OTHER], &[DELUXE], false),
            ("a declaration from a module that is no client", &[OTHER], &[DELUXE], false),
            ("two declared clients", &[DELUXE, OTHER], &[OTHER, DELUXE], true),
        ];
        for (name, clients, declared, expected) in cases {
            let paged = paged_index(clients, declared);
            assert_eq!(paged, expected, "{name}");
            assert_eq!(paged == embedded_paged, expected, "{name}: parity with the built-in host");
            let record = host_clients(clients, declared, 21);
            assert_eq!(record.paged, u32::from(expected), "{name}");
            assert_eq!(record.clients as usize, clients.len(), "{name}");
            assert_eq!(record.declarations as usize, declared.len(), "{name}");
            assert_eq!(record.decided, 1);
            assert_eq!(record.version, HOST_CLIENTS_VERSION);
            assert_eq!(record.size, 32);
            assert_eq!(record.plugins_seen, 21);
        }
        let mixed = host_clients(&[DELUXE, OTHER], &[DELUXE], 3);
        assert_eq!(mixed.declared_clients, 1);
    }

    #[test]
    fn a_plugin_is_a_client_when_it_names_a_context_export() {
        // Online Deluxe's ro segment: LookupSymbol strings, NUL-terminated.
        let deluxe = b"..imgui_setup_context_export\0imgui_smash_add_on_draw_frame_wrapper\0imgui_get_context_export\0..";
        assert!(names_context_export(deluxe));
        for name in CONTEXT_EXPORTS {
            let mut ro = b"xx".to_vec();
            ro.extend_from_slice(name);
            assert!(names_context_export(&ro), "{}", String::from_utf8_lossy(name));
            assert!(!names_context_export(&ro[..ro.len() - 1]), "a cut name is not a name");
        }
        // The mouse toggle and the stats getters give no context.
        assert!(!names_context_export(b"imgui_smash_show_mouse_wrapper\0imgui_smash_font_stats_wrapper\0"));
        assert!(!names_context_export(b""));
        assert!(!names_context_export(b"imgui_smash"));
    }

    fn header(text_offset: u32, ro_offset: u32, ro_size: u32) -> [u8; 0x80] {
        let mut header = [0u8; 0x80];
        header[0x10..0x14].copy_from_slice(b"NRO0");
        for (offset, value) in [
            (0x18, 0x134000u32),
            (0x20, text_offset),
            (0x24, 0xe8000),
            (0x28, ro_offset),
            (0x2c, ro_size),
        ] {
            header[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        header
    }

    #[test]
    fn plugin_headers_give_their_ro_segment() {
        let segments = parse_nro_header(&header(0, 0xe8000, 0x42000)).unwrap();
        assert_eq!((segments.ro_offset, segments.ro_size), (0xe8000, 0x42000));
        assert!(parse_nro_header(&header(0x100, 0xe8000, 0x42000)).is_none(), "text must start at the header");
        assert!(parse_nro_header(&header(0, 0x130000, 0x42000)).is_none(), "ro past the file");
        assert!(parse_nro_header(&header(0, 0xe8000, 0)).is_none(), "empty ro");
        let mut not_nro = header(0, 0xe8000, 0x42000);
        not_nro[0x10] = b'M';
        assert!(parse_nro_header(&not_nro).is_none());
        assert!(parse_nro_header(&[0u8; 0x40]).is_none());
    }
}
