/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * Virtual Machine
 *
 * Executes bytecode instructions.
 */

/**
 * Execute bytecode in runtime context
 * Returns true on successful completion
 * Returns false on error (check runtime->error_msg)
 */
bool c64script_vm_execute(c64script_runtime_t *runtime);

/**
 * Step through one instruction (for debugging)
 */
bool c64script_vm_step(c64script_runtime_t *runtime);
