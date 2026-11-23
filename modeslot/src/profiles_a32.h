// Firmware profiles for the 32-bit ARM MPC builds (Gen1 MPC and Force, MPC OS 3.10.1). Addresses are link-time;
// modeslot/check_profile.py verifies them against a binary. A32 jump tables are tables of `b` instructions: a table is
// the address of its first entry (the instruction after the default `b` that follows the `addls pc, pc, rN, lsl #2`).
// Entry instruction pairs: the first two words of the function (see arch_a32.c).
//
// Gen1 and the Force run the same program text, about 0x40 bytes apart in the first 15 MB and 0x438 apart in the touch UI.
static const struct omm_profile profiles[] = {
{
    .label = "MPC 3.10.1 Gen1 (armv7, 851d050e)",
    .build_id = {0x85, 0x1d, 0x05, 0x0e, 0xb0, 0x3c, 0xe0, 0xb8, 0x66, 0x3a,
                 0xaa, 0x57, 0x2b, 0x25, 0x99, 0x81, 0x3c, 0xbf, 0x53, 0xd4},
    .build_id_len = 20,
    .candidates = {0x2090, 0x2092, 0x2099, 0},
    .slot_name = "Plugin Manager",
    .name_table = 0x166e638, .name_default = 0x166e6cc,
    .page_table = 0x36408ac, .page_default = 0x3640950,
    .table_len = 0x25,
    .get_mode_name = {0x166e39c, {0xe3033112, 0xe52d4008}},          // movw r3,#0x3112; str r4,[sp,#-8]!
    .string_ctor = 0x98fe98, .string_ctor_first = 0xe16d41f0,        // juce::String::String(const char *): strd r4,r5,[sp,#-0x10]!
    .register_mode = {0x166b374, {0xe16d42f4, 0xe59040b8}},
    .alias_from = 0x2080,
    .mode_changed = {0x3640804, {0xe59f24fc, 0xe59f34fc}},           // two PC-relative literal loads
    .image_from_memory = {0xadbeb4, {0xe16d42f4, 0xe1cd81f0}},
    .placeholder_ptr_var = 0x7028620, .placeholder_size = 0x32e46,   // -> the PNG at 0x5844d50
    .mode_icon = {0x3818620, {0xe30230a4, 0xe59f247c}},
    .menu_order = {0xef4e20, {0xe16d41fc, 0xe1a05002}},
    .page_paint = {0x3a5a5ec, {0xe59fc194, 0xe59f2194}},
    .mouse_slots = {0x6f3c3d0, 0x6f3c3d4, 0x6f3c3d8}, .mouse_default = 0xb53c08,
    .repaint = {0xbc39d0, 0xe59f226c},
    .repaint_area = {0xbc322c, 0xe24dd010},
    .memstream_ctor = {0x94f4dc, 0xe59fc0e0},
    .memstream_dtor = {0x93bc70, 0xe52d4008},
    .find_format = {0xa86bbc, 0xe5903000},
    .decode_vslot = 0x14,
    .page_component_offset = 8,
    .set_bounds = {0xbd72c8, 0xe16d41f8},
    .dummy_page_offset = 0x3510, .page_holder_field = 0x1a0, .page_holder_get = 0x3640988,
    .page_vtable = 0x6f3c370,
    .wrapper_vtable = 0x6f01bec,
    .comp_parent = 0xc, .comp_bounds = 0x10, .comp_children = 0x28, .comp_children_used = 0x30, .img_refcount = 4,
},
{
    .label = "Force 3.10.1 (armv7, 1d935334)",
    .build_id = {0x1d, 0x93, 0x53, 0x34, 0x52, 0xbc, 0x33, 0x00, 0x28, 0xca,
                 0x60, 0xb7, 0x35, 0xb8, 0x3a, 0x4b, 0xe3, 0xe4, 0xeb, 0xbd},
    .build_id_len = 20,
    .candidates = {0x2090, 0x2092, 0x2099, 0},
    .slot_name = "Plugin Manager",
    .name_table = 0x166e5f8, .name_default = 0x166e68c,
    .page_table = 0x3640474, .page_default = 0x3640518,
    .table_len = 0x25,
    .get_mode_name = {0x166e35c, {0xe3033112, 0xe52d4008}},
    .string_ctor = 0x98fe58, .string_ctor_first = 0xe16d41f0,
    .register_mode = {0x166b334, {0xe16d42f4, 0xe59040b8}},
    .alias_from = 0x2080,
    .mode_changed = {0x36403cc, {0xe59f24fc, 0xe59f34fc}},
    .image_from_memory = {0xadbe74, {0xe16d42f4, 0xe1cd81f0}},
    .placeholder_ptr_var = 0x7027620, .placeholder_size = 0x32e46,   // -> the PNG at 0x58445d0
    .mode_icon = {0x38181e8, {0xe30230a4, 0xe59f247c}},
    .menu_order = {0xef4de0, {0xe16d41fc, 0xe1a05002}},
    .page_paint = {0x3a5a1b4, {0xe59fc194, 0xe59f2194}},
    .mouse_slots = {0x6f3b3f0, 0x6f3b3f4, 0x6f3b3f8}, .mouse_default = 0xb53bc8,
    .repaint = {0xbc3990, 0xe59f226c},
    .repaint_area = {0xbc31ec, 0xe24dd010},
    .memstream_ctor = {0x94f49c, 0xe59fc0e0},
    .memstream_dtor = {0x93bc30, 0xe52d4008},
    .find_format = {0xa86b7c, 0xe5903000},
    .decode_vslot = 0x14,
    .page_component_offset = 8,
    .set_bounds = {0xbd7288, 0xe16d41f8},
    .dummy_page_offset = 0x3510, .page_holder_field = 0x1a0, .page_holder_get = 0x3640550,
    .page_vtable = 0x6f3b390,
    .wrapper_vtable = 0x6f00c0c,
    .comp_parent = 0xc, .comp_bounds = 0x10, .comp_children = 0x28, .comp_children_used = 0x30, .img_refcount = 4,
},
};
