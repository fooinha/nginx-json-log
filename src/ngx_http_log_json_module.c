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

#include <ctype.h>
#include <assert.h>

#include "ngx_http_log_json_str.h"
#include "ngx_http_log_json_text.h"
#include "ngx_http_log_json_kafka.h"
#include "ngx_http_log_json_variables.h"

#define HTTP_LOG_JSON_VER    "0.0.4"

#define HTTP_LOG_JSON_FILE_OUT_LEN (sizeof("file:") - 1)
#define HTTP_LOG_JSON_LOG_HAS_FILE_PREFIX(str)                                \
    (ngx_strncmp(str->data,                                                   \
                 http_log_json_file_prefix,                                   \
                 HTTP_LOG_JSON_FILE_OUT_LEN) ==  0 )

#define HTTP_LOG_JSON_KAFKA_OUT_LEN (sizeof("kafka:") - 1)
#define HTTP_LOG_JSON_LOG_HAS_KAFKA_PREFIX(str) \
    (ngx_strncmp(str->data,                                                   \
                 http_log_json_kafka_prefix,                                  \
                 HTTP_LOG_JSON_KAFKA_OUT_LEN) ==  0 )


#define HTTP_LOG_JSON_REQ_BODY_LIMIT_DEFAULT 512

/* output prefixes */
static const char *http_log_json_file_prefix              = "file:";
static const char *http_log_json_kafka_prefix             = "kafka:";

/* format prefixes types and values */
static const char *http_log_json_true_value               = "true";
static const char *http_log_json_array_prefix             = "a:";
static const char *http_log_json_boolean_prefix           = "b:";
static const char *http_log_json_string_prefix            = "s:";
static const char *http_log_json_real_prefix              = "r:";
static const char *http_log_json_int_prefix               = "i:";
static const char *http_log_json_null_prefix              = "n:";

/*Global variable to indicate the we have kafka locations*/
static ngx_int_t   http_log_json_has_kafka_locations          = NGX_CONF_UNSET;
static ngx_int_t   http_log_json_needs_req_body_filter        = NGX_CONF_UNSET;
static ngx_int_t   http_log_json_needs_resp_headers_filter    = NGX_CONF_UNSET;

ngx_int_t
ngx_http_log_json_needs_body_filter() {
    return http_log_json_needs_req_body_filter != NGX_CONF_UNSET ;
}

ngx_int_t
ngx_http_log_json_needs_header_filter() {
    return http_log_json_needs_resp_headers_filter != NGX_CONF_UNSET ;
}

typedef enum {
    NGX_HTTP_LOG_JSON_SINK_FILE = 0,
    NGX_HTTP_LOG_JSON_SINK_KAFKA = 1
} ngx_http_log_json_sink_e;

/* configuration kafka constants */
static const char *conf_client_id_key          = "client.id";
static const char *conf_compression_codec_key  = "compression.codec";
static const char *conf_debug_key              = "debug";
static const char *conf_log_level_key          = "log_level";
static const char *conf_max_retries_key        = "message.send.max.retries";
static const char *conf_buffer_max_msgs_key    = "queue.buffering.max.messages";
static const char *conf_req_required_acks_key  = "request.required.acks";
static const char *conf_retry_backoff_ms_key   = "retry.backoff.ms";
static ngx_str_t   conf_all_value              = ngx_string("all");
static ngx_str_t   conf_zero_value             = ngx_string("0");


/* data structures */

struct ngx_http_log_json_format_s {
    ngx_str_t                        name;      /* the format name */
    ngx_str_t                        config;    /* value at config files */
    ngx_array_t                      *items;    /* format items */
    ngx_http_complex_value_t         *filter;    /* filter output */
};
typedef struct ngx_http_log_json_format_s     ngx_http_log_json_format_t;

struct ngx_http_log_json_loc_kafka_conf_s {
    rd_kafka_topic_t                 *rkt;       /* kafka topic */
    rd_kafka_topic_conf_t            *rktc;      /* kafka topic configuration */
};

