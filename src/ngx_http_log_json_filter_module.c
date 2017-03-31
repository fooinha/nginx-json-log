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
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>

#include "ngx_http_log_json_module.h"
#include "ngx_http_log_json_variables.h"

#define HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT 512

struct ngx_http_log_json_req_body_s {
    ngx_http_request_t                       *r;
    ngx_str_t                                 payload;
    ngx_queue_t                               queue;
};

typedef struct ngx_http_log_json_req_body_s ngx_http_log_json_req_body_t;
typedef struct ngx_http_log_json_resp_headers_s
    ngx_http_log_json_resp_headers_t;

/* main config state */
struct ngx_http_log_json_filter_main_conf_s {
};

/* location config */
struct ngx_http_log_json_filter_loc_conf_s {
    size_t                req_body_limit;
};

typedef struct ngx_http_log_json_filter_main_conf_s
    ngx_http_log_json_filter_main_conf_t;

typedef struct ngx_http_log_json_filter_loc_conf_s
    ngx_http_log_json_filter_loc_conf_t;

static void *
ngx_http_log_json_filter_create_main_conf(ngx_conf_t *cf);

static void *
ngx_http_log_json_filter_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t
ngx_http_log_json_filter_init(ngx_conf_t *cf);

static char *
ngx_http_log_json_loc_req_body_limit(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static ngx_command_t ngx_http_log_json_filter_commands[] = {
    { ngx_string("http_log_json_req_body_limit"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_log_json_loc_req_body_limit,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    }
};

/* config preparation */
static ngx_http_module_t ngx_http_log_json_filter_module_ctx = {
    NULL,                                      /* preconfiguration */
    ngx_http_log_json_filter_init,             /* postconfiguration */
    ngx_http_log_json_filter_create_main_conf, /* create main configuration */
    NULL,                                      /* init main configuration */
    NULL,                                      /* create server configuration */
    NULL,                                      /* merge server configuration */
    ngx_http_log_json_filter_create_loc_conf,  /* create location configuration */
    NULL                                       /* merge location configuration */
};

ngx_module_t ngx_http_log_json_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_log_json_filter_module_ctx,  /* module context */
    ngx_http_log_json_filter_commands,     /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static char *
ngx_http_log_json_loc_req_body_limit(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf) {

    ngx_http_log_json_filter_loc_conf_t  *lc = conf;
    ngx_str_t                            *args = cf->args->elts;
    size_t                               sp = NGX_ERROR;


    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid argument for HTTP request body limit");
        return NGX_CONF_ERROR;
    }

    sp = ngx_parse_size(&args[1]);
    if (sp == (size_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid argument for HTTP request body limit");
        return NGX_CONF_ERROR;
    }

    if (sp == 0) {
        sp = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;
    }

    lc->req_body_limit = sp;


    return NGX_CONF_OK;
}


static size_t
ngx_http_log_json_get_req_body_limit(ngx_http_request_t *r) {
    ngx_http_log_json_filter_loc_conf_t        *lc;

    lc = ngx_http_get_module_loc_conf(r, ngx_http_log_json_filter_module);

    if (!lc) {
        return HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;
    }
    return lc->req_body_limit;
}

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_int_t
ngx_http_log_json_header_filter(ngx_http_request_t *r) {

    ngx_uint_t                          i;
    ngx_list_part_t                     *part = &r->headers_out.headers.part;
    ngx_table_elt_t                     *header = part->elts;
    ngx_table_elt_t                     *header_copy;
    ngx_array_t                         *headers;

    ngx_str_t                           lcname;
    ngx_http_variable_value_t           *vv;
    ngx_uint_t                          varkey;
    ngx_str_t                   name = ngx_string("http_log_json_resp_headers");

    if (!ngx_http_log_json_needs_header_filter()) {
        return ngx_http_next_header_filter(r);
    }

    if (!part->nelts) {
        return ngx_http_next_header_filter(r);
    }

    lcname.len = name.len;
    lcname.data = ngx_pcalloc(r->pool, name.len);
    varkey = ngx_hash_strlow(lcname.data,
            name.data, name.len);

    vv = ngx_http_get_variable(r, &lcname, varkey);

    if (!vv) {
        return ngx_http_next_header_filter(r);
    }

    headers = ngx_array_create(r->pool, part->nelts, sizeof(ngx_table_elt_t));
    if (!headers) {
        return ngx_http_next_header_filter(r);
    }

    for (i = 0; i < part->nelts ; ++i) {

        header_copy = ngx_array_push(headers);
        header_copy->hash = -1;
        header_copy->lowcase_key  = NULL;

        header_copy->key.data = ngx_pcalloc(r->pool, header[i].key.len);
        if (!header_copy->key.data) {
            continue;
        }
        ngx_memcpy(header_copy->key.data,
                header[i].key.data, header[i].key.len);
        header_copy->key.len = header[i].key.len;

        header_copy->value.data = ngx_pcalloc(r->pool, header[i].value.len);
        if (!header_copy->value.data) {
            continue;
        }
        ngx_memcpy(header_copy->value.data,
                header[i].value.data, header[i].value.len);
        header_copy->value.len = header[i].value.len;

    }

    if (headers->nelts < 1) {
        return ngx_http_next_header_filter(r);
    }

    ngx_http_log_json_set_variable_resp_headers(r, vv, (uintptr_t) headers);

    return ngx_http_next_header_filter(r);
}

static ngx_http_request_body_filter_pt  ngx_http_next_request_body_filter;

/* save body filter */
static ngx_int_t
ngx_http_log_json_body_filter(ngx_http_request_t *r, ngx_chain_t *in) {


    ngx_str_t                  lcname;
    ngx_http_variable_value_t  *vv;
    ngx_uint_t                 varkey;
    ngx_str_t                  name = ngx_string("http_log_json_req_body");

    ngx_chain_t                *cl;
    size_t                     len = 0;
    size_t                     count = 0;
    u_char                     *pos = NULL;
    ngx_str_t                  payload;
    size_t                     limit = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;

    if (!ngx_http_log_json_needs_body_filter()) {
        return ngx_http_next_request_body_filter(r, in);
    }
    limit = ngx_http_log_json_get_req_body_limit(r);
    payload.len = 0;
    /* Finds out the len below limit bytes to save */
    for (cl = in; cl; cl = cl->next) {

        len = cl->buf->last - cl->buf->pos;
        if (len < (limit - payload.len)) {
            payload.len += len;
        } else {
            len = (limit - payload.len);
            payload.len = limit;
        }

        if (payload.len >= limit) {
            payload.len = limit;
            break;
        }
    }
    /* Nothing to save from this iteration */
    if (!payload.len) {
        return ngx_http_next_request_body_filter(r, in);
    }

    //TODO:  support body append from multiple filter calls
    payload.data = ngx_pcalloc(r->pool, payload.len);
    if (! payload.data) {
        //TODO: WARN
        return ngx_http_next_request_body_filter(r, in);
    }

    /* Saves body payload */
    pos = payload.data;
    count = 0;
    for (cl = in; cl; cl = cl->next) {

        len = cl->buf->last - cl->buf->pos;
        if (count + len >= payload.len) {
            count = payload.len;
            len = payload.len;
        } else {
            count += len;
        }

        ngx_memcpy(pos, cl->buf->pos, len);
        pos += len;
        if (count >= limit) {
            break;
        }
    }

    lcname.len = name.len;
    lcname.data = ngx_pcalloc(r->pool, name.len);
    varkey = ngx_hash_strlow(lcname.data, name.data, name.len);

    vv = ngx_http_get_variable(r, &lcname, varkey);

    if (!vv) {
        return ngx_http_next_header_filter(r);
    }

    ngx_http_log_json_set_variable_req_body(r, vv, (uintptr_t) &payload);
    return ngx_http_next_request_body_filter(r, in);
}

static ngx_int_t
ngx_http_log_json_filter_init(ngx_conf_t *cf) {

    if (ngx_http_log_json_needs_body_filter()) {
        ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
        ngx_http_top_request_body_filter = ngx_http_log_json_body_filter;
    }

    if (ngx_http_log_json_needs_header_filter()) {
        ngx_http_next_header_filter = ngx_http_top_header_filter;
        ngx_http_top_header_filter = ngx_http_log_json_header_filter;
    }
    return NGX_OK;
}

static void *
ngx_http_log_json_filter_create_main_conf(ngx_conf_t *cf) {

    ngx_http_log_json_filter_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_json_filter_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}

static void *
ngx_http_log_json_filter_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_log_json_filter_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_json_filter_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->req_body_limit = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;

    return conf;
}
