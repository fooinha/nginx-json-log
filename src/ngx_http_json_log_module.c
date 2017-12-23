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

#include "ngx_http_json_log_module.h"
#include "ngx_json_log_str.h"
#include "ngx_json_log_output.h"
#include "ngx_json_log_text.h"
#include "ngx_http_json_log_variables.h"

#if (NGX_HAVE_LIBRDKAFKA)

#include "ngx_json_log_kafka.h"

/*Global variable to indicate the we have kafka locations*/
static ngx_int_t   http_json_log_has_kafka_locations     = NGX_CONF_UNSET;
/* configuration kafka constants */
#endif

/* Configuration callbacks */
static char *
ngx_http_json_log_loc_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static void *        ngx_http_json_log_create_main_conf(ngx_conf_t *cf);
static void *        ngx_http_json_log_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t     ngx_http_json_log_init_worker(ngx_cycle_t *cycle);
static void          ngx_http_json_log_exit_worker(ngx_cycle_t *cycle);

static ngx_int_t     ngx_http_json_log_pre_config(ngx_conf_t *cf);
static ngx_int_t     ngx_http_json_log_post_config(ngx_conf_t *cf);

/* json commands */
static ngx_command_t ngx_http_json_log_commands[] = {
    /* FORMAT */
    { ngx_string("json_log_format"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2|NGX_CONF_TAKE3,
        ngx_http_json_log_main_format_block,
        NGX_HTTP_MAIN_CONF_OFFSET,
        0,
        NULL
    },
    /* OUTPUT LOCATION */
    { ngx_string("json_log"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
        ngx_http_json_log_loc_output,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
#if (NGX_HAVE_LIBRDKAFKA)
    /* KAFKA */
    {
        ngx_string("json_log_kafka_client_id"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.client_id),
        NULL
    },
    {
        ngx_string("json_log_kafka_brokers"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_1MORE,
        ngx_conf_set_str_array_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.brokers),
        NULL
    },
    {
        ngx_string("json_log_kafka_compression"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.compression),
        NULL
    },
    {
        ngx_string("json_log_kafka_partition"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.partition),
        NULL
    },
    {
        ngx_string("json_log_kafka_log_level"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.log_level),
        NULL
    },
    {
        ngx_string("json_log_kafka_max_retries"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.max_retries),
        NULL
    },
    {
        ngx_string("json_log_kafka_buffer_max_messages"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.buffer_max_messages),
        NULL
    },
    {
        ngx_string("json_log_kafka_backoff_ms"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_json_log_main_conf_t, kafka.backoff_ms),
        NULL
    },
#endif
    ngx_null_command
};

/* http_json_log config preparation */
static ngx_http_module_t ngx_http_json_log_module_ctx = {
    ngx_http_json_log_pre_config,          /* preconfiguration */
    ngx_http_json_log_post_config,         /* postconfiguration */
    ngx_http_json_log_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_json_log_create_loc_conf,     /* create location configuration */
    NULL                                   /* merge location configuration */
};


/* http_json_log delivery */
ngx_module_t ngx_http_json_log_module = {
    NGX_MODULE_V1,
    &ngx_http_json_log_module_ctx,         /* module context */
    ngx_http_json_log_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_json_log_init_worker,         /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_json_log_exit_worker,         /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/* Initialized stuff per http_json_log worker.*/
static ngx_int_t
ngx_http_json_log_init_worker(ngx_cycle_t *cycle)
{
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_int_t rc = NGX_OK;
    ngx_http_json_log_main_conf_t  *conf =
        ngx_http_cycle_get_module_main_conf(cycle, ngx_http_json_log_module);

    /* From this point we just are init kafka stuff */
    if (http_json_log_has_kafka_locations == NGX_CONF_UNSET ) {
        return NGX_OK;
    }

    rc = ngx_json_log_configure_kafka(cycle->pool, &conf->kafka);
    if (rc != NGX_OK) {
        return NGX_OK; //FIXME: What to do?
    }
#endif

    return NGX_OK;
}


/* Things that a http_json_log maker must do before go home. */
void
ngx_http_json_log_exit_worker(ngx_cycle_t *cycle)
{
    //TODO: cleanup kafka stuff
}


/* log handler - format and print */
static ngx_int_t ngx_http_json_log_log_handler(ngx_http_request_t *r)
{
    ngx_http_json_log_loc_conf_t        *lc;
    ngx_str_t                           filter_val;
    char                                *txt;
    size_t                              i;
    ngx_json_log_output_location_t     *arr;
    ngx_json_log_output_location_t     *location;


#if (NGX_HAVE_LIBRDKAFKA)
    ngx_http_json_log_main_conf_t       *mcf;
    ngx_str_t                           msg_id;
#endif
    lc = ngx_http_get_module_loc_conf(r, ngx_http_json_log_module);
    /* Location to eat http_json_log was not found */
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

#if (NGX_HAVE_LIBRDKAFKA)
    mcf = ngx_http_get_module_main_conf(r, ngx_http_json_log_module);
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
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0, "File write error!");
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
    } // for location

    return NGX_OK;
}


static ngx_int_t
ngx_http_json_log_pre_config(ngx_conf_t *cf)
{
    ngx_http_json_log_register_variables(cf);
    return NGX_OK;
}


static ngx_int_t
ngx_http_json_log_post_config(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    /* Register custom json memory functions */
    ngx_json_log_set_alloc_funcs();

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_json_log_log_handler;

    return NGX_OK;
}


static void *
ngx_http_json_log_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_main_conf_t));
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
ngx_http_json_log_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* create an array for the output locations */
    conf->locations = ngx_array_create(cf->pool, 1,
            sizeof(ngx_json_log_output_location_t));

    return conf;
}


/* Register a output location destination for the HTTP location config
 * `json_log`
 *
 * Supported output destinations:
 *
 * file:   -> filesystem
 * kafka:  -> kafka topic
 */
static char *
ngx_http_json_log_loc_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_json_log_loc_conf_t         *lc = conf;
    ngx_http_json_log_main_conf_t        *mcf = NULL;
    ngx_json_log_format_t                *format;
    ngx_str_t                            *args = cf->args->elts;
    ngx_json_log_output_location_t       *new_location;

    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid argument for HTTP log JSON output location");
        return NGX_CONF_ERROR;
    }

    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_json_log_module);
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
        http_json_log_has_kafka_locations = NGX_OK;

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

    return NGX_CONF_OK;
}