/* configuration data structures */
struct ngx_http_log_json_main_kafka_conf_s {
    rd_kafka_t       *rk;                  /* kafka connection handler */
    rd_kafka_conf_t  *rkc;                 /* kafka configuration */
    ngx_array_t      *brokers;             /* kafka list of brokers */
    size_t           valid_brokers;        /* number of valid brokers added */
    ngx_str_t        client_id;            /* kafka client id */
    ngx_str_t        compression;          /* kafka communication compression */
    ngx_uint_t       log_level;            /* kafka client log level */
    ngx_uint_t       max_retries;          /* kafka client max retries */
    ngx_uint_t       buffer_max_messages;  /* max. num. mesg. at send buffer */
    ngx_msec_t       backoff_ms;           /* ms to wait for ... */
    ngx_int_t        partition;            /* kafka partition */
};

typedef struct ngx_http_log_json_main_kafka_conf_s
                                           ngx_http_log_json_main_kafka_conf_t;
typedef struct ngx_http_log_json_loc_kafka_conf_s
                                           ngx_http_log_json_loc_kafka_conf_t;
struct ngx_http_log_json_main_conf_s {
    ngx_http_log_json_main_kafka_conf_t  kafka;
};

struct ngx_http_log_json_output_location_s {
    ngx_str_t                                 location;
    ngx_http_log_json_sink_e                  type;
    ngx_http_log_json_format_t                format;
    ngx_open_file_t                           *file;
    ngx_http_log_json_loc_kafka_conf_t        kafka;
};

typedef struct ngx_http_log_json_output_location_s
    ngx_http_log_json_output_location_t;

struct ngx_http_log_json_loc_conf_s {
    ngx_array_t                               *locations;
    ngx_array_t                               *formats;
};

typedef struct ngx_http_log_json_loc_conf_s      ngx_http_log_json_loc_conf_t;
typedef struct ngx_http_log_json_main_conf_s     ngx_http_log_json_main_conf_t;

/* Configuration callbacks */
static char *        ngx_http_log_json_loc_format_block(ngx_conf_t *cf,
                                                    ngx_command_t *cmd,
                                                    void *conf);
static char *
ngx_http_log_json_loc_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static void *        ngx_http_log_json_create_main_conf(ngx_conf_t *cf);
static void *        ngx_http_log_json_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t     ngx_http_log_json_init_worker(ngx_cycle_t *cycle);
static void          ngx_http_log_json_exit_worker(ngx_cycle_t *cycle);

static ngx_int_t     ngx_http_log_json_pre_config(ngx_conf_t *cf);
static ngx_int_t     ngx_http_log_json_init(ngx_conf_t *cf);

