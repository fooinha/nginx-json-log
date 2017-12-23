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

#include "ngx_http_json_log_module.h"
#include "ngx_http_json_log_variables.h"
#include "ngx_json_log_text.h"
#include "ngx_json_log_kafka.h"
#include "ngx_json_log_output.h"

#define HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT 512

/* data structures */


typedef struct ngx_http_json_log_main_conf_s     ngx_http_json_log_main_conf_t;

#if (NGX_HAVE_LIBRDKAFKA)
/* configuration kafka constants */
static ngx_int_t   http_json_log_filter_has_kafka_locations   = NGX_CONF_UNSET;
#endif

struct ngx_http_json_log_req_body_s {
    ngx_http_request_t                       *r;
    ngx_str_t                                 payload;
    ngx_queue_t                               queue;
};

typedef struct ngx_http_json_log_req_body_s ngx_http_json_log_req_body_t;
typedef struct ngx_http_json_log_resp_headers_s
    ngx_http_json_log_resp_headers_t;

/* main config */
struct ngx_http_json_log_filter_main_conf_s {
    ngx_array_t                               *formats;
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_json_log_main_kafka_conf_t             kafka;
#endif
};
typedef struct ngx_http_json_log_filter_main_conf_s
    ngx_http_json_log_filter_main_conf_t;

/* location config */
struct ngx_http_json_log_filter_loc_conf_s {
    size_t                req_body_limit;
};


typedef struct ngx_http_json_log_filter_loc_conf_s
    ngx_http_json_log_filter_loc_conf_t;

static void *
ngx_http_json_log_filter_create_main_conf(ngx_conf_t *cf);

static void *
ngx_http_json_log_filter_create_srv_conf(ngx_conf_t *cf);

static void *
ngx_http_json_log_filter_create_loc_conf(ngx_conf_t *cf);


static ngx_int_t
ngx_http_json_log_filter_init(ngx_conf_t *cf);

static ngx_int_t
ngx_http_json_log_filter_init_worker(ngx_cycle_t *cycle);


