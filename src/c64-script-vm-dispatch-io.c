/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-io.h"

#include "c64-script-builtins.h"
#include "c64-script-vm-internal.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#endif

typedef struct {
    char *data;
    size_t size;
} c64script_http_response_t;

static size_t http_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    c64script_http_response_t *response = (c64script_http_response_t *)userdata;
    if (!response || total == 0) {
        return 0;
    }

    char *new_data = realloc(response->data, response->size + total + 1);
    if (!new_data) {
        return 0;
    }

    response->data = new_data;
    memcpy(response->data + response->size, ptr, total);
    response->size += total;
    response->data[response->size] = '\0';
    return total;
}

bool c64script_dispatch_io(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
    switch (instr->opcode) {
    case OP_RUNLOCAL: {
        c64script_value_t output_var_val, status_var_val, args_val, path_val;
        if (!c64script_runtime_pop(runtime, &output_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &status_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &args_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (path_val.type != VALUE_STRING || args_val.type != VALUE_STRING || status_var_val.type != VALUE_STRING ||
            output_var_val.type != VALUE_STRING) {
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (RUNLOCAL)");
            return false;
        }

        const char *exec_path = path_val.as.string ? path_val.as.string : "";
        char resolved_path[1024];
        if (exec_path[0] != '\0' &&
            (exec_path[0] == '.' || strchr(exec_path, '/') != NULL || strchr(exec_path, '\\') != NULL)) {
            if (!c64script_resolve_script_path(runtime, exec_path, resolved_path, sizeof(resolved_path))) {
                c64script_value_free(&path_val);
                c64script_value_free(&args_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&output_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
                return false;
            }
            exec_path = resolved_path;
        }

        char cmd[2048];
        if (args_val.as.string[0] == '\0') {
            snprintf(cmd, sizeof(cmd), "%s 2>&1", exec_path);
        } else {
            snprintf(cmd, sizeof(cmd), "%s %s 2>&1", exec_path, args_val.as.string);
        }

        FILE *pipe = popen(cmd, "r");
        if (!pipe) {
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            const char *prefix = "Failed to execute: ";
            size_t max_len = sizeof(runtime->error_msg) - strlen(prefix) - 1;
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s%.*s", prefix, (int)max_len, exec_path);
            return false;
        }

        const size_t max_output = 1024 * 1024;
        char *output = malloc(max_output);
        if (!output) {
            pclose(pipe);
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
            return false;
        }

        size_t total_read = 0;
        while (total_read < max_output - 1) {
            size_t nread = fread(output + total_read, 1, max_output - 1 - total_read, pipe);
            if (nread == 0)
                break;
            total_read += nread;
        }
        output[total_read] = '\0';

        int exit_code = pclose(pipe);
#ifndef _WIN32
        if (WIFEXITED(exit_code)) {
            exit_code = WEXITSTATUS(exit_code);
        } else {
            exit_code = -1;
        }
#endif

        if (status_var_val.as.string[0] != '\0') {
            c64script_value_t status = {.type = VALUE_NUMBER, .as.number = (double)exit_code};
            c64script_runtime_set_var(runtime, status_var_val.as.string, status);
        }

        if (output_var_val.as.string[0] != '\0') {
            c64script_value_t output_value = {.type = VALUE_STRING, .as.string = output};
            c64script_runtime_set_var(runtime, output_var_val.as.string, output_value);
        }

        free(output);
        c64script_value_free(&path_val);
        c64script_value_free(&args_val);
        c64script_value_free(&status_var_val);
        c64script_value_free(&output_var_val);
        break;
    }

    case OP_READFILE: {
        c64script_value_t path_val;
        c64script_value_t var_name_val;
        if (!c64script_runtime_pop(runtime, &var_name_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (var_name_val.type != VALUE_STRING) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE variable name)");
            return false;
        }

        if (path_val.type != VALUE_STRING) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE path)");
            return false;
        }

        char resolved_path[1024];
        if (!c64script_resolve_script_path(runtime, path_val.as.string, resolved_path, sizeof(resolved_path))) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
            return false;
        }

        char *content = NULL;
        char err[256] = {0};
        if (!load_text_file(resolved_path, &content, err, sizeof(err))) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to read file");
            return false;
        }

        c64script_value_t content_val = {.type = VALUE_STRING, .as.string = content};
        c64script_runtime_set_var(runtime, var_name_val.as.string, content_val);

        c64script_value_free(&content_val);
        c64script_value_free(&var_name_val);
        c64script_value_free(&path_val);
        break;
    }

    case OP_WRITEFILE_APPEND:
    case OP_WRITEFILE_TRUNCATE: {
        bool truncate = (instr->opcode == OP_WRITEFILE_TRUNCATE);
        c64script_value_t content_val;
        c64script_value_t path_val;
        if (!c64script_runtime_pop(runtime, &content_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (path_val.type != VALUE_STRING) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE path)");
            return false;
        }

        const char *content_str = NULL;
        char num_buf[64];
        if (content_val.type == VALUE_STRING) {
            content_str = content_val.as.string ? content_val.as.string : "";
        } else if (content_val.type == VALUE_NUMBER) {
            if (!c64script_builtin_str(content_val.as.number, num_buf, sizeof(num_buf))) {
                c64script_value_free(&path_val);
                c64script_value_free(&content_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "WRITEFILE conversion failed");
                return false;
            }
            content_str = num_buf;
        } else {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE content)");
            return false;
        }

        char resolved_path[1024];
        if (!c64script_resolve_script_path(runtime, path_val.as.string, resolved_path, sizeof(resolved_path))) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
            return false;
        }

        char err[256] = {0};
        if (!write_file(resolved_path, content_str, truncate, err, sizeof(err))) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to write file");
            return false;
        }

        c64script_value_free(&path_val);
        c64script_value_free(&content_val);
        break;
    }

    case OP_HTTP: {
        c64script_value_t response_var_val, status_var_val, body_val, headers_val, url_val;
        if (!c64script_runtime_pop(runtime, &url_val))
            return false;
        if (!c64script_runtime_pop(runtime, &headers_val))
            return false;
        if (!c64script_runtime_pop(runtime, &body_val))
            return false;
        if (!c64script_runtime_pop(runtime, &status_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &response_var_val))
            return false;

        if (url_val.type != VALUE_STRING) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP url)");
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            return false;
        }

        const bool has_status_var =
            (status_var_val.type == VALUE_STRING && status_var_val.as.string && status_var_val.as.string[0]);
        const bool has_response_var =
            (response_var_val.type == VALUE_STRING && response_var_val.as.string && response_var_val.as.string[0]);
        const char *body_str = NULL;
        char body_buf[64];
        if (body_val.type == VALUE_STRING) {
            body_str = body_val.as.string ? body_val.as.string : "";
        } else if (body_val.type == VALUE_NUMBER) {
            if (!c64script_builtin_str(body_val.as.number, body_buf, sizeof(body_buf))) {
                c64script_value_free(&url_val);
                c64script_value_free(&headers_val);
                c64script_value_free(&body_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&response_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP body conversion failed");
                return false;
            }
            body_str = body_buf;
        } else if (body_val.type != VALUE_STRING) {
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP body)");
            return false;
        }

        struct curl_slist *headers = NULL;
        if (headers_val.type == VALUE_STRING) {
            if (headers_val.as.string && headers_val.as.string[0] != '\0') {
                headers = curl_slist_append(headers, headers_val.as.string);
            }
        } else if (headers_val.type == VALUE_MAP) {
            c64script_map_t *map = headers_val.as.map;
            if (map) {
                for (size_t i = 0; i < map->count; i++) {
                    const char *key = map->entries[i].key;
                    const c64script_value_t *value = &map->entries[i].value;
                    if (!key) {
                        continue;
                    }

                    const char *value_str = NULL;
                    char value_buf[64];
                    if (value->type == VALUE_STRING) {
                        value_str = value->as.string ? value->as.string : "";
                    } else if (value->type == VALUE_NUMBER) {
                        if (!c64script_builtin_str(value->as.number, value_buf, sizeof(value_buf))) {
                            c64script_value_free(&url_val);
                            c64script_value_free(&headers_val);
                            c64script_value_free(&body_val);
                            c64script_value_free(&status_var_val);
                            c64script_value_free(&response_var_val);
                            curl_slist_free_all(headers);
                            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP header conversion failed");
                            return false;
                        }
                        value_str = value_buf;
                    } else {
                        c64script_value_free(&url_val);
                        c64script_value_free(&headers_val);
                        c64script_value_free(&body_val);
                        c64script_value_free(&status_var_val);
                        c64script_value_free(&response_var_val);
                        curl_slist_free_all(headers);
                        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP headers)");
                        return false;
                    }

                    size_t header_len = strlen(key) + strlen(value_str) + 3;
                    char *header_line = malloc(header_len);
                    if (!header_line) {
                        c64script_value_free(&url_val);
                        c64script_value_free(&headers_val);
                        c64script_value_free(&body_val);
                        c64script_value_free(&status_var_val);
                        c64script_value_free(&response_var_val);
                        curl_slist_free_all(headers);
                        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                        return false;
                    }
                    snprintf(header_line, header_len, "%s: %s", key, value_str);
                    headers = curl_slist_append(headers, header_line);
                    free(header_line);
                }
            }
        } else if (headers_val.type != VALUE_STRING) {
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP headers)");
            return false;
        }

        c64script_http_response_t response = {.data = NULL, .size = 0};
        CURL *curl = curl_easy_init();
        if (!curl) {
            curl_slist_free_all(headers);
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP initialization failed");
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, url_val.as.string);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        switch (instr->operand) {
        case 0: // GET
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case 1: // POST
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        case 2: // PUT
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        case 3: // DELETE
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            if (body_str) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body_str));
            }
            break;
        case 4: // PATCH
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        default:
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid HTTP method");
            return false;
        }

        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(curl);
        long status_code = 0;
        const char *error_text = NULL;

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        } else {
            error_text = curl_easy_strerror(res);
            if (!has_status_var) {
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                c64script_value_free(&url_val);
                c64script_value_free(&headers_val);
                c64script_value_free(&body_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&response_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP request failed: %s", error_text);
                free(response.data);
                return false;
            }
        }

        if (has_status_var) {
            c64script_value_t status_value = {.type = VALUE_NUMBER, .as.number = (double)status_code};
            c64script_runtime_set_var(runtime, status_var_val.as.string, status_value);
        }

        if (has_response_var) {
            const char *resp_text = response.data ? response.data : (error_text ? error_text : "");
            c64script_value_t response_value = {.type = VALUE_STRING, .as.string = strdup(resp_text)};
            c64script_runtime_set_var(runtime, response_var_val.as.string, response_value);
            c64script_value_free(&response_value);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(response.data);
        c64script_value_free(&url_val);
        c64script_value_free(&headers_val);
        c64script_value_free(&body_val);
        c64script_value_free(&status_var_val);
        c64script_value_free(&response_var_val);
        break;
    }

    default:
        return false;
    }

    return true;
}
