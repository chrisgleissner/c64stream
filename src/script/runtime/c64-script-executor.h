/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifndef C64_SCRIPT_EXECUTOR_H
#define C64_SCRIPT_EXECUTOR_H

#include <obs.h>
#include <stdbool.h>

typedef struct c64_script_executor c64_script_executor_t;

typedef enum {
    C64_SCRIPT_STATUS_IDLE,
    C64_SCRIPT_STATUS_RUNNING,
    C64_SCRIPT_STATUS_PAUSED,
    C64_SCRIPT_STATUS_WAITING,
    C64_SCRIPT_STATUS_ERROR,
    C64_SCRIPT_STATUS_COMPLETED
} c64_script_status_t;

/**
 * Create a new script executor for the given source and source context
 */
c64_script_executor_t *c64_script_executor_create(obs_source_t *source, void *source_data);

/**
 * Destroy a script executor and free resources
 */
void c64_script_executor_destroy(c64_script_executor_t *executor);

/**
 * Start executing a script
 * Returns true if started successfully, false if already running or error
 */
bool c64_script_executor_start(c64_script_executor_t *executor, const char *script_file_path);

/**
 * Start executing a script in debug mode (paused on the first line).
 * Returns true if started successfully, false if already running or error
 */
bool c64_script_executor_start_debug(c64_script_executor_t *executor, const char *script_file_path);

/**
 * Validate/compile a script file without executing it.
 * Returns true if the script parses and compiles successfully.
 */
bool c64_script_executor_validate_file(c64_script_executor_t *executor, const char *script_file_path);

/**
 * Stop the currently executing script
 */
void c64_script_executor_stop(c64_script_executor_t *executor);

/**
 * Check if executor is currently running
 */
bool c64_script_executor_is_running(c64_script_executor_t *executor);

/**
 * Get current execution status
 */
c64_script_status_t c64_script_executor_get_status(c64_script_executor_t *executor);

/**
 * Get current line number being executed (0 if not running)
 */
int c64_script_executor_get_current_line(c64_script_executor_t *executor);

/**
 * Get execution progress (0-100, or -1 if unknown)
 */
int c64_script_executor_get_progress(c64_script_executor_t *executor);

/**
 * Get current command name being executed (NULL if not running)
 */
const char *c64_script_executor_get_current_command(c64_script_executor_t *executor);

/**
 * Get error message if status is ERROR (NULL otherwise)
 */
const char *c64_script_executor_get_error(c64_script_executor_t *executor);

/**
 * Pause the currently executing script (at next source line boundary)
 * Only works if status is RUNNING. Does nothing if already paused or not running.
 */
void c64_script_executor_pause(c64_script_executor_t *executor);

/**
 * Resume a paused script
 * Only works if status is PAUSED. Does nothing if not paused.
 */
void c64_script_executor_resume(c64_script_executor_t *executor);

/**
 * Step to the next source line (only when paused)
 * Returns true if step was successful, false if not paused or error
 */
bool c64_script_executor_step(c64_script_executor_t *executor);

/**
 * Get the last executed source line (line number and text)
 * Returns line number (0 if not started), fills line_text buffer if provided
 * line_text_size should include space for null terminator
 */
int c64_script_executor_get_last_executed_line(c64_script_executor_t *executor, char *line_text, size_t line_text_size);

/**
 * Get the next source line to execute (line number and text)
 * Returns line number (0 if completed or not started), fills line_text buffer if provided
 * line_text_size should include space for null terminator
 */
int c64_script_executor_get_next_line(c64_script_executor_t *executor, char *line_text, size_t line_text_size);

/**
 * Log all current variables to OBS log
 */
void c64_script_executor_log_variables(c64_script_executor_t *executor);

#endif // C64_SCRIPT_EXECUTOR_H