/* Configuration callbacks */
static char *
ngx_http_json_log_loc_req_body_limit(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static char *
ngx_http_json_log_srv_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t ngx_http_json_log_filter_commands[] = {
    { ngx_string("http_json_log_req_body_limit"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_json_log_loc_req_body_limit,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    { ngx_string("json_err_log_format"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2|NGX_CONF_TAKE3,
        ngx_http_json_log_main_format_block,
        NGX_HTTP_MAIN_CONF_OFFSET,
        0,
        NULL
    },
    /* OUTPUT LOCATION */
    { ngx_string("json_err_log"),
        NGX_HTTP_SRV_CONF|NGX_CONF_TAKE2,
        ngx_http_json_log_srv_output,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL
    },
#if (NGX_HAVE_LIBRDKAFKA)
    /* KAFKA */
    {
        ngx_string("json_err_log_kafka_client_id"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.client_id),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_brokers"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_1MORE,
        ngx_conf_set_str_array_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.brokers),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_compression"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.compression),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_partition"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.partition),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_log_level"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.log_level),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_max_retries"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.max_retries),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_buffer_max_messages"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.buffer_max_messages),
        NULL
    },
    {
        ngx_string("json_err_log_kafka_backoff_ms"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_filter_main_conf_t, kafka.backoff_ms),
        NULL
    },
#endif
    ngx_null_command
};

/* config preparation */
static ngx_http_module_t ngx_http_json_log_filter_module_ctx = {
    NULL,                                      /* preconfiguration */
    ngx_http_json_log_filter_init,             /* postconfiguration */
    ngx_http_json_log_filter_create_main_conf, /* create main configuration */
    NULL,                                      /* init main configuration */
    ngx_http_json_log_filter_create_srv_conf,  /* create server configuration */
    NULL,                                      /* merge server configuration */
    ngx_http_json_log_filter_create_loc_conf,  /* create location configuration */
    NULL                                       /* merge location configuration */
};

ngx_module_t ngx_http_json_log_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_json_log_filter_module_ctx,  /* module context */
    ngx_http_json_log_filter_commands,     /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_json_log_filter_init_worker,  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t   http_json_log_needs_req_body_filter         = NGX_CONF_UNSET;
static ngx_int_t   http_json_log_needs_resp_headers_filter     = NGX_CONF_UNSET;
static ngx_int_t   http_json_log_needs_err_resp_headers_filter = NGX_CONF_UNSET;


ngx_int_t
ngx_http_json_log_needs_body_filter()
{
    return http_json_log_needs_req_body_filter != NGX_CONF_UNSET;
}


void
ngx_http_json_log_set_needs_body_filter()
{
    http_json_log_needs_req_body_filter = 1;
}


ngx_int_t
ngx_http_json_log_needs_header_filter()
{
    return http_json_log_needs_resp_headers_filter != NGX_CONF_UNSET;
}


void
ngx_http_json_log_set_needs_header_filter()
{
    http_json_log_needs_resp_headers_filter = 1;
}


ngx_int_t
ngx_http_json_log_needs_err_header_filter()
{
    return http_json_log_needs_err_resp_headers_filter != NGX_CONF_UNSET;
}


void
ngx_http_json_log_set_needs_err_header_filter()
{
    http_json_log_needs_err_resp_headers_filter = 1;
}


static char *
ngx_http_json_log_loc_req_body_limit(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf)
{
    ngx_http_json_log_filter_loc_conf_t  *lc = conf;
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
ngx_http_json_log_get_req_body_limit(ngx_http_request_t *r)
{
    ngx_http_json_log_filter_loc_conf_t        *lc;

    lc = ngx_http_get_module_loc_conf(r, ngx_http_json_log_filter_module);

    if (!lc) {
        return HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;
    }
    return lc->req_body_limit;
}


static void
ngx_http_json_log_header_bad_request(ngx_http_request_t *r)
{
    ngx_http_json_log_srv_conf_t            *lc;
    ngx_str_t                               filter_val;
    char                                    *txt;
    size_t                                  i, len;
    ngx_json_log_output_location_t          *arr;
    ngx_json_log_output_location_t          *location;
    size_t                     limit = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;
    ngx_str_t                  name = ngx_string("http_json_err_log_req");
    ngx_str_t                           lcname;
    ngx_http_variable_value_t           *vv;
    ngx_uint_t                          varkey;

    ngx_str_t            name_hex = ngx_string("http_json_err_log_req_hexdump");
    ngx_str_t                  lcname_hex;
    ngx_http_variable_value_t  *vv_hex;
    ngx_uint_t                 varkey_hex;

    ngx_str_t                           payload;

#if (NGX_HAVE_LIBRDKAFKA)
    ngx_str_t                               msg_id;
    ngx_http_json_log_filter_main_conf_t    *mcf;
#endif

    lc = ngx_http_get_module_srv_conf(r, ngx_http_json_log_filter_module);
    /* Location to eat http_json_log was not found */
    if (!lc) {
        return;
    }

    /* Bypass if number of location is empty */
    if (!lc->locations->nelts) {
        return;
    }

    if (r->header_in) {

        limit = ngx_http_json_log_get_req_body_limit(r);
        len = r->header_in->last - r->header_in->start;

        if (len > limit) {
            len = limit;
        }

        payload.data = ngx_pcalloc(r->pool, len);
        if (payload.data) {

            payload.len = len;
            ngx_memcpy(payload.data, r->header_in->start, len);

            lcname.len = name.len;
            lcname.data = ngx_pcalloc(r->pool, name.len);
            varkey = ngx_hash_strlow(lcname.data, name.data, name.len);
            vv = ngx_http_get_variable(r, &lcname, varkey);
            if (vv) {
                ngx_http_json_log_set_variable_req_body(r,
                        vv, (uintptr_t) &payload);
            }

            lcname_hex.len = name_hex.len;
            lcname_hex.data = ngx_pcalloc(r->pool, name_hex.len);
            varkey_hex = ngx_hash_strlow(lcname_hex.data,
                    name_hex.data, name_hex.len);
            vv_hex = ngx_http_get_variable(r, &lcname_hex, varkey_hex);
            if (vv_hex) {
                ngx_http_json_log_set_variable_req_body_hexdump(r,
                        vv_hex, (uintptr_t) &payload);
            }
        }
    }

#if (NGX_HAVE_LIBRDKAFKA)
    mcf = ngx_http_get_module_main_conf(r, ngx_http_json_log_filter_module);
#endif

    arr = lc->locations->elts;
    for (i = 0; i < lc->locations->nelts; ++i) {

        location = &arr[i];

        if (!location) {
            break;
        }

        /* Check filter result */
        if (location->format.http_filter != NULL) {
            if (ngx_http_complex_value(r,
                        location->format.http_filter, &filter_val) != NGX_OK) {
                /* WARN ? */
                continue;
            }

            if (filter_val.len == 0
                    || (filter_val.len == 1 && filter_val.data[0] == '0')) {
                continue;
            }
        }

        /* Get json text for items at this request */
        /*TODO: cache format output dump */
        txt = ngx_json_log_items_dump_text(NGX_JSON_LOG_HTTP, r,
                location->format.items);
        if (!txt) {
            /* WARN ? */
            continue;
        }

        /* Write to file */
        if (location->type == NGX_JSON_LOG_SINK_FILE) {

            if (!location->file) {
                continue;
            }

            if (ngx_json_log_write_sink_file(r->pool->log,
                        location->file->fd, txt) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0, "Write Error!");
            }
            continue;
        }

        /* Write to syslog */
        if (location->type == NGX_JSON_LOG_SINK_SYSLOG) {
            if (!location->syslog) {
                continue;
            }
            if (ngx_json_log_write_sink_syslog(r->pool->log,
                        r->pool, location->syslog, txt) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0, "Syslog write error!");
            }
            continue;
        }

#if (NGX_HAVE_LIBRDKAFKA)
        /* Write to kafka */
        if (location->type == NGX_JSON_LOG_SINK_KAFKA) {

            /* don't do anything if no kafka brokers to send */
            if (! mcf->kafka.valid_brokers) {
                continue;
            }

            if (location->kafka.rkt == NGX_CONF_UNSET_PTR ||
                    !location->kafka.rkt)  {
                /* configure and create topic */
                location->kafka.rkt =
                    ngx_json_log_kafka_topic_new(r->pool,
                            mcf->kafka.rk, location->kafka.rktc,
                            &location->location);
            }

            /* if failed to create topic */
            if (!location->kafka.rkt) {
                location->kafka.rkt = NGX_CONF_UNSET_PTR;
                /* WARN ?*/
                continue;
            }

            if (location->kafka.http_msg_id_var) {
                ngx_http_complex_value(r,
                        location->kafka.http_msg_id_var, &msg_id);
#if (NGX_DEBUG)
                ngx_log_error(NGX_LOG_DEBUG, r->pool->log, 0,
                        "http_json_log: kafka msg-id:[%v] msg:[%s]",
                        &msg_id, txt);
#endif
            }

            ngx_json_log_kafka_produce(r->pool, mcf->kafka.rk,
                    location->kafka.rkt,
                    mcf->kafka.partition, txt, &msg_id);

        } // if KAFKA type
#endif

    } // for server
}


