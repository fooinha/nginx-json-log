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
#include <ngx_stream.h>

#include <jansson.h>

typedef struct {
    ngx_flag_t      enabled;
} ngx_stream_json_log_preread_srv_conf_t;

typedef struct {
    ngx_str_t      payload;
    u_char         *pos;
    u_char         *last;
    ngx_log_t      *log;
    ngx_pool_t     *pool;
} ngx_stream_json_log_preread_ctx_t;

static ngx_int_t ngx_stream_json_log_preread_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_stream_json_log_preread_handler(ngx_stream_session_t *s);
static void *ngx_stream_json_log_preread_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_json_log_preread_merge_srv_conf(ngx_conf_t *cf,
        void *parent, void *child);
static ngx_int_t ngx_stream_json_log_preread_init(ngx_conf_t *cf);


static ngx_command_t  ngx_stream_json_log_preread_commands[] = {
      ngx_null_command
};

static ngx_stream_module_t  ngx_stream_json_log_preread_module_ctx = {
    ngx_stream_json_log_preread_add_variables,          /* preconfiguration */
    ngx_stream_json_log_preread_init,                  /* postconfiguration */

    NULL,                                      /* create main configuration */
    NULL,                                        /* init main configuration */

    ngx_stream_json_log_preread_create_srv_conf, /* create server configuration */
    ngx_stream_json_log_preread_merge_srv_conf   /* merge server configuration */
};


ngx_module_t  ngx_stream_json_log_preread_module = {
    NGX_MODULE_V1,
    &ngx_stream_json_log_preread_module_ctx,     /* module context */
    ngx_stream_json_log_preread_commands,        /* module directives */
    NGX_STREAM_MODULE,                           /* module type */
    NULL,                                        /* init master */
    NULL,                                        /* init module */
    NULL,                                        /* init process */
    NULL,                                        /* init thread */
    NULL,                                        /* exit thread */
    NULL,                                        /* exit process */
    NULL,                                        /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_stream_json_log_preread_handler(ngx_stream_session_t *s)
{

    ngx_connection_t                        *c;
    ngx_stream_json_log_preread_ctx_t       *ctx;
//    ngx_stream_json_log_preread_srv_conf_t  *sscf;

    c = s->connection;

//    sscf = ngx_stream_get_module_srv_conf(s, ngx_stream_json_log_preread_module);

//    if (!sscf->enabled) {
//        return NGX_DECLINED;
//    }

    if (c->type != SOCK_STREAM) {
        return NGX_DECLINED;
    }

    if (c->buffer == NULL) {
        return NGX_AGAIN;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_json_log_preread_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(c->pool, sizeof(ngx_stream_json_log_preread_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_stream_set_ctx(s, ctx, ngx_stream_json_log_preread_module);

        ctx->pool = c->pool;
        ctx->log = c->log;
        ctx->pos = c->buffer->pos;
    }

    ctx->last = c->buffer->last;

    //printf("H P>%p L>%p S>%lu\n" ,
    //        ctx->pos, ctx->last, ctx->last-ctx->pos);

    return NGX_OK;
}


static void *
ngx_stream_json_log_preread_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_json_log_preread_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
            sizeof(ngx_stream_json_log_preread_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_stream_json_log_preread_merge_srv_conf(ngx_conf_t *cf, void *parent,
        void *child)
{
    ngx_stream_json_log_preread_srv_conf_t *prev = parent;
    ngx_stream_json_log_preread_srv_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_stream_json_log_preread_init(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_json_log_preread_handler;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_json_log_preread_payload_variable(ngx_stream_session_t *s,
    ngx_variable_value_t *v, uintptr_t data)
{
    ngx_stream_json_log_preread_ctx_t  *ctx;
    size_t                              len;
    ngx_str_t                           payload;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_json_log_preread_module);

    if (ctx == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = ctx->last-ctx->pos;

    if (ctx->payload.data == NULL && len) {

        ctx->payload.len = ngx_base64_encoded_length(len);
        ctx->payload.data = ngx_pcalloc(ctx->pool, ctx->payload.len);
        if (ctx->payload.data == NULL) {
            ctx->payload.len = 0;
            return NGX_ERROR;
        }
        payload.data = ctx->pos;
        payload.len = len;

        ngx_encode_base64(&ctx->payload, &payload);
    }

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->len = ctx->payload.len;
    v->data = ctx->payload.data;

    return NGX_OK;
}


//FIXME
//static ngx_int_t
//ngx_stream_json_log_preread_payload_hex_variable(ngx_stream_session_t *s,
//    ngx_variable_value_t *v, uintptr_t data)
//{
//    ngx_stream_json_log_preread_ctx_t  *ctx;
//    size_t                              len;
//    ngx_str_t                           payload;
//
//    ctx = ngx_stream_get_module_ctx(s, ngx_stream_json_log_preread_module);
//
//    if (ctx == NULL) {
//        v->not_found = 1;
//        return NGX_OK;
//    }
//
//    len = ctx->last-ctx->pos;
//
//    if (ctx->payload.data == NULL && len) {
//        //FIXME
//    }
//
//    v->valid = 1;
//    v->no_cacheable = 0;
//    v->not_found = 0;
//
//    return NGX_OK;
//}


static ngx_int_t
ngx_stream_json_log_preread_add_variables(ngx_conf_t *cf)
{
    ngx_stream_variable_t *var;
    ngx_str_t              payload = ngx_string("ngx_stream_json_log_payload");

//    ngx_stream_variable_t *var_hex;
//    ngx_str_t              payload_hex =
//        ngx_string("ngx_stream_json_log_payload_hexdump");

    var = ngx_stream_add_variable(cf, &payload, 0);
    var->get_handler = ngx_stream_json_log_preread_payload_variable;

//    var_hex = ngx_stream_add_variable(cf, &payload_hex, 0);
//    var_hex->get_handler = ngx_stream_json_log_preread_payload_hex_variable;

    return NGX_OK;
}

