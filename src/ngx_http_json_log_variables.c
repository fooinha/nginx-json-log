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
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_json_log_variables.h"
#include "ngx_json_log_text.h"
#include "ngx_json_log_str.h"

#include <jansson.h>

typedef ngx_queue_t *(*get_body_queue_pt)(ngx_http_request_t *r);

static ngx_int_t
ngx_http_json_log_get_variable_req_headers(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data);

/* variables list */
static ngx_http_variable_t  ngx_http_json_log_variables_list[] = {
        {   ngx_string("http_json_log_req_headers"),
            NULL,
            ngx_http_json_log_get_variable_req_headers,
            0, 0, 0
        },
        {   ngx_string("http_json_log_resp_headers"),
            ngx_http_json_log_set_variable_resp_headers,
            NULL,
            0, 0, 0
        },
        {   ngx_string("http_json_log_req_body"),
            ngx_http_json_log_set_variable_req_body,
            NULL,
            0, 0, 0
        },
        {   ngx_string("http_json_err_log_req"),
            ngx_http_json_log_set_variable_req_body,
            NULL,
            0, 0, 0
        },
        {   ngx_string("http_json_log_req_body_hexdump"),
            ngx_http_json_log_set_variable_req_body_hexdump,
            NULL,
            0, 0, 0
        },
        {   ngx_string("http_json_err_log_req_hexdump"),
            ngx_http_json_log_set_variable_req_body_hexdump,
            NULL,
            0, 0, 0
        }
};


ngx_http_variable_t *
ngx_http_json_log_variables(size_t *len)
{
    *len =
      sizeof(ngx_http_json_log_variables_list)
      / sizeof(ngx_http_variable_t);

    return ngx_http_json_log_variables_list;
}


ngx_int_t
ngx_http_json_log_is_local_variable(ngx_str_t *name)
{
    size_t len = 0, i = 0;
    ngx_http_variable_t * vars;

    if (!name || !name->data || !name->len) {
        return 0;
    }

    vars = ngx_http_json_log_variables(&len);
    for (i = 0; i < len; i++) {
        if (ngx_strncmp(vars[i].name.data, name->data, vars[i].name.len) == 0) {
            return 1;
        }
    }

    return 0;
}


ngx_int_t
ngx_http_json_log_local_variable_needs_body_filter(ngx_str_t *name)
{
    if (!name || !name->data || !name->len) {
        return 0;
    }

    if (ngx_strncmp("http_json_log_req_body", name->data,
                sizeof("http_json_log_req_body")) == 0) {
        return 1;
    }
    if (ngx_strncmp("http_json_log_req_body_hexdump", name->data,
                sizeof("http_json_log_req_body_hexdump")) == 0) {
        return 1;
    }
    return NGX_ERROR;
}


ngx_int_t
ngx_http_json_log_local_variable_needs_header_filter(ngx_str_t *name)
{
    if (!name || !name->data || !name->len) {
        return 0;
    }

    if (ngx_strncmp("http_json_log_resp_headers", name->data,
                sizeof("http_json_log_resp_headers")) == 0) {
        return 1;
    }
    return 0;
}


void
ngx_http_json_log_register_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t          *v;
    ngx_http_variable_t          *local_vars = NULL;
    size_t                        l = 0;
    size_t                        local_vars_len;

    local_vars = ngx_http_json_log_variables(&local_vars_len);
    /* Register variables */
    for (l = 0; l < local_vars_len ; ++l) {
        v = ngx_http_add_variable(cf,
                &local_vars[l].name, local_vars[l].flags);
        if (v == NULL) {
            continue;
        }
        *v = local_vars[l];
    }
}


static ngx_int_t
ngx_http_json_log_get_variable_req_headers(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t                    i;
    ngx_list_part_t              *part = &r->headers_in.headers.part;
    ngx_table_elt_t              *header = part->elts;
    char                         *key = NULL;
    json_t                       *object = json_object();
#if JANSSON_VERSION_HEX < 0x020700
    char                         *buf;
#endif

    for (i = 0; i < part->nelts ; ++i) {
        if (!header[i].key.data  || !header[i].key.len) {
            continue;
        }
        key = ngx_pcalloc(r->pool, header[i].key.len + 1);
        if (!key) {
            return NGX_ERROR;
        }
        ngx_memcpy(key, header[i].key.data, header[i].key.len);
#if JANSSON_VERSION_HEX >= 0x020700
        json_object_set(object, (const char *) key,
                json_stringn((const char *) header[i].value.data,
                header[i].value.len));
#else
        buf = ngx_pcalloc(r->pool, header[i].value.len + 1);
        if (buf) {
            ngx_memcpy(buf,  header[i].value.data, header[i].value.len);
            json_object_set(object, (const char *) key, json_string(buf));
        }

#endif
    }
    v->data = (void *) object;

    return NGX_OK;
}