/* http_log_json commands */
static ngx_command_t ngx_http_log_json_commands[] = {
    /* RECIPE */
    { ngx_string("http_log_json_format"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2|NGX_CONF_TAKE3,
        ngx_http_log_json_loc_format_block,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    { ngx_string("http_log_json_output"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
        ngx_http_log_json_loc_output,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    /* KAFKA */
    {
        ngx_string("http_log_json_kafka_client_id"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.client_id),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_brokers"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_1MORE,
        ngx_conf_set_str_array_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.brokers),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_compression"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.compression),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_partition"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.partition),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_log_level"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.log_level),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_max_retries"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.max_retries),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_buffer_max_messages"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.buffer_max_messages),
        NULL
    },
    {
        ngx_string("http_log_json_kafka_backoff_ms"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_log_json_main_conf_t, kafka.backoff_ms),
        NULL
    },
};

/* http_log_json config preparation */
static ngx_http_module_t ngx_http_log_json_module_ctx = {
    ngx_http_log_json_pre_config,          /* preconfiguration */
    ngx_http_log_json_init,                /* postconfiguration */
    ngx_http_log_json_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_log_json_create_loc_conf,     /* create location configuration */
    NULL                                   /* merge location configuration */
};

/* http_log_json delivery */
ngx_module_t ngx_http_log_json_module = {
    NGX_MODULE_V1,
    &ngx_http_log_json_module_ctx,         /* module context */
    ngx_http_log_json_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_log_json_init_worker,         /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_log_json_exit_worker,         /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/* Initialized stuff per http_log_json worker.*/
static ngx_int_t ngx_http_log_json_init_worker(ngx_cycle_t *cycle) {

    ngx_http_log_json_main_conf_t  *conf =
        ngx_http_cycle_get_module_main_conf(cycle, ngx_http_log_json_module);

    /* From this point we just are init kafka stuff */
    if (http_log_json_has_kafka_locations == NGX_CONF_UNSET ) {
        return NGX_OK;
    }

    /* kafka */
    /* - default values - */
    static ngx_str_t  http_log_json_kafka_compression_default_value =
        ngx_string("snappy");

    static ngx_str_t  http_log_json_kafka_client_id_default_value =
        ngx_string("nginx");

    static ngx_int_t  http_log_json_kafka_log_level_default_value =
        6;

    static ngx_int_t  http_log_json_kafka_max_retries_default_value =
        0;

    static ngx_int_t  http_log_json_kafka_buffer_max_messages_default_value =
        100000;

    static ngx_msec_t http_log_json_kafka_backoff_ms_default_value =
        10;

    /* create kafka configuration */
    conf->kafka.rkc = http_log_json_kafka_conf_new(cycle->pool);
    if (! conf->kafka.rkc) {
        return NGX_ERROR;
    }

    /* configure compression */
    if ((void*) conf->kafka.compression.data == NULL) {
        http_log_json_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                conf_compression_codec_key,
                &http_log_json_kafka_compression_default_value);
    } else {
        http_log_json_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                conf_compression_codec_key,
                &conf->kafka.compression);
    }
    /* configure max messages, max retries, retry backoff default values if unset*/
    if (conf->kafka.buffer_max_messages == NGX_CONF_UNSET_UINT) {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_buffer_max_msgs_key,
                http_log_json_kafka_buffer_max_messages_default_value);
    } else {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_buffer_max_msgs_key,
                conf->kafka.buffer_max_messages);
    }
    if (conf->kafka.max_retries == NGX_CONF_UNSET_UINT) {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_max_retries_key,
                http_log_json_kafka_max_retries_default_value);
    } else {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_max_retries_key,
                conf->kafka.max_retries);
    }
    if (conf->kafka.backoff_ms == NGX_CONF_UNSET_MSEC) {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_retry_backoff_ms_key,
                http_log_json_kafka_backoff_ms_default_value);
    } else {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_retry_backoff_ms_key,
                conf->kafka.backoff_ms);
    }
    /* configure default client id if not set*/
    if ((void*) conf->kafka.client_id.data == NULL) {
        http_log_json_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                conf_client_id_key,
                &http_log_json_kafka_client_id_default_value);
    } else {
        http_log_json_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                conf_client_id_key,
                &conf->kafka.client_id);
    }
    /* configure default log level if not set*/
    if (conf->kafka.log_level == NGX_CONF_UNSET_UINT) {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_log_level_key,
                http_log_json_kafka_log_level_default_value);
    } else {
        http_log_json_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                conf_log_level_key,
                conf->kafka.log_level);
    }

#if (NGX_DEBUG)
    /* configure debug */
    http_log_json_kafka_conf_set_str(cycle->pool,conf->kafka.rkc,
            conf_debug_key,
            &conf_all_value);
#endif
    /* create kafka handler */
    conf->kafka.rk = http_log_json_kafka_producer_new(
            cycle->pool,
            conf->kafka.rkc);
    if (! conf->kafka.rk) {
        return NGX_ERROR;
    }
    /* set client log level */
    if (conf->kafka.log_level == NGX_CONF_UNSET_UINT) {
        rd_kafka_set_log_level(conf->kafka.rk,
                http_log_json_kafka_log_level_default_value);
    } else {
        rd_kafka_set_log_level(conf->kafka.rk,
                conf->kafka.log_level);
    }
    /* configure brokers */
    conf->kafka.valid_brokers = http_log_json_kafka_add_brokers(cycle->pool,
            conf->kafka.rk, conf->kafka.brokers);

    if (!conf->kafka.valid_brokers) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                "http_log_json: failed to configure at least a kafka broker.");
        return NGX_OK;
    }

    return NGX_OK;
}


/* Things that a http_log_json maker must do before go home. */
void
ngx_http_log_json_exit_worker(ngx_cycle_t *cycle) {
    //TODO: cleanup kafka stuff
}

static ngx_int_t
ngx_http_log_json_write_sink_file(ngx_log_t *log,
        ngx_fd_t fd, const char *txt, size_t len) {

    size_t written = ngx_write_fd(fd, (u_char *)txt, len);
    if (len != written) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "Mismatch size");
        return NGX_ERROR;
    }
    return NGX_OK;
}


