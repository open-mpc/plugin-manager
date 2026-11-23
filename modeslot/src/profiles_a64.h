// Firmware profiles for Gen2 (aarch64). Addresses are link-time; modeslot/check_profile.py verifies them against a binary.
static const struct omm_profile profiles[] = {
    {
        .label = "MPC 3.10.1.12 (f088e9c8427), Gen2 official 3.10.1",
        .build_id = {0x8b, 0x28, 0x17, 0x9d, 0x89, 0x27, 0x7a, 0x5f, 0x73, 0xc3,
                     0x0b, 0xe9, 0x07, 0x40, 0x76, 0x6e, 0x61, 0x66, 0xb8, 0x37},
        .build_id_len = 20,
        .candidates = {0x2090, 0x2092, 0x2099, 0},
        .slot_name = "Plugin Manager",   // as long as "Step Sequencer", the longest stock name: fits a MENU tile
        .name_table = 0x5386fa0, .name_table_base = 0x1bed864, .name_default = 0x1bed800, // getModeName
        .page_table = 0x6712c58, .page_table_base = 0x3889800, .page_default = 0x3889668, // touch UI pages
        .table_len = 0x25,
        // paciasp; stp x29, x30, [sp, #-0x20]!; mov w1, w0; mov w0, #0x3112
        .get_mode_name = {0x1bed7c0, {0xd503233f, 0xa9be7bfd, 0x2a0003e1, 0x52862240}},
        .string_ctor = 0x108e5e4,
        .string_ctor_first = 0xd503233f, // paciasp
        // paciasp; stp x29, x30, [sp, #-0x50]!; mov x29, sp; str x25, [sp, #0x40]
        .register_mode = {0x1beab20, {0xd503233f, 0xa9bb7bfd, 0x910003fd, 0xf90023f9}},
        .alias_from = 0x2080, // Main Mode
        // paciasp; sub sp, sp, #0x70; stp x29, x30, [sp, #0x30]; add x29, sp, #0x30
        .mode_changed = {0x38895cc, {0xd503233f, 0xd101c3ff, 0xa9037bfd, 0x9100c3fd}},
        // paciasp; sub sp, sp, #0xa0; adrp x2, 0x7f53000; ldr x2, [x2, #0x8b8]
        .image_from_memory = {0x11bd880, {0xd503233f, 0xd10283ff, 0xd0036ca2, 0xf9445c42}},
        .placeholder_ptr_var = 0x7f83b30, // -> the PNG at 0x5a3b200 in the file
        .placeholder_size = 0x32e46,      // 1280 x 800
        // paciasp; sub sp, sp, #0x30; adrp x1, 0x7f53000; ldr x1, [x1, #0x8b8]
        .mode_icon = {0x3a3c4c0, {0xd503233f, 0xd100c3ff, 0xf00228a1, 0xf9445c21}},
        // paciasp; sub sp, sp, #0x70; stp x29, x30, [sp, #0x30]; add x29, sp, #0x30
        .menu_order = {0x154da40, {0xd503233f, 0xd101c3ff, 0xa9037bfd, 0x9100c3fd}},
        // paciasp; sub sp, sp, #0x70; adrp x3, 0x7f58000; ldr x3, [x3, #0x210]
        .page_paint = {0x3c59b80, {0xd503233f, 0xd101c3ff, 0xf00217e3, 0xf9410863}},
        .mouse_slots = {0x7df7140, 0x7df7148, 0x7df7150}, // TouchDummyPage's Component vtable 0x7df7118, entries 5..7
        .mouse_default = 0x1203cc8,
        .repaint = {0x1269b80, 0xd503245f},        // juce::Component::repaint(), bti c
        .repaint_area = {0x1269664, 0xd503233f},   // its Rectangle<int> repaint, paciasp
        .memstream_ctor = {0x1053020, 0xd503233f}, // juce::MemoryInputStream(data, size, keepCopy)
        .memstream_dtor = {0x10413cc, 0xd503245f},
        .find_format = {0x1172c88, 0xd503233f},    // juce::ImageFileFormat::findImageFormatForStream
        .decode_vslot = 0x28,                      // ImageFileFormat::decodeImage
        .page_component_offset = 16,               // offset-to-top of vtable 0x7df7118 is -16
        .set_bounds = {0x127f5e0, 0xd503233f},     // juce::Component::setBounds(x, y, w, h), paciasp
        .dummy_page_offset = 0x6268,               // mode handler 0x38895cc, default case: add x21, x19, #0x6268
        .page_holder_field = 0x300,                // the holder's page getter 0x3881960: ldr x0, [x0, #0x300]
        .page_holder_get = 0x3881960,
        .page_vtable = 0x7df7080,                  // TouchDummyPage, set again by its destructor 0x3c59b08
        .page_dtor = 0x3c59b08,
        .wrapper_vtable = 0x7d7fae0,               // each mode's page wrapper (read from the Live III's component tree)
        .comp_parent = 0x18, .comp_bounds = 0x20, .comp_children = 0x40, .comp_children_used = 0x4c, .img_refcount = 8,
    },
};
