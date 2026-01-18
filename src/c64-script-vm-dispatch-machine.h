/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script-vm-internal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool c64script_dispatch_machine(c64script_runtime_t *runtime, const c64script_instruction_t *instr);

#ifdef __cplusplus
}
#endif
