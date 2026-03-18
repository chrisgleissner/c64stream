/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script-runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

bool c64script_vm_execute_instruction(c64script_runtime_t *runtime, const c64script_instruction_t *instr,
                                      bool *skip_line_update);

void c64script_vm_record_trace_entry(c64script_runtime_t *runtime, int line_num);

bool c64script_debug_logging_enabled(void);

bool require_number(c64script_runtime_t *runtime, const c64script_value_t *value, const char *what);
bool require_string(c64script_runtime_t *runtime, const c64script_value_t *value, const char *what);
bool compare_values(c64script_runtime_t *runtime, const c64script_value_t *a, const c64script_value_t *b, int *out_cmp,
                    const char *what);

bool number_to_int(c64script_runtime_t *runtime, const c64script_value_t *value, int *out, const char *what);
bool number_to_uint16(c64script_runtime_t *runtime, const c64script_value_t *value, uint16_t *out, const char *what);
bool number_to_uint8(c64script_runtime_t *runtime, const c64script_value_t *value, uint8_t *out, const char *what);

double wait_unit_multiplier(c64script_wait_unit_t unit);

bool parse_wallclock_target(const char *s, double *out_epoch_seconds);
bool format_current_time(c64script_runtime_t *runtime, char *out, size_t out_size);

bool is_c64u_path(const char *path, const char **out_c64u_path);
bool load_binary_file(const char *path, uint8_t **out_data, size_t *out_size, char *error_msg, size_t error_size);
bool load_text_file(const char *path, char **out_content, char *error_msg, size_t error_size);
bool write_file(const char *path, const char *content, bool truncate, char *error_msg, size_t error_size);
const char *file_extension_lower(const char *path);
bool c64script_resolve_script_path(c64script_runtime_t *runtime, const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