void
ngx_http_json_log_set_variable_resp_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t                        i;
    ngx_table_elt_t              *header;
    ngx_array_t                  *headers = (ngx_array_t *) data;
    json_t                       *object;
    char                         *key;
#if JANSSON_VERSION_HEX < 0x020700
    char                         *buf;
#endif

    if (!headers) {
        return;
    }

    /* FIXME Maybe called with strange values */
    if (headers->nelts > 256) {
        return;
    }

    set_current_mem_pool(r->pool);
    object = json_object();
    if (!object) {
        return;
    }

    header = headers->elts;
    for (i = 0; i < headers->nelts; ++i) {

        if (!header[i].key.data  || !header[i].key.len) {
            continue;
        }
        key = ngx_pcalloc(r->pool, header[i].key.len + 1);
        if (!key) {
            return;
        }
        ngx_memcpy(key, header[i].key.data, header[i].key.len);
#if JANSSON_VERSION_HEX >= 0x020700
        json_object_set(object, (const char *) key,
                json_stringn((const char *) header[i].value.data,
                header[i].value.len));
#else
        buf = ngx_pcalloc(r->pool, header[i].value.len + 1);
        if (buf) {
            ngx_memcpy(buf,  header[i].value.data, header[i].value.len);
            json_object_set(object, (const char *) key, json_string(buf));
        }
#endif
    }

    v->valid = 1;
    v->data = (void *) object;

    set_current_mem_pool(NULL);
}


void
ngx_http_json_log_set_variable_req_body(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t                          base64;
    json_t                            *object = NULL;
    ngx_str_t                         *payload = (ngx_str_t *) data;
#if JANSSON_VERSION_HEX < 0x020700
    char                              *buf;
#endif

    if (!payload || !payload->data) {
        return;
    }

    /* Empty payload */
    if (!payload->len) {
        set_current_mem_pool(r->pool);
#if JANSSON_VERSION_HEX >= 0x020700
        object = json_stringn((const char *) "", 0);
#else
        object = json_string((const char *) "");
#endif
        if (object) {
            v->valid = 1;
            v->data = (void *) object;
        }
        set_current_mem_pool(NULL);
        return;
    }

    base64.len = ngx_base64_encoded_length(payload->len);
    base64.data = ngx_pcalloc(r->pool, base64.len);
    if (!base64.data) {
        return;
    }

    ngx_encode_base64(&base64, payload);

    set_current_mem_pool(r->pool);

#if JANSSON_VERSION_HEX >= 0x020700
    object = json_stringn((const char *) base64.data, base64.len);
#else
    buf = ngx_pcalloc(r->pool, base64.len + 1);
    if (buf) {
        ngx_memcpy(buf, base64.data, base64.len);
        object = json_string(buf);
    }
#endif

    if (object) {
        v->valid = 1;
        v->data = (void *) object;
    }
    set_current_mem_pool(NULL);
}


void
ngx_http_json_log_set_variable_req_body_hexdump(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t                          hexdump;
    json_t                            *object = NULL;
    json_t                            *value = NULL;
    ngx_str_t                         *payload = (ngx_str_t *) data;
    size_t                             start, i;
#if JANSSON_VERSION_HEX < 0x020700
    char                              *buf;
#endif

    if (!payload || !payload->data) {
        return;
    }

    /* Empty payload */
    if (!payload->len) {
        set_current_mem_pool(r->pool);
#if JANSSON_VERSION_HEX >= 0x020700
        object = json_stringn((const char *) "", 0);
#else
        object = json_string((const char *) "");
#endif
        if (object) {
            v->valid = 1;
            v->data = (void *) object;
        }
        set_current_mem_pool(NULL);
        return;
    }

    hexdump.len = ngx_json_log_hexdump_length(payload->len, 16);
    hexdump.data = ngx_pcalloc(r->pool, hexdump.len);
    if (!hexdump.data) {
        return;
    }

    ngx_json_log_hexdump(payload, &hexdump);

    set_current_mem_pool(r->pool);

    object = json_array();
    start = 0;
    for (i = 0; i < hexdump.len; ++i) {
        if (hexdump.data[i] == '\n') {
#if JANSSON_VERSION_HEX >= 0x020700
            value = json_stringn((const char *) hexdump.data+start, (i-start));
#else
            buf = ngx_pcalloc(r->pool, (i-start) + 1);
            if (buf) {
                ngx_memcpy(buf, hexdump.data+start, (i-start));
                object = json_string(buf);
            }
#endif
            json_array_append(object, value);
            ++i;
            start = i;
        }
    }

    if (object) {
        v->valid = 1;
        v->data = (void *) object;
    }
    set_current_mem_pool(NULL);
}