static ngx_http_output_header_filter_pt ngx_http_next_header_filter;


static ngx_int_t
ngx_http_json_log_header_filter(ngx_http_request_t *r)
{
    ngx_uint_t                          i;
    ngx_list_part_t                     *part = &r->headers_out.headers.part;
    ngx_table_elt_t                     *header = part->elts;
    ngx_table_elt_t                     *header_copy;
    ngx_array_t                         *headers;

    ngx_str_t                           lcname;
    ngx_http_variable_value_t           *vv;
    ngx_uint_t                          varkey;
    ngx_str_t                   name = ngx_string("http_json_log_resp_headers");

    if (r->err_status == NGX_HTTP_BAD_REQUEST &&
            ngx_http_json_log_needs_err_header_filter()) {
        ngx_http_json_log_header_bad_request(r);
        return ngx_http_next_header_filter(r);
    }

    if (!ngx_http_json_log_needs_header_filter()) {
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
        if (! header_copy) {
            continue;
        }
        ngx_memzero(header_copy, sizeof(*header_copy));

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

    ngx_http_json_log_set_variable_resp_headers(r, vv, (uintptr_t) headers);

    return ngx_http_next_header_filter(r);
}


static ngx_http_request_body_filter_pt  ngx_http_next_request_body_filter;


/* save body filter */
static ngx_int_t
ngx_http_json_log_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_str_t                  name = ngx_string("http_json_log_req_body");
    ngx_str_t                  lcname;
    ngx_http_variable_value_t  *vv;
    ngx_uint_t                 varkey;

    ngx_str_t           name_hex = ngx_string("http_json_log_req_body_hexdump");
    ngx_str_t                  lcname_hex;
    ngx_http_variable_value_t  *vv_hex;
    ngx_uint_t                 varkey_hex;

    ngx_chain_t                *cl;
    size_t                     len = 0;
    size_t                     count = 0;
    u_char                     *pos = NULL;
    ngx_str_t                  payload;
    size_t                     limit = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;

    if (!ngx_http_json_log_needs_body_filter()) {
        return ngx_http_next_request_body_filter(r, in);
    }
    limit = ngx_http_json_log_get_req_body_limit(r);
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
    ngx_http_json_log_set_variable_req_body(r, vv, (uintptr_t) &payload);

    lcname_hex.len = name_hex.len;
    lcname_hex.data = ngx_pcalloc(r->pool, name_hex.len);
    varkey_hex = ngx_hash_strlow(lcname_hex.data, name_hex.data, name_hex.len);
    vv_hex = ngx_http_get_variable(r, &lcname_hex, varkey_hex);
    if (!vv_hex) {
        return ngx_http_next_header_filter(r);
    }
    ngx_http_json_log_set_variable_req_body_hexdump(r, vv_hex, (uintptr_t) &payload);

    return ngx_http_next_request_body_filter(r, in);
}