/* log handler - format and print */
static ngx_int_t ngx_http_log_json_log_handler(ngx_http_request_t *r) {

    ngx_http_log_json_loc_conf_t        *lc;
    ngx_http_log_json_main_conf_t       *mcf;
    ngx_str_t                           filter_val;
    char                                *txt;
    size_t                              i, len;
    int                                 err;
    ngx_http_log_json_output_location_t *arr;
    ngx_http_log_json_output_location_t *location;

    lc = ngx_http_get_module_loc_conf(r, ngx_http_log_json_module);

    /*FIXME: Try to discard local upstream requests */
    if (!r->upstream && r->lingering_time) {
        return NGX_OK;
    }

    /* Location to eat http_log_json was not found */
    if (!lc) {
        return NGX_OK;
    }

    /* Bypass if number of location is empty */
    if (!lc->locations->nelts) {
        return NGX_OK;
    }

    /* Discard connect methods ... file is not open!?. Proxy mode  */
    if (r->method == NGX_HTTP_UNKNOWN &&
        ngx_strncasecmp((u_char *)"CONNECT", r->request_line.data, 7) == 0) {
        return NGX_OK;
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_log_json_module);

    arr = lc->locations->elts;
    for (i = 0; i < lc->locations->nelts; ++i) {

        location = &arr[i];

        if (!location) {
            break;
        }

        /* Check filter result */
        if (location->format.filter != NULL) {
            if (ngx_http_complex_value(r,
                        location->format.filter, &filter_val) != NGX_OK) {
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
        txt = ngx_http_log_json_items_dump_text(r, location->format.items);
        if (!txt) {
            /* WARN ? */
            continue;
        }

        /* Write to file */
        if (location->type == NGX_HTTP_LOG_JSON_SINK_FILE) {

            if (!location->file) {
                continue;
            }

            len = strlen(txt);
            if (!len) {
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0, "Empty line");
                continue;
            }

            if (ngx_http_log_json_write_sink_file(r->pool->log,
                        location->file->fd, txt, len) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0, "Write Error!");
            }
            continue;
        }

        /* Write to kafka */
        if (location->type == NGX_HTTP_LOG_JSON_SINK_KAFKA) {

            /* don't do anything if no kafka brokers to send */
            if (! mcf->kafka.valid_brokers) {
                continue;
            }

            if (location->kafka.rkt == NGX_CONF_UNSET_PTR ||
                    !location->kafka.rkt)  {
                /* configure and create topic */
                location->kafka.rkt =
                    http_log_json_kafka_topic_new(r->pool,
                            mcf->kafka.rk, location->kafka.rktc,
                            &location->location);
            }

            /* if failed to create topic */
            if (!location->kafka.rkt) {
                location->kafka.rkt = NGX_CONF_UNSET_PTR;
                /* WARN ?*/
                continue;
            }

            /* FIXME : Reconnect support */
            /* Send/Produce message. */
            if ((err =  rd_kafka_produce(
                            location->kafka.rkt,
                            mcf->kafka.partition,
                            RD_KAFKA_MSG_F_COPY,
                            /* Payload and length */
                            txt, strlen(txt),
                            /* Optional key and its length */
                            NULL, 0,
                            /* Message opaque, provided in
                             * delivery report callback as
                             * msg_opaque. */
                            NULL)) == -1) {

                const char *errstr = rd_kafka_err2str(rd_kafka_errno2err(err));

                ngx_log_error(NGX_LOG_ERR, r->pool->log, 0,
                        "%% Failed to produce to topic %s "
                        "partition %i: %s\n",
                        rd_kafka_topic_name(location->kafka.rkt),
                        mcf->kafka.partition,
                        errstr);
            } else {

#if (NGX_DEBUG)
                if (mcf) {
                    ngx_log_error(NGX_LOG_DEBUG, r->pool->log, 0,
                            "http_log_json: kafka msg:[%s] ERR:[%d] QUEUE:[%d]",
                            txt, err, rd_kafka_outq_len(mcf->kafka.rk));
                }
#endif
            }

        } // if KAFKA type
    } // for location

    return NGX_OK;
}

