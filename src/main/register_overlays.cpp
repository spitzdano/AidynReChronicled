// Registers the section/function table that N64Recomp generated, so the runtime can resolve
// function pointers (jalr) by ROM address.
#include "librecomp/overlays.hpp"

#include "../../RecompiledFuncs/recomp_overlays.inl"

void aidyn_register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}