static ngx_int_t
ngx_http_json_log_filter_init(ngx_conf_t *cf)
{
    if (ngx_http_json_log_needs_body_filter()) {
        ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
        ngx_http_top_request_body_filter = ngx_http_json_log_body_filter;
    }

    if (ngx_http_json_log_needs_header_filter()
            || ngx_http_json_log_needs_err_header_filter()) {
        ngx_http_next_header_filter = ngx_http_top_header_filter;
        ngx_http_top_header_filter = ngx_http_json_log_header_filter;
    }
    return NGX_OK;
}


static void *
ngx_http_json_log_filter_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_filter_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_filter_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

#if (NGX_HAVE_LIBRDKAFKA)
    if (ngx_json_log_init_kafka(cf->pool, &conf->kafka) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "http_json_log: error initialize kafka conf");
    }
#endif

    /* create the items array for formats */
    conf->formats = ngx_array_create(cf->pool, 1,
            sizeof(ngx_json_log_format_t));

    return conf;
}


static void *
ngx_http_json_log_filter_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* create an array for the output locations */
    conf->locations = ngx_array_create(cf->pool, 1,
            sizeof(ngx_json_log_output_location_t));

    return conf;
}


static void *
ngx_http_json_log_filter_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_filter_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_filter_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->req_body_limit = HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT;

    return conf;
}


/* Register the output location for the HTTP server config
 * `json_log`
 *
 * Supported output destinations:
 *
 * file:   -> filesystem
 * kafka:  -> kafka topic
 */
static char *
ngx_http_json_log_srv_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_json_log_srv_conf_t         *lc = conf;
    ngx_http_json_log_main_conf_t        *mcf = NULL;
    ngx_json_log_format_t                *format;
    ngx_str_t                            *args = cf->args->elts;
    ngx_json_log_output_location_t       *new_location;

    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid argument for HTTP log JSON output location");
        return NGX_CONF_ERROR;
    }

    mcf = ngx_http_conf_get_module_main_conf(cf,
            ngx_http_json_log_filter_module);
    if (!mcf) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Missing main configuration");
        return NGX_CONF_ERROR;
    }

    format = ngx_json_log_check_format(mcf->formats, &args[2]);
    /* Do not accept unknown format names */
    if (format == NULL)  {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "http_json_log: Invalid format name [%V]",
                &args[2]);
        return NGX_CONF_ERROR;
    }

    new_location = ngx_json_log_output_location_conf(cf, format, lc->locations, &args[1]);
    if (new_location == NULL) {
        return NGX_CONF_ERROR;
    }


#if (NGX_HAVE_LIBRDKAFKA)
    /* If sink type is kafka, then set topic config for this location */
    if (new_location->type == NGX_JSON_LOG_SINK_KAFKA) {

        /* create topic conf */
        new_location->kafka.rktc = ngx_json_log_kafka_topic_conf_new(cf->pool);
        if (! new_location->kafka.rktc) {
            /* WARN ?*/
            return NGX_CONF_ERROR;
        }

        /* disable topic acks */
        ngx_json_log_kafka_topic_disable_ack(cf->pool,
                new_location->kafka.rktc);

        /* Set global variable */
        http_json_log_filter_has_kafka_locations = NGX_OK;

#if nginx_version >= 1011000
        ngx_http_compile_complex_value_t     ccv;
        /*FIXME: Change this to an user's configured variable */
        ngx_str_t                  msg_id_variable = ngx_string("$request_id");

        /* Set variable for message id */
        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));


        ccv.cf = cf;
        ccv.value = &msg_id_variable;
        ccv.complex_value = ngx_pcalloc(cf->pool,
                sizeof(ngx_http_complex_value_t));
        if (ccv.complex_value == NULL) {
            return NGX_CONF_ERROR;
        }
        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
        new_location->kafka.http_msg_id_var = ccv.complex_value;
#endif
    }
#endif

    ngx_http_json_log_set_needs_err_header_filter();

    return NGX_CONF_OK;
}


/* Initialized stuff per http_json_log worker.*/
static ngx_int_t
ngx_http_json_log_filter_init_worker(ngx_cycle_t *cycle)
{
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_int_t rc = NGX_OK;
    ngx_http_json_log_filter_main_conf_t  *conf =
        ngx_http_cycle_get_module_main_conf(cycle,
                ngx_http_json_log_filter_module);

    /* From this point we just are init kafka stuff */
    if (http_json_log_filter_has_kafka_locations == NGX_CONF_UNSET ) {
        return NGX_OK;
    }


    rc = ngx_json_log_configure_kafka(cycle->pool, &conf->kafka);
    if (rc != NGX_OK) {
        return NGX_OK; //FIXME: What to do?
    }
#endif

    return NGX_OK;
}