static ngx_int_t
ngx_http_log_json_pre_config(ngx_conf_t *cf) {

    ngx_http_log_json_register_variables(cf);
    return NGX_OK;
}

static ngx_int_t
ngx_http_log_json_init(ngx_conf_t *cf) {

    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    /* Register custom json memory functions */
    ngx_http_log_json_set_alloc_funcs();

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_log_json_log_handler;

    return NGX_OK;
}

/* Compares two items by name */
static
ngx_int_t ngx_http_log_json_items_cmp(const void *left, const void *right) {

    const ngx_http_log_json_item_t * l = left;
    const ngx_http_log_json_item_t * r = right;

    return ngx_strncasecmp(l->name->data, r->name->data,
            ngx_min(l->name->len, r->name->len));
}

/* Reads and parses, format from configuration */
static ngx_int_t
ngx_http_log_json_read_format(ngx_conf_t *cf,
        ngx_http_log_json_format_t *format) {

/* This requires PCRE */
#if (NGX_PCRE)
    u_char errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t rc;
    ngx_str_t *config;
    ngx_str_t spec;
    int ovector[1024] = {0};
    char value[1025] = {0};
    ngx_str_t pattern = ngx_string("\\s*([^\\s]+)\\s+([^\\s;]+);");
    int array_prefix_len = 0;
    ngx_http_log_json_item_t *item;
    ngx_http_complex_value_t           *cv = NULL;
    ngx_http_compile_complex_value_t   ccv;
    int i, offset, matched, ret;
    ngx_str_t *key_str;
    ngx_str_t *value_str;

    /* Prepares regex */
    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
    rc.pattern = pattern;
    rc.pool = cf->pool;
    rc.options = NGX_REGEX_CASELESS;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    /* Compiles regex */
    if (ngx_regex_compile(&rc) != NGX_OK) {
        /* Bad regex - programming error */
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V", &rc.err);
        return NGX_ERROR;
    }

    /* Tries to match format to regex and verify format */
    config = &format->config;
    spec.data = config->data;
    spec.len = config->len;

    /* While we find group lines for the spec */
    matched = ngx_regex_exec(rc.regex, &spec, ovector, 1024);
    while (matched > 0) {
        offset = 0;

        if (matched < 1) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                    "Failed to configure http_log_json_format.");
            return NGX_ERROR;
        }

        key_str   = ngx_pcalloc(cf->pool, sizeof(ngx_str_t));
        value_str = ngx_pcalloc(cf->pool, sizeof(ngx_str_t));

        for (i=0; i < matched; i++) {
            ret = pcre_copy_substring((const char *)spec.data,
                    ovector, matched, i, value, 1024);
            /* i = 0 => all match with isize */
            if (i == 0) {
                offset = ret;
            }
            /* i = 1 => key - item name */
            if (i == 1) {
                key_str->data = ngx_pcalloc(cf->pool, ret);
                key_str->len = ret;
                ngx_cpystrn(key_str->data, (u_char *)value, ret+1);
            }
            /* i = 2 => value */
            if (i == 2) {
                value_str->data = ngx_pcalloc(cf->pool, ret);
                value_str->len = ret;
                ngx_cpystrn(value_str->data, (u_char *)value, ret+1);
            }
        }

        cv = ngx_pcalloc(cf->pool, sizeof(ngx_http_complex_value_t));
        if (cv == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                    0, "Failed to configure http_log_json_format.");
            return NGX_ERROR;
        }
        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
        ccv.cf = cf;
        ccv.value = value_str;
        ccv.complex_value = cv;
        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                    0, "Failed to configure http_log_json_format.");
            return NGX_ERROR;
        }

        item = ngx_array_push(format->items);
        if (item == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                    0, "Failed to configure http_log_json_format .");
            return NGX_ERROR;
        }

        item->name = key_str;

        /* Saves var name */
        if (*value_str->data == '$') {
            item->var_name.data= ngx_pcalloc(cf->pool, value_str->len - 1);
            ngx_memcpy(item->var_name.data,
                    value_str->data + 1, value_str->len - 1);
            item->var_name.len = value_str->len - 1;

            /* enables body filter if needed */
            if (http_log_json_needs_req_body_filter == NGX_CONF_UNSET
                    && ngx_http_log_json_is_local_variable(
                        &item->var_name)
                    && ngx_http_log_json_local_variable_needs_body_filter(
                        &item->var_name)) {
                http_log_json_needs_req_body_filter = 1;
            }

            /* enables header filter if needed */
            if (http_log_json_needs_resp_headers_filter== NGX_CONF_UNSET
                    && ngx_http_log_json_is_local_variable(
                        &item->var_name)
                    && ngx_http_log_json_local_variable_needs_header_filter(
                        &item->var_name)) {
                http_log_json_needs_resp_headers_filter = 1;
            }
        }

        item->is_array = 0;

        /* Check and save type from name prefix */
        /* Default is JSON_STRING type */
        item->type = ngx_http_log_json_type_string();
        if (ngx_strncmp(item->name->data,
                    http_log_json_array_prefix, 2) == 0) {
            item->is_array = 1;
            array_prefix_len = 2;
        }

        if (ngx_strncmp(item->name->data + array_prefix_len,
                    http_log_json_int_prefix, 2) == 0) {
            item->type = ngx_http_log_json_type_integer();
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    http_log_json_real_prefix, 2) == 0) {
            item->type = ngx_http_log_json_type_real();
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    http_log_json_string_prefix, 2) == 0) {
            item->type = ngx_http_log_json_type_string();
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    http_log_json_null_prefix, 2) == 0) {
            item->type = ngx_http_log_json_type_null();
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    http_log_json_boolean_prefix, 2) == 0) {
            if (ngx_strncmp(value_str->data + array_prefix_len,
                        http_log_json_true_value, 4) == 0) {
                item->type = ngx_http_log_json_type_true();
            } else {
                item->type = ngx_http_log_json_type_false();
            }
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else {
            item->type = ngx_http_log_json_type_string();
            if (item->is_array) {
                item->name->data += array_prefix_len;
                item->name->len  -= array_prefix_len;
            }
        }
        item->ccv = (ngx_http_compile_complex_value_t *) cv;

        /* adjust pointers and size for reading the next item*/
        spec.data += offset;
        spec.len -= offset;

        matched = ngx_regex_exec(rc.regex, &spec, ovector, 1024);
    }
