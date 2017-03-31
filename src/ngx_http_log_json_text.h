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
#ifndef __NGX_HTTP_LOG_JSON_TEXT_H__
#define __NGX_HTTP_LOG_JSON_TEXT_H__

struct ngx_http_log_json_item_s {
    const char                           *type;
    ngx_str_t                            *name;
    ngx_str_t                            var_name;
    ngx_int_t                            is_array;
    ngx_http_compile_complex_value_t     *ccv;
};

typedef struct ngx_http_log_json_item_s  ngx_http_log_json_item_t;

const char * ngx_http_log_json_type_string();
const char * ngx_http_log_json_type_integer();
const char * ngx_http_log_json_type_real();
const char * ngx_http_log_json_type_true();
const char * ngx_http_log_json_type_false();
const char * ngx_http_log_json_type_null();

/* Global alloc funcs registration */
void
ngx_http_log_json_set_alloc_funcs();

void
set_current_mem_pool(ngx_pool_t *pool);

/* Dumps to text format the JSON for the items for this request. */
char *
ngx_http_log_json_items_dump_text(ngx_http_request_t *r, ngx_array_t *items);

#endif // __NGX_HTTP_LOG_JSON_TEXT_H__

