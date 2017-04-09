/*
 * Copyright (C) 2017 Paulo Pacheco
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef __NGX_JSON_LOG_TEXT_H__
#define __NGX_JSON_LOG_TEXT_H__

#include <ngx_http.h>
#include <ngx_http_variables.h>
#include <ngx_stream.h>
#include <ngx_stream_variables.h>

typedef enum {
    NGX_JSON_LOG_HTTP = 0,
    NGX_JSON_LOG_STREAM = 1,
} ngx_json_log_module_type_e;

struct ngx_json_log_format_s {
    ngx_str_t                   name;           /* the format name */
    ngx_str_t                   config;         /* value at config files */
    ngx_array_t                 *items;         /* format items */
    ngx_http_complex_value_t    *http_filter;   /* filter output */
    ngx_stream_complex_value_t  *stream_filter; /* filter output */
};
typedef struct ngx_json_log_format_s     ngx_json_log_format_t;

char *
ngx_http_json_log_loc_format_block(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

char *
ngx_stream_json_log_srv_format_block(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);


const char * ngx_json_log_type_string();
const char * ngx_json_log_type_integer();
const char * ngx_json_log_type_real();
const char * ngx_json_log_type_true();
const char * ngx_json_log_type_false();
const char * ngx_json_log_type_null();

/* Global alloc funcs registration */
void
ngx_json_log_set_alloc_funcs();

void
set_current_mem_pool(ngx_pool_t *pool);

/* Dumps to text format the JSON for the items for this request. */
char *
ngx_json_log_items_dump_text(ngx_json_log_module_type_e type,
        void *rs, ngx_array_t *items);

/* Compares two items by name */
ngx_int_t
ngx_json_log_items_cmp(const void *left, const void *right);

#endif // __NGX_JSON_LOG_TEXT_H__

