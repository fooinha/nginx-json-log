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
#ifndef __NGX_JSON_LOG_KAFKA_H__
#define __NGX_JSON_LOG_KAFKA_H__

#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_stream.h>

#if (NGX_HAVE_LIBRDKAFKA)

#include <librdkafka/rdkafka.h>

struct ngx_json_log_kafka_conf_s {
    rd_kafka_topic_t           *rkt;              /* kafka topic */
    rd_kafka_topic_conf_t      *rktc;             /* kafka topic configuration*/
    ngx_http_complex_value_t   *http_msg_id_var;  /* variable for message id */
#if nginx_version >= 1011002
    ngx_stream_complex_value_t *stream_msg_id_var;/* variable for message id */
#endif
};
typedef struct ngx_json_log_kafka_conf_s ngx_json_log_kafka_conf_t;

/* configuration data structures */
struct ngx_json_log_main_kafka_conf_s {
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
typedef struct ngx_json_log_main_kafka_conf_s ngx_json_log_main_kafka_conf_t;

/* topic confifuration */
rd_kafka_topic_conf_t *
ngx_json_log_kafka_topic_conf_new(ngx_pool_t* pool);

/* topic */
rd_kafka_topic_t *
ngx_json_log_kafka_topic_new(ngx_pool_t *pool, rd_kafka_t *rk,
                      rd_kafka_topic_conf_t *topic_conf, ngx_str_t *topic);

rd_kafka_conf_res_t
ngx_json_log_kafka_topic_conf_set_str(ngx_pool_t *pool,
        rd_kafka_topic_conf_t *topic_conf, const char *key, ngx_str_t *str);

void
ngx_json_log_kafka_topic_disable_ack(ngx_pool_t *pool,
    rd_kafka_topic_conf_t      *rktc);

ngx_int_t
ngx_json_log_init_kafka(ngx_pool_t *pool,
        ngx_json_log_main_kafka_conf_t *kafka);
ngx_int_t
ngx_json_log_configure_kafka(ngx_pool_t *pool,
        ngx_json_log_main_kafka_conf_t *conf);

#endif// (NGX_HAVE_LIBRDKAFKA)
#endif// __NGX_LOG_JSON_KAFKA_H__