#endif

    /* sort items .... this is very import for serialization output alg*/
    ngx_sort(format->items->elts, (size_t) format->items->nelts,
            sizeof(ngx_http_log_json_item_t), ngx_http_log_json_items_cmp);

    return NGX_OK;
}

static void *
ngx_http_log_json_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_log_json_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_json_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* create an array for the output locations */
    conf->locations = ngx_array_create(cf->pool, 1,
            sizeof(ngx_http_log_json_output_location_t));


    /* create the items array for formats */
    conf->formats = ngx_array_create(cf->pool, 1,
            sizeof(ngx_http_log_json_format_t));

    return conf;
}

static void *
ngx_http_log_json_create_main_conf(ngx_conf_t *cf) {

    ngx_http_log_json_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_json_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* kafka */
    conf->kafka.rk                  = NULL;
    conf->kafka.rkc                 = NULL;

    /* default values */
    conf->kafka.brokers             = ngx_array_create(cf->pool,
            1 , sizeof(ngx_str_t));
    conf->kafka.client_id.data      = NULL;
    conf->kafka.compression.data    = NULL;
    conf->kafka.log_level           = NGX_CONF_UNSET_UINT;
    conf->kafka.max_retries         = NGX_CONF_UNSET_UINT;
    conf->kafka.buffer_max_messages = NGX_CONF_UNSET_UINT;
    conf->kafka.backoff_ms          = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_log_json_loc_format_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_str_t                            *args;
    ngx_http_log_json_format_t           *new_format;
    ngx_http_log_json_loc_conf_t         *lc = conf;
    ngx_http_compile_complex_value_t     ccv;
    ngx_str_t                            s;

    args = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                cf, 0, "invalid empty format");
        return NGX_CONF_ERROR;
    }

    /*TODO*: to verify if format name is duplicated */
    new_format = ngx_array_push(lc->formats);

    if (!new_format) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Failed to create format HTTP log JSON.");
        return NGX_CONF_ERROR;
    }

    /* Saves the format name and the format spec value */
    new_format->name   = args[1];
    new_format->config = args[2];

    /* Create an array with the number of items found */
    new_format->items = ngx_array_create(cf->pool,
            ngx_http_log_json_str_split_count(&new_format->config, ';'),
            sizeof(ngx_http_log_json_item_t)
            );

    if (ngx_http_log_json_read_format(cf, new_format) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf,
                0, "invalid format read");
        return NGX_CONF_ERROR;
    }

    /*check and save the if filter condition */
    if (cf->args->nelts >= 4 && args[3].data != NULL) {

        if (ngx_strncmp(args[3].data, "if=", 3) == 0) {
            s.len = args[3].len - 3;
            s.data = args[3].data + 3;

            ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

            ccv.cf = cf;
            ccv.value = &s;
            ccv.complex_value = ngx_pcalloc(cf->pool,
                    sizeof(ngx_http_complex_value_t));
            if (ccv.complex_value == NULL) {
                return NGX_CONF_ERROR;
            }
            if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
            new_format->filter = ccv.complex_value;
        }
    }

    return NGX_CONF_OK;
}

/* Register a output location destination for the HTTP location config
 * `http_log_json_output`
 *
 * Supported output destinations:
 *
 * file:   -> filesystem
 * kafka:  -> kafka topic
 */
static char *
ngx_http_log_json_loc_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_log_json_loc_conf_t         *lc = conf;
    ngx_str_t                            *args = cf->args->elts;
    ngx_str_t                            *value = NULL;
    ngx_http_log_json_output_location_t  *new_location = NULL;
    ngx_str_t                            *format_name;
    ngx_http_log_json_format_t           *format;
    size_t                               prefix_len;
    size_t                               i;
    ngx_uint_t                           found = 0;
    ngx_http_log_json_main_conf_t        *mcf;

    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid argument for HTTP log JSON output location");
        return NGX_CONF_ERROR;
    }

    /* Check if format exists by name */
    format_name = &args[2];
    format = lc->formats->elts;
    for (i = 0; i < lc->formats->nelts; i++) {
        if (ngx_strncmp(format_name->data, format[i].name.data,
                    format[i].name.len) == 0) {
            found = 1;
            break;
        }
    }

    /* Do not accept unknown format names */
    if (!found)  {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "http_log_json: Invalid format name [%V]",
                format_name);
        return NGX_CONF_ERROR;
    }

    value = &args[1];

    if (HTTP_LOG_JSON_LOG_HAS_FILE_PREFIX(value)) {
        new_location = ngx_array_push(lc->locations);
        new_location->type = NGX_HTTP_LOG_JSON_SINK_FILE;
        prefix_len = ngx_strlen(http_log_json_file_prefix);
    }
    else if (HTTP_LOG_JSON_LOG_HAS_KAFKA_PREFIX(value)) {
        new_location = ngx_array_push(lc->locations);
        new_location->type = NGX_HTTP_LOG_JSON_SINK_KAFKA;
        prefix_len = ngx_strlen(http_log_json_kafka_prefix);

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid prefix [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }

    if (!new_location) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Failed to add [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }

    /* Saves location without prefix. */
    new_location->location       = args[1];
    new_location->location.len   -= prefix_len;
    new_location->location.data  += prefix_len;
    new_location->format         =  format[i];

    /* If sink type is file, then try to open it and save */
    if (new_location->type == NGX_HTTP_LOG_JSON_SINK_FILE) {
        new_location->file = ngx_conf_open_file(cf->cycle,
                &new_location->location);
    }

    /* If sink type is kafka, then set topic config for this location */
    if (new_location->type == NGX_HTTP_LOG_JSON_SINK_KAFKA) {

        mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_log_json_module);

        if (! mcf) {
            /*TODO: WARN ?*/
            return NGX_CONF_ERROR;
        }

        /* create topic conf */
        new_location->kafka.rktc = http_log_json_kafka_topic_conf_new(cf->pool);
        if (! new_location->kafka.rktc) {
            /* WARN ?*/
            return NGX_CONF_ERROR;
        }

        /* configure topic acks */
        http_log_json_kafka_topic_conf_set_str(cf->pool,
                new_location->kafka.rktc,
                conf_req_required_acks_key, &conf_zero_value);

        /* Set global variable */
        http_log_json_has_kafka_locations = NGX_OK;
    }

    return NGX_CONF_OK;
}
