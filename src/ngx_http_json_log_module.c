/*
 * Copyright (C) 2017-2021 Paulo Pacheco
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

#if (NGX_HAVE_STREAM)
#include <ngx_stream.h>
#endif

#include <jansson.h>

#define NGX_JSON_LOG_FILE_OUT_LEN (sizeof("file:") - 1)
#define NGX_JSON_LOG_HAS_FILE_PREFIX(str)                   \
(ngx_strncmp(str->data, "file:", NGX_JSON_LOG_FILE_OUT_LEN) ==  0 )

#define NGX_JSON_LOG_SYSLOG_OUT_LEN (sizeof("syslog:") - 1)
#define NGX_JSON_LOG_HAS_SYSLOG_PREFIX(str)                 \
(ngx_strncmp(str->data, "syslog:", NGX_JSON_LOG_SYSLOG_OUT_LEN) ==  0 )

#if (NGX_HAVE_LIBRDKAFKA)
#include <librdkafka/rdkafka.h>

#define NGX_JSON_LOG_KAFKA_OUT_LEN (sizeof("kafka:") - 1)
#define NGX_JSON_LOG_HAS_KAFKA_PREFIX(str)                  \
(ngx_strncmp(str->data, "kafka:", NGX_JSON_LOG_KAFKA_OUT_LEN) ==  0 )

#define NGX_JSON_LOG_KAFKA_ERROR_MSG_LEN (2048)
#endif

typedef enum {
    NGX_JSON_LOG_HTTP = 0,
#if (NGX_HAVE_STREAM)
    NGX_JSON_LOG_STREAM = 1,
#endif
    } ngx_json_log_module_type_e;

typedef enum {
    ENCODING_NONE    = 0,
    ENCODING_BASE64  = 1,
    ENCODING_HEX     = 2,
    ENCODING_HEXDUMP = 3
} ngx_json_log_encoding_e;

typedef enum {
    NGX_JSON_LOG_NODE_VALUE_STR  = 0,
    NGX_JSON_LOG_NODE_TABLE_LIST = 1
} ngx_json_node_value_type_e;

typedef enum {
    NGX_JSON_LOG_SINK_FILE    = 0,
    NGX_JSON_LOG_SINK_SYSLOG  = 1,
#if (NGX_HAVE_LIBRDKAFKA)
    NGX_JSON_LOG_SINK_KAFKA   = 2
#endif
} ngx_json_log_sink_e;

struct ngx_json_log_item_s {
    json_type                            type;
    ngx_str_t                           *name;
    ngx_str_t                            var_name;
    ngx_int_t                            is_array;
    ngx_json_log_encoding_e              encoding;
    ngx_http_compile_complex_value_t    *http_ccv;
#if (NGX_HAVE_STREAM)
    ngx_stream_compile_complex_value_t  *stream_ccv;
#endif
};

struct ngx_json_log_output_cxt_s {
    /* array to keep levels node values */
    /* no need for hash or list struct */
    /* as it should be very small */
    json_t        *root;
    ngx_array_t   *items;
};

struct ngx_json_node_value_s {
    ngx_json_node_value_type_e type;
    union {
        ngx_str_t        *str;
        ngx_list_part_t  *part;
    } v;
};

struct ngx_json_log_value_s {
    ngx_str_t                        label;
    json_t                           *node;
};

struct ngx_json_log_body_queue_s {
    ngx_str_t chunk;
    ngx_queue_t queue;
};

struct ngx_http_json_log_loc_conf_s {
    ngx_array_t *locations;
    ngx_flag_t request_body;
    ngx_flag_t response_headers;
};

struct ngx_json_log_variable_value_s {
    union {
        ngx_queue_t queue;
    } u;
    ngx_uint_t len;
};

struct ngx_json_log_format_s {
    ngx_str_t                   name;           /* the format name */
    ngx_str_t                   config;         /* value at config files */
    ngx_array_t                 *items;         /* format items */
    ngx_http_complex_value_t    *http_filter;   /* filter output */
#if (NGX_HAVE_STREAM)
    ngx_stream_complex_value_t  *stream_filter; /* filter output */
#endif
};


#if (NGX_HAVE_LIBRDKAFKA)
struct ngx_json_log_kafka_conf_s {
    rd_kafka_topic_t           *rkt;              /* kafka topic */
    rd_kafka_topic_conf_t      *rktc;             /* kafka topic configuration*/
    ngx_http_complex_value_t   *http_msg_id_var;  /* variable for message id */
#if (NGX_HAVE_STREAM)
    ngx_stream_complex_value_t *stream_msg_id_var;/* variable for message id */
#endif
};

/* configuration data structures */
struct ngx_json_log_main_kafka_conf_s {
    rd_kafka_t      *rk;                  /* kafka connection handler */
    rd_kafka_conf_t *rkc;                 /* kafka configuration */
    ngx_array_t     *brokers;             /* kafka list of brokers */
    size_t           valid_brokers;        /* number of valid brokers added */
    ngx_str_t        client_id;            /* kafka client id */
    ngx_str_t        compression;          /* kafka communication compression */
    ngx_uint_t       log_level;            /* kafka client log level */
    ngx_uint_t       max_retries;          /* kafka client max retries */
    ngx_uint_t       buffer_max_messages;  /* max. num. mesg. at send buffer */
    ngx_msec_t       backoff_ms;           /* ms to wait for ... */
    ngx_int_t        partition;            /* kafka partition */
};

typedef struct ngx_json_log_kafka_conf_s ngx_json_log_kafka_conf_t;
typedef struct ngx_json_log_main_kafka_conf_s ngx_json_log_main_kafka_conf_t;
#endif

/* Configuration structures */
struct ngx_http_json_log_main_conf_s {
    ngx_array_t *formats;
    ngx_http_variable_t *resp_headers;
    ngx_http_variable_t *req_body;

#if (NGX_HAVE_LIBRDKAFKA)
    ngx_json_log_main_kafka_conf_t   kafka;
#endif
};

struct ngx_stream_json_log_main_conf_s {
    ngx_array_t                               *formats;
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_json_log_main_kafka_conf_t  kafka;
#endif
};
struct ngx_stream_json_log_srv_conf_s {
    ngx_array_t                               *locations;
};

typedef struct ngx_json_log_format_s          ngx_json_log_format_t;

struct ngx_json_log_output_location_s {
    ngx_str_t                                 location;
    ngx_json_log_sink_e                       type;
    ngx_json_log_format_t                     format;
    ngx_open_file_t                          *file;
    ngx_syslog_peer_t                        *syslog;
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_json_log_kafka_conf_t                 kafka;
#endif
};

typedef struct ngx_json_log_item_s            ngx_json_log_item_t;
typedef struct ngx_json_log_value_s           ngx_json_log_value_t;
typedef struct ngx_json_node_value_s          ngx_json_node_value_t;
typedef struct ngx_json_log_output_cxt_s      ngx_json_log_output_cxt_t;
typedef struct ngx_json_log_body_queue_s      ngx_json_log_body_queue_t;
typedef struct ngx_http_json_log_loc_conf_s   ngx_http_json_log_loc_conf_t;
typedef struct ngx_http_json_log_main_conf_s  ngx_http_json_log_main_conf_t;
typedef struct ngx_json_log_variable_value_s  ngx_json_log_variable_value_t;
typedef struct ngx_json_log_output_location_s ngx_json_log_output_location_t;

#if (NGX_HAVE_STREAM)
typedef struct ngx_stream_json_log_srv_conf_s   ngx_stream_json_log_srv_conf_t;
typedef struct ngx_stream_json_log_main_conf_s  ngx_stream_json_log_main_conf_t;
#endif

static ngx_int_t ngx_http_json_log_body_filter(ngx_http_request_t *r,
    ngx_chain_t *in);
static ngx_int_t ngx_http_json_log_header_filter(ngx_http_request_t *r,
    ngx_chain_t *in);
static void ngx_http_json_log_set_resp_headers( ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_json_log_get_resp_headers(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static void ngx_http_json_log_set_req_body(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_json_log_get_req_body(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static char * ngx_http_json_log_loc_output(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_json_log_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_json_log_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_http_json_log_init_worker(ngx_cycle_t *cycle);
static void ngx_http_json_log_exit_worker(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_json_log_pre_config(ngx_conf_t *cf);
static ngx_int_t ngx_http_json_log_post_config(ngx_conf_t *cf);
static char * ngx_http_json_log_main_format_block(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_json_log_items_cmp(const void *left, const void *right);
static ngx_int_t ngx_json_log_write_sink_file(ngx_log_t *log, ngx_fd_t fd,
    const char *txt);
static ngx_int_t ngx_json_log_write_sink_syslog(ngx_log_t *log, ngx_pool_t* pool,
    ngx_syslog_peer_t *syslog, const char *txt);
static ngx_int_t ngx_http_json_log_log_handler(ngx_http_request_t *r);
static void set_current_mem_pool(ngx_pool_t *pool);
static ngx_pool_t * get_current_mem_pool();
static void * ngx_json_log_malloc(size_t size);
static void ngx_json_log_set_alloc_funcs();
static ngx_json_log_value_t * ngx_json_log_find_saved_parent(ngx_pool_t *pool,
    ngx_array_t *arr_items, ngx_str_t *name, size_t len);
static const char * ngx_json_log_label_key_dup(ngx_pool_t *pool,
    ngx_str_t *path, size_t max);
static u_char * ngx_json_log_has_label_pos(u_char *path);
static json_t * ngx_json_log_find_parent(ngx_pool_t *pool,
    ngx_array_t *arr_items, json_t *parent, ngx_str_t *path);
static void ngx_json_log_add_json_node(json_t *base, int is_array,
    json_type type, ngx_json_log_encoding_e encoding, const char *key,
    ngx_json_node_value_t *var_value);
static ngx_int_t ngx_json_log_output_add_item(ngx_json_log_module_type_e type,
    void *rs, ngx_json_log_output_cxt_t *output_ctx, ngx_json_log_item_t *item);
static char *ngx_json_log_items_dump_text(ngx_json_log_module_type_e type,
    void *rs, ngx_array_t *items);
static ngx_int_t ngx_json_log_read_format(ngx_conf_t *cf,
    ngx_json_log_module_type_e type, ngx_json_log_format_t *format);
static const char * ngx_json_log_buf_dup_len(ngx_pool_t *pool, u_char *src,
    size_t len);
static u_char * ngx_json_log_str_dup(ngx_pool_t *pool, ngx_str_t *src);
//static ngx_str_t *ngx_json_log_str_dup_from_buf_len(ngx_pool_t *pool,
//    ngx_str_t *src, size_t len);
//static u_char * ngx_json_log_str_dup_len(ngx_pool_t *pool, ngx_str_t *src,
//    size_t len);
//static ngx_int_t ngx_json_log_str_clone(ngx_pool_t *pool, ngx_str_t *src,
//    ngx_str_t *dst);
static ngx_uint_t ngx_json_log_str_split_count(ngx_str_t *value,
    u_char separator);
static size_t ngx_json_log_hexdump_length(size_t len, size_t blocksz);
static void ngx_json_log_hexdump(ngx_str_t *src, ngx_str_t *dst);

#if (NGX_HAVE_STREAM)
static char * ngx_stream_json_log_main_format_block(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char * ngx_stream_json_log_srv_output(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_stream_json_log_post_conf(ngx_conf_t *cf);
static void * ngx_stream_json_log_create_main_conf(ngx_conf_t *cf);
static void * ngx_stream_json_log_create_srv_conf(ngx_conf_t *cf);
static ngx_int_t ngx_stream_json_log_init_worker(ngx_cycle_t *cycle);
static char * ngx_stream_json_log_srv_output(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_stream_json_log_log_handler(ngx_stream_session_t *s);
#endif

#if (NGX_HAVE_LIBRDKAFKA)
static rd_kafka_topic_conf_t * ngx_json_log_kafka_topic_conf_new(
    ngx_pool_t* pool);
static rd_kafka_conf_t * ngx_json_log_kafka_conf_new(ngx_pool_t *pool);
static rd_kafka_topic_t * ngx_json_log_kafka_topic_new(ngx_pool_t *pool,
    rd_kafka_t *rk, rd_kafka_topic_conf_t *topic_conf, ngx_str_t *topic);
static ngx_int_t ngx_json_log_kafka_topic_conf_set_str(ngx_pool_t *pool,
    rd_kafka_topic_conf_t *topic_conf, const char *key, ngx_str_t *str);
static void ngx_json_log_kafka_topic_disable_ack(ngx_pool_t *pool,
    rd_kafka_topic_conf_t *rktc);
static ngx_int_t ngx_json_log_init_kafka(ngx_pool_t *pool,
    ngx_json_log_main_kafka_conf_t *kafka);
static ngx_int_t ngx_json_log_configure_kafka(ngx_pool_t *pool,
    ngx_json_log_main_kafka_conf_t *conf);
static rd_kafka_t * ngx_json_log_kafka_producer_new(ngx_pool_t *pool,
    rd_kafka_conf_t *conf);
static void ngx_json_log_kafka_produce(ngx_pool_t *pool, rd_kafka_t *rk,
    rd_kafka_topic_t * rkt, ngx_int_t partition,char * txt,
    ngx_str_t *msg_id);
static ngx_int_t ngx_json_log_kafka_conf_set_int(ngx_pool_t *pool,
    rd_kafka_conf_t *conf, const char * key, intmax_t value);
static ngx_int_t ngx_json_log_kafka_conf_set_str(ngx_pool_t *pool,
    rd_kafka_conf_t *conf, const char *key, ngx_str_t *str);
static size_t ngx_json_log_kafka_add_brokers(ngx_pool_t *pool, rd_kafka_t *rk,
    ngx_array_t *brokers);
#endif


#if (NGX_HAVE_LIBRDKAFKA)
static ngx_int_t   http_json_log_has_kafka_locations     = NGX_CONF_UNSET;

#if (NGX_HAVE_STREAM)
static ngx_int_t   stream_json_log_has_kafka_locations   = NGX_CONF_UNSET;
#endif

#endif

/* format prefixes types and values */
static const char *ngx_json_log_true_value               = "true";
static const char *ngx_json_log_array_prefix             = "a:";
static const char *ngx_json_log_boolean_prefix           = "b:";
static const char *ngx_json_log_string_prefix            = "s:";
static const char *ngx_json_log_real_prefix              = "r:";
static const char *ngx_json_log_int_prefix               = "i:";
static const char *ngx_json_log_null_prefix              = "n:";

static const char *ngx_json_log_encoding_base64          = "|base64";
static const char *ngx_json_log_encoding_hex             = "|hex";
static const char *ngx_json_log_encoding_hexdump         = "|hexdump";

static ngx_str_t msg_id_variable             = ngx_string("$request_id");
static const ngx_str_t req_body_var_name     = ngx_string("http_json_log_req_body");
static const ngx_str_t resp_headers_var_name = ngx_string("http_json_log_resp_headers");

/* memory pool for json objects */
static ngx_pool_t * current_pool;


static ngx_http_request_body_filter_pt  ngx_http_next_request_body_filter;
static ngx_http_output_body_filter_pt   ngx_http_next_body_filter;


/* json commands */
static ngx_command_t ngx_http_json_log_commands[] = {
    {
        ngx_string("json_log_format"),
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE2 | NGX_CONF_TAKE3,
        ngx_http_json_log_main_format_block,
        NGX_HTTP_MAIN_CONF_OFFSET,
        0,
        NULL
    }, {
        ngx_string("json_log"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
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

/* json log module configuration */
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

/* HTTP json log module */
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

#if (NGX_HAVE_STREAM)
/* json commands */
static ngx_command_t ngx_stream_json_log_commands[] = {
    /* FORMAT */
    { ngx_string("json_log_format"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE2|NGX_CONF_TAKE3,
        ngx_stream_json_log_main_format_block,
        NGX_STREAM_MAIN_CONF_OFFSET,
        0,
        NULL
    },
    /* OUTPUT LOCATION */
    { ngx_string("json_log"),
        NGX_STREAM_SRV_CONF|NGX_CONF_TAKE2,
        ngx_stream_json_log_srv_output,
        NGX_STREAM_SRV_CONF_OFFSET,
        0,
        NULL
    },
#if (NGX_HAVE_LIBRDKAFKA)
    /* KAFKA */
    {
        ngx_string("json_log_kafka_client_id"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.client_id),
        NULL
    },
    {
        ngx_string("json_log_kafka_brokers"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_1MORE,
        ngx_conf_set_str_array_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.brokers),
        NULL
    },
    {
        ngx_string("json_log_kafka_compression"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.compression),
        NULL
    },
    {
        ngx_string("json_log_kafka_partition"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.partition),
        NULL
    },
    {
        ngx_string("json_log_kafka_log_level"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.log_level),
        NULL
    },
    {
        ngx_string("json_log_kafka_max_retries"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.max_retries),
        NULL
    },
    {
        ngx_string("json_log_kafka_buffer_max_messages"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.buffer_max_messages),
        NULL
    },
    {
        ngx_string("json_log_kafka_backoff_ms"),
        NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_STREAM_MAIN_CONF_OFFSET,
        offsetof(ngx_stream_json_log_main_conf_t, kafka.backoff_ms),
        NULL
    },
#endif
    ngx_null_command
};

static ngx_stream_module_t  ngx_stream_json_log_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_stream_json_log_post_conf,         /* postconfiguration */
    ngx_stream_json_log_create_main_conf,  /* create main configuration */
    NULL,                                  /* init main configuration */
    ngx_stream_json_log_create_srv_conf,   /* create server configuration */
    NULL                                   /* merge server configuration */
};

/* STREAM json log module */
ngx_module_t  ngx_stream_json_log_module = {
    NGX_MODULE_V1,
    &ngx_stream_json_log_module_ctx,       /* module context */
    ngx_stream_json_log_commands,          /* module directives */
    NGX_STREAM_MODULE,                     /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_stream_json_log_init_worker,       /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};
#endif

/* Initialized stuff per http_json_log worker.*/
static ngx_int_t
ngx_http_json_log_init_worker(ngx_cycle_t *cycle)
{
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_int_t                       rc;
    ngx_http_json_log_main_conf_t  *conf =
        ngx_http_cycle_get_module_main_conf(cycle, ngx_http_json_log_module);

    /* From this point we just are init kafka stuff */
    if (http_json_log_has_kafka_locations == NGX_CONF_UNSET) {
        return NGX_OK;
    }

    rc = ngx_json_log_configure_kafka(cycle->pool, &conf->kafka);
    if (rc != NGX_OK) {
        return NGX_OK; //FIXME: What to do?
    }
#endif
    return NGX_OK;
}

#if (NGX_HAVE_STREAM)
/* initialized stuff per http_json_log worker.*/
static ngx_int_t
ngx_stream_json_log_init_worker(ngx_cycle_t *cycle)
{
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_stream_json_log_main_conf_t  *conf =
    ngx_stream_cycle_get_module_main_conf(cycle, ngx_stream_json_log_module);

    /* from this point we just are init kafka stuff */
    if (stream_json_log_has_kafka_locations == NGX_CONF_UNSET ) {
        return NGX_OK;
    }

    ngx_json_log_configure_kafka(cycle->pool, &conf->kafka);
#endif
    return NGX_OK;
}
#endif

/* Things that a http_json_log maker must do before go home. */
static void
ngx_http_json_log_exit_worker(ngx_cycle_t *cycle) {
    //TODO: cleanup kafka stuff
}

static ngx_int_t
ngx_json_log_write_sink_file(ngx_log_t *log, ngx_fd_t fd, const char *txt)
{
    ssize_t                   written = 0;
    ssize_t                   len = 0;
    if (!txt) {
        return NGX_ERROR;
    }

    len = strlen(txt);
    written = ngx_write_fd(fd, (u_char *)txt, (size_t) len);
    if (len && len != written) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
            "mismatch size: fd=%d len=%d written=%d", fd, len, written);
        return NGX_ERROR;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_json_log_write_sink_syslog(ngx_log_t *log, ngx_pool_t* pool,
    ngx_syslog_peer_t *syslog, const char *txt)
{
    u_char   *line, *p;
    size_t    size = 0;
    size_t    len = 0;
    ssize_t   n = 0;

    len += sizeof("<255>Jan 01 00:00:00 ") - 1
            + ngx_cycle->hostname.len + 1
            + syslog->tag.len + 2
            + strlen(txt);

    line = ngx_pnalloc(pool, len);
    if (line == NULL) {
        return NGX_ERROR;
    }

    p = ngx_syslog_add_header(syslog, line);
    p = ngx_snprintf(p, len, "%s", txt);
    size = p - line;
    n = ngx_syslog_send(syslog, line, size);

    if (n < 0) {
        ngx_log_error(NGX_LOG_WARN, log, 0, "send() to syslog failed");
        return NGX_ERROR;

    } else if ((size_t) n != size) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
            "send() to syslog has written only %z of %uz", n, size);
        return NGX_ERROR;
    }
    return NGX_OK;
}

/* log handler - format and print */
static ngx_int_t
ngx_http_json_log_log_handler(ngx_http_request_t *r)
{
    size_t                           i;
    char                            *txt;
    ngx_str_t                        filter_val;
    ngx_http_json_log_loc_conf_t    *lc;
    ngx_json_log_output_location_t  *arr;
    ngx_json_log_output_location_t  *location;
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_str_t                        msg_id;
    ngx_http_json_log_main_conf_t   *mcf = NULL;
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
        ngx_strncasecmp((u_char *) "CONNECT", r->request_line.data, 7) == 0) {
        return NGX_OK;
    }

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

            if (filter_val.len == 0 ||
                (filter_val.len == 1 && filter_val.data[0] == '0')) {
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
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0,
                    "File write error!");
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
                ngx_log_error(NGX_LOG_EMERG, r->pool->log, 0,
                    "Syslog write error!");
            }
            continue;
        }

#if (NGX_HAVE_LIBRDKAFKA)
        if (mcf == NULL) {
            mcf = ngx_http_get_module_main_conf(r, ngx_http_json_log_module);
        }

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
                ngx_http_complex_value(r, location->kafka.http_msg_id_var,
                    &msg_id);
#if (NGX_DEBUG)
                ngx_log_error(NGX_LOG_DEBUG, r->pool->log, 0,
                    "http_json_log: kafka msg-id:[%v] msg:[%s]", &msg_id, txt);
#endif
            }

            ngx_json_log_kafka_produce(r->pool, mcf->kafka.rk,
                location->kafka.rkt, mcf->kafka.partition, txt, &msg_id);
        }
#endif
    }

    return NGX_OK;
}

#if (NGX_HAVE_STREAM)
/* log handler - format and print */
static ngx_int_t
ngx_stream_json_log_log_handler(ngx_stream_session_t *s)
{
    ngx_stream_json_log_srv_conf_t     *lc;
    ngx_str_t                           filter_val;
    char                               *txt;
    size_t                              i;
    ngx_json_log_output_location_t     *arr;
    ngx_json_log_output_location_t     *location;

#if (NGX_HAVE_LIBRDKAFKA)
    ngx_stream_json_log_main_conf_t    *mcf;

    mcf = ngx_stream_get_module_main_conf(s, ngx_stream_json_log_module);
#endif

    lc = ngx_stream_get_module_srv_conf(s, ngx_stream_json_log_module);

    if (!lc) {
        return NGX_OK;
    }

    /* Bypass if number of location is empty */
    if (!lc->locations->nelts) {
        return NGX_OK;
    }

    arr = lc->locations->elts;
    for (i = 0; i < lc->locations->nelts; ++i) {

        location = &arr[i];

        if (!location) {
            break;
        }

        /* Check filter result */
        if (location->format.stream_filter != NULL) {
            if (ngx_stream_complex_value(s,
                        location->format.stream_filter, &filter_val) != NGX_OK) {
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
        txt = ngx_json_log_items_dump_text(NGX_JSON_LOG_STREAM, s,
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

            if (ngx_json_log_write_sink_file(s->connection->pool->log,
                        location->file->fd, txt) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG,
                        s->connection->pool->log, 0, "File write error!");
            }
            continue;
        }

        /* Write to syslog */
        if (location->type == NGX_JSON_LOG_SINK_SYSLOG) {
            if (!location->syslog) {
                continue;
            }
            if (ngx_json_log_write_sink_syslog(s->connection->pool->log,
                        s->connection->pool,location->syslog, txt) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, s->connection->pool->log, 0, "Syslog write error!");
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
                    ngx_json_log_kafka_topic_new(s->connection->pool,
                            mcf->kafka.rk, location->kafka.rktc,
                            &location->location);
            }

            /* if failed to create topic */
            if (!location->kafka.rkt) {
                location->kafka.rkt = NGX_CONF_UNSET_PTR;
                /* WARN ?*/
                continue;
            }

            ngx_json_log_kafka_produce(s->connection->pool, mcf->kafka.rk,
                    location->kafka.rkt,
                    mcf->kafka.partition, txt, NULL);

         } // if KAFKA type
#endif
    } // for location

    return NGX_OK;
}
#endif

static ngx_int_t
ngx_http_json_log_pre_config(ngx_conf_t *cf)
{
    return NGX_OK;
}

static ngx_int_t
ngx_http_json_log_post_config(ngx_conf_t *cf)
{
    ngx_http_handler_pt            *h;
    ngx_http_core_main_conf_t      *cmcf;
    ngx_http_json_log_main_conf_t  *mcf = NULL;

    /* Register custom json memory functions */
    ngx_json_log_set_alloc_funcs();

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_json_log_log_handler;

    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_json_log_module);
    if (mcf == NULL) {
        return NGX_ERROR;
    }

    if (mcf->req_body != NULL) {
        ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
        ngx_http_top_request_body_filter = ngx_http_json_log_body_filter;
    }

    if (mcf->resp_headers != NULL) {
        ngx_http_next_body_filter = ngx_http_top_body_filter;
        ngx_http_top_body_filter = ngx_http_json_log_header_filter;
    }

    return NGX_OK;
}

static void *
ngx_http_json_log_create_main_conf(ngx_conf_t *cf) {
    ngx_http_json_log_main_conf_t *conf;

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

    conf->req_body = NULL;
    conf->resp_headers = NULL;

    return conf;
}

static void *
ngx_http_json_log_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_json_log_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_json_log_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* create an array for the output locations */
    conf->locations = ngx_array_create(cf->pool, 1,
        sizeof(ngx_json_log_output_location_t));

    return conf;
}

#if (NGX_HAVE_STREAM)
static void *
ngx_stream_json_log_create_main_conf(ngx_conf_t *cf)
{
    ngx_stream_json_log_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_json_log_main_conf_t));
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
ngx_stream_json_log_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_json_log_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_json_log_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* create an array for the output locations */
    conf->locations = ngx_array_create(cf->pool, 1,
        sizeof(ngx_json_log_output_location_t));

    return conf;
}


static ngx_int_t
ngx_stream_json_log_post_conf(ngx_conf_t *cf)
{
    ngx_stream_handler_pt        *h;
    ngx_stream_core_main_conf_t  *cmcf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_json_log_log_handler;

    return NGX_OK;
}
#endif

static ngx_int_t
ngx_http_json_log_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_variable_t            *v;
    ngx_http_variable_value_t      *vv;
    ngx_http_json_log_loc_conf_t   *lcf;
    ngx_http_json_log_main_conf_t  *mcf;

    if (in == NULL || r != r->main) {
        return ngx_http_next_request_body_filter(r, in);
    }

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_json_log_module);
    if (!lcf || !lcf->request_body ) {
        return ngx_http_next_request_body_filter(r, in);
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_json_log_module);
    if (!mcf) {
        return ngx_http_next_request_body_filter(r, in);
    }

    v = mcf->req_body;
    if (v == NULL) {
        return ngx_http_next_request_body_filter(r, in);
    }
    vv = ngx_http_get_indexed_variable(r, v->index);
    if (vv == NULL) {
        return ngx_http_next_request_body_filter(r, in);
    }

    v->set_handler(r, vv, (uintptr_t) in);

    return ngx_http_next_request_body_filter(r, in);
}

static ngx_int_t
ngx_http_json_log_header_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_variable_t            *v;
    ngx_http_variable_value_t      *vv;
    ngx_http_json_log_loc_conf_t   *lcf;
    ngx_http_json_log_main_conf_t  *mcf;

    if (r != r->main) {
        return ngx_http_next_body_filter(r, in);
    }

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_json_log_module);
    if (!lcf || !lcf->response_headers ) {
        return ngx_http_next_body_filter(r, in);
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_json_log_module);
    if (!mcf) {
        return ngx_http_next_body_filter(r, in);
    }

    v = mcf->resp_headers;
    if (v == NULL) {
        return ngx_http_next_body_filter(r, in);
    }
    vv = ngx_http_get_indexed_variable(r, v->index);
    if (vv == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    v->set_handler(r, vv, (uintptr_t) NULL);

    return ngx_http_next_body_filter(r, in);
}

static void
ngx_http_json_log_set_resp_headers( ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{

    if (r == NULL || r->out == NULL || r->header_size < 1
        || r->out->buf == NULL || r->out->buf->pos == NULL) {
        return;
    }

    v->len = r->header_size;
    v->data = (u_char *) ngx_json_log_buf_dup_len(r->pool, r->out->buf->pos,
        r->header_size);

    v->valid = 1;
    v->not_found = 0;
    v->escape = 1;
    v->no_cacheable = 0;
}

static ngx_int_t
ngx_http_json_log_get_resp_headers(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    return NGX_OK;
}

static void
ngx_http_json_log_set_req_body(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t                          len;
    ngx_chain_t                    *in = (ngx_chain_t *) data;
    ngx_chain_t                    *cl;
    ngx_json_log_body_queue_t      *f;
    ngx_json_log_variable_value_t  *vv;

    v->valid = 1;
    v->not_found = 0;
    v->escape = 1;
    v->no_cacheable = 1;

    //TODO: use location's limit
    for (cl = in; cl; cl = cl->next) {
        len = ngx_buf_size(cl->buf);
        if (len == 0) {
            continue;
        }
        if (v->data == NULL) {
            vv = ngx_palloc(r->pool, sizeof(ngx_json_log_variable_value_t));
            if (vv == NULL) {
                //TODO: LOG EMERG
                break;
            }
            ngx_queue_init(&vv->u.queue);
            vv->len = 0;
            v->data = (u_char *) vv;
            v->len = 0;
        } else {
            vv = (ngx_json_log_variable_value_t *) v->data;
        }

        f = ngx_palloc(r->pool, sizeof(ngx_json_log_body_queue_t));
        if (f == NULL) {
            //TODO: LOG EMERG
            break;
        }

        v->len += len;
        f->chunk.data = ngx_pcalloc(r->pool, len);
        ngx_memcpy(f->chunk.data, cl->buf->start, len);
        f->chunk.len = len;
        ngx_queue_insert_tail(&vv->u.queue, &f->queue);
        vv->len += 1;
        if (cl->buf->last_buf) {
            v->not_found = 0;
        }
    }
}

static ngx_int_t
ngx_http_json_log_get_req_body(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t                          c = 0;
    u_char                         *value = NULL;
    ngx_queue_t                    *q = NULL;
    ngx_json_log_body_queue_t      *f = NULL;
    ngx_json_log_variable_value_t  *vv = NULL;

    vv = (ngx_json_log_variable_value_t  *) v->data;

    v->valid = 0;
    v->not_found = 0;
    v->escape = 0;
    v->no_cacheable = 0;

    if (vv && v->len) {
        value = ngx_pcalloc(r->pool, v->len);
        if (value == NULL) {
            //TODO: log warn ...
            return NGX_ERROR;
        }

        for (q = ngx_queue_head(&vv->u.queue);
        q != ngx_queue_sentinel(&vv->u.queue);
        q = ngx_queue_next(q))
        {
            f = ngx_queue_data(q, ngx_json_log_body_queue_t, queue);
            ngx_memcpy(value+c, f->chunk.data, f->chunk.len);
            c += f->chunk.len;
        }

        v->data = value;
        v->valid = 1;
        return NGX_OK;
    }

    v->valid = 0;
    return NGX_OK;
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
    size_t                           i = 0, j = 0, prefix_len = 0;
    ngx_uint_t                       found = 0, needs_body = 0;
    ngx_uint_t                       needs_headers = 0;
    ngx_str_t                       *format_name = NULL, *value = NULL;
    ngx_str_t                       *args = cf->args->elts;
    ngx_http_variable_t             *v;
    ngx_json_log_item_t             *item = NULL;
    ngx_json_log_format_t           *format = NULL;
    ngx_http_json_log_loc_conf_t    *lc = conf;
    ngx_http_json_log_main_conf_t   *mcf = NULL;

    ngx_json_log_output_location_t  *new_location = NULL;

    if (!args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Invalid argument for HTTP log JSON output"
                           " location");
        return NGX_CONF_ERROR;
    }

    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_json_log_module);
    if (!mcf) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Missing main configuration");
        return NGX_CONF_ERROR;
    }

    /* Check if format exists by name */
    format_name = &args[2];
    format = mcf->formats->elts;
    for (i = 0; i < mcf->formats->nelts; i++) {

        if (ngx_strncmp(format_name->data,
                        format[i].name.data, format[i].name.len) != 0) {
            continue;
        }

        /* Found format */
        found = 1;

        item = format[i].items->elts;
        for (j = 0; j < format[i].items->nelts; j++) {

            if (item[j].var_name.data == NULL) {
                // no var used - literal value
                continue;
            }

            /* Checks if needs request body filter and sets variable */
            if (ngx_strncmp(item[j].var_name.data, req_body_var_name.data,
                req_body_var_name.len) == 0) {

                needs_body = 1;
                /* Prepare request body variable */
                if (mcf->req_body == NULL) {
                    v = ngx_http_add_variable(cf,
                        (ngx_str_t *) &req_body_var_name, 0);

                    if (v == NULL) {
                        //TODO: To log emergency
                        return NULL;
                    }

                    v->set_handler = ngx_http_json_log_set_req_body;
                    v->get_handler = ngx_http_json_log_get_req_body;
                    v->index = ngx_http_get_variable_index(cf,
                        (ngx_str_t *) &req_body_var_name);

                    mcf->req_body = v;
                }
            }

            /* Checks if needs response headers filter and sets variable */
            if (ngx_strncmp(item[j].var_name.data, resp_headers_var_name.data,
                resp_headers_var_name.len) == 0) {

                needs_headers = 1;
                if (mcf->resp_headers == NULL) {
                    v = ngx_http_add_variable(cf,
                        (ngx_str_t *) &resp_headers_var_name, 0);

                    if (v == NULL) {
                        //TODO: To log emergency
                        return NULL;
                    }

                    v->set_handler = ngx_http_json_log_set_resp_headers;
                    v->get_handler = ngx_http_json_log_get_resp_headers;
                    v->index = ngx_http_get_variable_index(cf,
                        (ngx_str_t *) &resp_headers_var_name);

                    mcf->resp_headers = v;
                }
            }
        }
        break;
    }

    /* Do not accept unknown format names */
    if (!found) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "http_json_log: Invalid format name [%V]", format_name);
        return NGX_CONF_ERROR;
    }

    value = &args[1];

    if (!NGX_JSON_LOG_HAS_FILE_PREFIX(value)
        #if (NGX_HAVE_LIBRDKAFKA)
        && ! NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)
        #endif
        && !NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Invalid prefix [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }

    new_location = ngx_array_push(lc->locations);
    if (!new_location) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to add [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }
    ngx_memzero(new_location, sizeof(*new_location));

    prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
    if (NGX_JSON_LOG_HAS_FILE_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_FILE;
        prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
#if (NGX_HAVE_LIBRDKAFKA)
    } else if (NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_KAFKA;
        prefix_len = NGX_JSON_LOG_KAFKA_OUT_LEN;
#endif
    } else if (NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_SYSLOG;
        prefix_len = NGX_JSON_LOG_SYSLOG_OUT_LEN;
    }

    /* Saves location without prefix. */
    new_location->location = args[1];
    new_location->location.len -= prefix_len;
    new_location->location.data += prefix_len;
    new_location->format = format[i];

    /* If sink type is file, then try to open it and save */
    if (new_location->type == NGX_JSON_LOG_SINK_FILE) {
        new_location->file = ngx_conf_open_file(cf->cycle,
            &new_location->location);
    }

    /* If sink type is syslog */
    if (new_location->type == NGX_JSON_LOG_SINK_SYSLOG) {
        new_location->syslog = ngx_pcalloc(cf->pool, sizeof(ngx_syslog_peer_t));

        if (new_location->syslog == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_syslog_process_conf(cf, new_location->syslog) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;
        }
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

        ngx_http_compile_complex_value_t     ccv;

        /*FIXME: Change this to an user's configured variable */

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
    }
#endif

    lc->request_body = needs_body;
    lc->response_headers = needs_headers;

    return NGX_CONF_OK;
}

/* reads and parses, format from configuration */
static ngx_int_t
ngx_json_log_read_format(ngx_conf_t *cf, ngx_json_log_module_type_e type,
    ngx_json_log_format_t *format)
{
    int                                  array_prefix_len = 0, offset = 0;
    int                                  matched = 0, ret=0;
    int                                  ovector[1024] = {0};
    char                                 value[1025] = {0};
    u_char                               errstr[NGX_MAX_CONF_ERRSTR] = {0};
    ngx_regex_compile_t                  rc;
    ngx_str_t                           *key_str, *value_str, *config;
    ngx_str_t                            spec;
    static ngx_str_t                     pattern =
        ngx_string("\\s*([^\\s\\|]+)(\\|base64|\\|hex|\\|hexdump)?\\s+([^\\s\\|;]+);");
    ngx_json_log_item_t                 *item;
    ngx_http_complex_value_t            *http_cv = NULL;
    ngx_http_compile_complex_value_t     http_ccv;
#if (NGX_HAVE_STREAM)
    ngx_stream_complex_value_t          *stream_cv = NULL;
    ngx_stream_compile_complex_value_t   stream_ccv;
#endif

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
                "Failed to configure json_log_format.");
            return NGX_ERROR;
        }

        key_str = ngx_pcalloc(cf->pool, sizeof(ngx_str_t));
        value_str = ngx_pcalloc(cf->pool, sizeof(ngx_str_t));

        if (matched != 4) {
            continue;
        }

        // i = 0 => all match with isize
        ret = pcre_copy_substring((const char *) spec.data, ovector, matched, 0,
            value, 1024);

        offset = ret;

        ret = pcre_copy_substring((const char *) spec.data, ovector, matched, 1,
            value, 1024);
        // i = 1 => key - item name
        key_str->data = ngx_pcalloc(cf->pool, ret);
        key_str->len = ret;
        ngx_cpystrn(key_str->data, (u_char *) value, ret + 1);

        ret = pcre_copy_substring((const char *) spec.data, ovector, matched, 3,
            value, 1024);
        // i = 3 => var -> value
        value_str->data = ngx_pcalloc(cf->pool, ret);
        value_str->len = ret;
        ngx_cpystrn(value_str->data, (u_char *) value, ret + 1);

        ret = pcre_copy_substring((const char *) spec.data, ovector, matched, 2,
            value, 1024);
        // i = 2 => encoding - item name
        //encoding = value;

        item = ngx_array_push(format->items);
        if (item == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                "Failed to configure json_log_format.");
            return NGX_ERROR;
        }
        ngx_memzero(item, sizeof(*item));
        /* encoding */
        if (ret) {
            if (ngx_strncmp(value, ngx_json_log_encoding_base64, ret) == 0) {
                item->encoding = ENCODING_BASE64;
            } else if (ngx_strncmp(value,
                ngx_json_log_encoding_hex, ret) == 0) {
                item->encoding = ENCODING_HEX;
            } else if (ngx_strncmp(value,
                ngx_json_log_encoding_hexdump, ret) == 0) {
                item->encoding = ENCODING_HEXDUMP;
            } else {
                item->encoding = ENCODING_NONE;
            }
        } else {
            item->encoding = ENCODING_NONE;
        }

        if (type == NGX_JSON_LOG_HTTP) {
            http_cv = ngx_pcalloc(cf->pool, sizeof(ngx_http_complex_value_t));
            if (http_cv == NULL) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                    "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            ngx_memzero(&http_ccv, sizeof(ngx_http_compile_complex_value_t));
            http_ccv.cf = cf;
            http_ccv.value = value_str;
            http_ccv.complex_value = http_cv;
            if (ngx_http_compile_complex_value(&http_ccv) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                    "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            item->http_ccv = (ngx_http_compile_complex_value_t *) http_cv;
        }

#if (NGX_HAVE_STREAM)
        if (type == NGX_JSON_LOG_STREAM) {
            stream_cv = ngx_pcalloc(cf->pool,
                sizeof(ngx_stream_complex_value_t));

            if (stream_cv == NULL) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                    "Failed to configure json_log_format.");
                return NGX_ERROR;
            }

            ngx_memzero(&stream_ccv,
                sizeof(ngx_stream_compile_complex_value_t));

            stream_ccv.cf = cf;
            stream_ccv.value = value_str;
            stream_ccv.complex_value = stream_cv;

            if (ngx_stream_compile_complex_value(&stream_ccv) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log, 0,
                    "Failed to configure json_log_format.");
                return NGX_ERROR;
            }

            item->stream_ccv = (ngx_stream_compile_complex_value_t *) stream_cv;
        }
#endif

        item->name = key_str;

        /* Saves var name */
        if (*value_str->data == '$') {
            item->var_name.data = ngx_pcalloc(cf->pool, value_str->len - 1);
            ngx_memcpy(item->var_name.data, value_str->data + 1,
                value_str->len - 1);
            item->var_name.len = value_str->len - 1;
        }

        item->is_array = 0;
        /* Check and save type from name prefix */
        /* Default is JSON_STRING type */
        item->type = JSON_STRING;

        if (ngx_strncmp(item->name->data, ngx_json_log_array_prefix, 2) == 0) {
            item->is_array = 1;
            array_prefix_len = 2;
        }

        if (ngx_strncmp(item->name->data + array_prefix_len,
            ngx_json_log_int_prefix, 2) == 0) {

            item->type = JSON_INTEGER;
            item->name->data += 2 + array_prefix_len;
            item->name->len -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
            ngx_json_log_real_prefix, 2) == 0) {

            item->type = JSON_REAL;
            item->name->data += 2 + array_prefix_len;
            item->name->len -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
            ngx_json_log_string_prefix, 2) == 0) {

            item->type = JSON_STRING;
            item->name->data += 2 + array_prefix_len;
            item->name->len -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                               ngx_json_log_null_prefix, 2) == 0) {

            item->type = JSON_NULL;
            item->name->data += 2 + array_prefix_len;
            item->name->len -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
            ngx_json_log_boolean_prefix, 2) == 0) {

            if (ngx_strncmp(value_str->data + array_prefix_len,
                ngx_json_log_true_value, 4) == 0) {
                item->type = JSON_TRUE;
            } else {
                item->type = JSON_FALSE;
            }

            item->name->data += 2 + array_prefix_len;
            item->name->len -= 2 + array_prefix_len;
        } else {
            item->type = JSON_STRING;
            if (item->is_array) {
                item->name->data += array_prefix_len;
                item->name->len -= array_prefix_len;
            }
        }

        /* adjust pointers and size for reading the next item*/
        spec.data += offset;
        spec.len -= offset;

        matched = ngx_regex_exec(rc.regex, &spec, ovector, 1024);
    }

    /* sort items .... this is very import for serialization output alg*/
    ngx_sort(format->items->elts, (size_t) format->items->nelts,
             sizeof(ngx_json_log_item_t), ngx_json_log_items_cmp);

    return NGX_OK;
}

static char *
ngx_http_json_log_main_format_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{

    ngx_str_t                          s;
    ngx_str_t                         *args;
    ngx_uint_t                         items_len;
    ngx_json_log_format_t             *new_format;
    ngx_http_json_log_main_conf_t     *mcf = conf;
    ngx_http_compile_complex_value_t   ccv;

    args = cf->args->elts;

    if (!args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid empty format");
        return NGX_CONF_ERROR;
    }

    items_len = ngx_json_log_str_split_count(&args[2], ';');
    if (!items_len) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to create empty json log format.");
        return NGX_CONF_ERROR;
    }

    /*TODO*: to verify if format name is duplicated */
    new_format = ngx_array_push(mcf->formats);
    if (!new_format) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to create json log format.");
        return NGX_CONF_ERROR;
    }
    ngx_memzero(new_format, sizeof(*new_format));

    /* Saves the format name and the format spec value */
    new_format->name = args[1];
    new_format->config = args[2];

    /* Create an array with the number of items found */
    new_format->items = ngx_array_create(cf->pool, items_len,
        sizeof(ngx_json_log_item_t)
    );

    if (ngx_json_log_read_format(cf, NGX_JSON_LOG_HTTP, new_format) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid format read");
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
            new_format->http_filter = ccv.complex_value;
        }
    }
    return NGX_CONF_OK;
}

#if (NGX_HAVE_STREAM)
char *
ngx_stream_json_log_main_format_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_str_t                           *args;
    ngx_json_log_format_t               *new_format;
    ngx_stream_json_log_main_conf_t     *mcf = conf;
    ngx_stream_compile_complex_value_t   ccv;
    ngx_str_t                            s;
    ngx_uint_t                           items_len;

    args = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid empty format");
        return NGX_CONF_ERROR;
    }

    items_len = ngx_json_log_str_split_count(&args[2], ';');
    if (! items_len) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to create empty json log format.");
        return NGX_CONF_ERROR;
    }

    /*TODO*: to verify if format name is duplicated */
    new_format = ngx_array_push(mcf->formats);
    if (!new_format) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to create json log format.");
        return NGX_CONF_ERROR;
    }
    ngx_memzero(new_format, sizeof(*new_format));

    /* Saves the format name and the format spec value */
    new_format->name   = args[1];
    new_format->config = args[2];

    /* Create an array with the number of items found */
    new_format->items = ngx_array_create(cf->pool, items_len,
        sizeof(ngx_json_log_item_t));

    if (ngx_json_log_read_format(cf, NGX_JSON_LOG_STREAM,
        new_format) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid format read");
        return NGX_CONF_ERROR;
    }

    /*check and save the if filter condition */
    if (cf->args->nelts >= 4 && args[3].data != NULL) {

        if (ngx_strncmp(args[3].data, "if=", 3) == 0) {
            s.len = args[3].len - 3;
            s.data = args[3].data + 3;

            ngx_memzero(&ccv, sizeof(ngx_stream_compile_complex_value_t));

            ccv.cf = cf;
            ccv.value = &s;
            ccv.complex_value = ngx_pcalloc(cf->pool,
                sizeof(ngx_stream_complex_value_t));
            if (ccv.complex_value == NULL) {
                return NGX_CONF_ERROR;
            }
            if (ngx_stream_compile_complex_value(&ccv) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
            new_format->stream_filter = ccv.complex_value;
        }
    }
    return NGX_CONF_OK;
}

static char *
ngx_stream_json_log_srv_output(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_json_log_srv_conf_t       *lc = conf;
    ngx_stream_json_log_main_conf_t      *mcf = NULL;
    ngx_json_log_output_location_t       *new_location = NULL;
    ngx_json_log_format_t                *format;
    ngx_str_t                            *args = cf->args->elts;
    ngx_str_t                            *value = NULL;
    ngx_str_t                            *format_name;
    ngx_uint_t                            found = 0;
    size_t                                prefix_len;
    size_t                                i;

    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Invalid argument for HTTP log JSON output location");
        return NGX_CONF_ERROR;
    }

    mcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_json_log_module);
    if (!mcf) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Missing main configuration");
        return NGX_CONF_ERROR;
    }

    /* Check if format exists by name */
    format_name = &args[2];
    format = mcf->formats->elts;
    for (i = 0; i < mcf->formats->nelts; i++) {
        if (ngx_strncmp(format_name->data, format[i].name.data,
            format[i].name.len) == 0) {
            found = 1;
            break;
        }
    }

    /* Do not accept unknown format names */
    if (!found)  {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "http_json_log: Invalid format name [%V]",
            format_name);
        return NGX_CONF_ERROR;
    }

    value = &args[1];

    if (! NGX_JSON_LOG_HAS_FILE_PREFIX(value)
#if (NGX_HAVE_LIBRDKAFKA)
&& ! NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)
#endif
&& ! NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)
) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Invalid prefix [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }

    new_location = ngx_array_push(lc->locations);
    if (!new_location) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "Failed to add [%v] for HTTP log JSON output location", value);
        return NGX_CONF_ERROR;
    }
    ngx_memzero(new_location, sizeof(*new_location));

    prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
    if (NGX_JSON_LOG_HAS_FILE_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_FILE;
        prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
#if (NGX_HAVE_LIBRDKAFKA)
    }
    else if (NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_KAFKA;
        prefix_len = NGX_JSON_LOG_KAFKA_OUT_LEN;
#endif
    } else if (NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_SYSLOG;
        prefix_len = NGX_JSON_LOG_SYSLOG_OUT_LEN;
    }

    /* Saves location without prefix. */
    new_location->location       = args[1];
    new_location->location.len   -= prefix_len;
    new_location->location.data  += prefix_len;
    new_location->format         =  format[i];

    /* If sink type is file, then try to open it and save */
    if (new_location->type == NGX_JSON_LOG_SINK_FILE) {
        new_location->file = ngx_conf_open_file(cf->cycle,
            &new_location->location);
    }

    /* If sink type is syslog */
    if (new_location->type == NGX_JSON_LOG_SINK_SYSLOG) {
        new_location->syslog = ngx_pcalloc(cf->pool, sizeof(ngx_syslog_peer_t));
        if (new_location->syslog == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_syslog_process_conf(cf, new_location->syslog) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;
        }
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
        stream_json_log_has_kafka_locations = NGX_OK;
    }
#endif

    return NGX_CONF_OK;
}
#endif

/* Helper functions for allocate/free from memory pool for libjsasson. */
static void
set_current_mem_pool(ngx_pool_t *pool)
{
    current_pool = pool;
}


static ngx_pool_t *
get_current_mem_pool()
{
    return current_pool;
}

ngx_int_t
ngx_json_log_output_cxt_new(ngx_pool_t *pool, ngx_json_log_output_cxt_t *ctx,
    ngx_uint_t items_len)
{
    set_current_mem_pool(pool);

    ctx->root = json_object();
    if (ctx->root == NULL) {
        return NGX_ERROR;
    }

    ctx->items = ngx_array_create(get_current_mem_pool(), items_len,
        sizeof(ngx_json_log_value_t));

    if (!ctx->items) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

/* allocated json data in memory pool */
static void *
ngx_json_log_malloc(size_t size)
{

    ngx_pool_t *pool = get_current_mem_pool();
    if (!pool) {
        return NULL;
    }
    void *mem = ngx_palloc(pool, size);
    if (!mem) {
        ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
            "Failed to allocate memory at json pool");
    }
    return mem;
}


/* free json data memory from pool */
static void
ngx_json_log_free(void *p)
{
    ngx_pool_t *pool = get_current_mem_pool();
    if (!pool) {
        return;
    }
    ngx_pfree(pool, p);
}


void
ngx_json_log_set_alloc_funcs()
{
    json_set_alloc_funcs(ngx_json_log_malloc, ngx_json_log_free);
}


/* output algorithm */
/* find the place to put the new item */
/* finds the saved parent for a item name */
static ngx_json_log_value_t *
ngx_json_log_find_saved_parent(ngx_pool_t *pool, ngx_array_t *arr_items,
    ngx_str_t *name, size_t len)
{
    size_t                 j;
    ngx_json_log_value_t  *rec = arr_items->elts;

    for (j = 0; j < arr_items->nelts; j++) {
        ngx_json_log_value_t *r = &rec[j];

        if (r && ngx_strncasecmp(r->label.data, name->data, len) == 0) {
            return r;
        }
    }
    return NULL;
}


/* creates a string from last label from path */
static const char *
ngx_json_log_label_key_dup(ngx_pool_t *pool, ngx_str_t *path, size_t max)
{
    int      l = ngx_min(path->len, max);
    int      start = l - 1;
    int      i;
    u_char  *copy;

    if (!path || !path->data || !path->len)
        return NULL;

    for (i = start; i >= 0; --i) {
        if (path->data[i] == '.') {
            copy = ngx_pcalloc(pool, start - i + 1);
            if (copy == NULL) {
                return NULL;
            }
            ngx_cpystrn(copy, &path->data[i + 1], start - i + 1);
            return (const char *) copy;
        }
    }

    /* full copy - single label*/
    copy = ngx_pcalloc(pool, l + 1);
    if (copy == NULL) {
        return NULL;
    }

    ngx_cpystrn(copy, path->data, l + 1);
    return (const char *) copy;
}

/* helper function to find next item level position in path*/
static u_char *
ngx_json_log_has_label_pos(u_char *path)
{
    u_char *ptr = NULL;

    if (!path)
        return NULL;

    ptr = (u_char *) strchr((const char *) path, '.');
    return ptr;
}


static json_t *
ngx_json_log_find_parent(ngx_pool_t *pool, ngx_array_t *arr_items,
    json_t *parent, ngx_str_t *path)
{
    json_t                *p = parent;
    u_char                *pos = &path->data[0];
    size_t                 len;
    ngx_json_log_value_t  *saved;
    ngx_json_log_value_t  *level = NULL;

    if (!path || !path->data || !path->len)
        return parent;

    while ((pos = ngx_json_log_has_label_pos(pos)) != NULL) {
        len = pos - path->data;

        saved = ngx_json_log_find_saved_parent(pool, arr_items, path, len);

        if (saved) {
            p = saved->node;
            ++pos;
            continue;
        }

        level = ngx_array_push(arr_items);
        if (!level) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                "json_log: Failed allocate new http_json_log level");
            return NULL;
        }
        ngx_memzero(level, sizeof(*level));

        level->label.len = len;
        level->label.data = ngx_palloc(pool, len);
        if (!level->label.data) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                "Failed allocate new json_log level label");
            return NULL;
        }
        ngx_cpystrn(level->label.data, path->data, len + 1);

        /* FIXME - breaks first level*/
        /*
           if (ngx_json_log_str_clone(pool, path, &level->label) != NGX_OK) {
           ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
           "json_log: Failed allocate new http_json_log le_vel");
           }
           */

        level->node = json_object();
        /* set node to parent */
        const char *key = ngx_json_log_label_key_dup(pool, &level->label,
            level->label.len);

        json_object_set(p, key, level->node);
        p = level->node;
        ++pos;
    }
    return p;
}


/* adds a typed json node to a parent node */
static void
ngx_json_log_add_json_node(json_t *base, int is_array, json_type type,
    ngx_json_log_encoding_e encoding, const char *key,
    ngx_json_node_value_t *var_value)
{

    char             *table_key = NULL;
    size_t            i, start;
    json_t           *parent = base;
    json_t           *parent_array = NULL;
    json_t           *node = NULL, *item_node = NULL;
    ngx_str_t         encoded;
    ngx_str_t        *value;
    ngx_pool_t       *pool = current_pool;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header;

    if (is_array) {
        parent_array = json_object_get(base, key);
        if (parent_array == NULL) {
            parent_array = json_array();
            json_object_set(parent, key, parent_array);
        }
        parent = parent_array;
    }

    if (var_value == NULL) {
        node = json_null();
        if (!is_array) {
            json_object_set(parent, key, node);
        } else {
            json_array_append(parent, node);
        }
        return;
    }

    if (var_value->type == NGX_JSON_LOG_NODE_VALUE_STR) {
        value = var_value->v.str;

        if (type == JSON_STRING) {

            /* it's a string type */
            /* multiple types of encoding */

            if (value->len == 0) {
                node = json_stringn("", 0);
            } else if (encoding == ENCODING_NONE) {
                node = json_stringn((const char *) value->data, value->len);
            } else if (encoding == ENCODING_BASE64) {
                encoded.len = ngx_base64_encoded_length(value->len);
                encoded.data = ngx_pcalloc(pool, encoded.len);
                if (encoded.data == NULL) {
                    //TODO: LOG WARN
                    node = json_null();
                } else {
                    ngx_encode_base64(&encoded, value);
                    node = json_stringn((const char *) encoded.data,
                        encoded.len);
                }
            } else if (encoding == ENCODING_HEX) {
                encoded.len = value->len * 2;
                encoded.data = ngx_pcalloc(pool, encoded.len);
                if (encoded.data == NULL) {
                    //TODO: LOG WARN
                    node = json_null();
                } else {
                    ngx_hex_dump(encoded.data, value->data, value->len);
                    node = json_stringn((const char *) encoded.data,
                        encoded.len);
                }
            } else if (encoding == ENCODING_HEXDUMP) {
                encoded.len = ngx_json_log_hexdump_length(value->len, 16);
                encoded.data = ngx_pcalloc(pool, encoded.len);
                if (encoded.data == NULL) {
                    //TODO: LOG WARN
                    node = json_null();
                } else {
                    ngx_json_log_hexdump(value, &encoded);
                    node = json_array();
                    start = 0;
                    for (i = 0; i < encoded.len; ++i) {
                        if (encoded.data[i] == '\n') {
                            item_node = json_stringn(
                                (const char *) encoded.data + start,
                                (i - start));
                            json_array_append(node, item_node);
                            ++i;
                            start = i;
                        }
                    }
                }
            }

        } else if (type == JSON_INTEGER) {
            /* it's a integer type*/
            ngx_int_t val_int = ngx_atoi(value->data, value->len);
            node = json_integer(val_int);
        } else if (type == JSON_TRUE) {
            /* it's a true type*/
            node = json_true();
        } else if (type == JSON_FALSE) {
            /* it's a false type*/
            node = json_false();
        } else if (type == JSON_NULL) {
            /* it's a null type*/
            node = json_null();
        } else if (type == JSON_REAL) {
            /* it's a real type */
            //u_char *nptr  = ngx_palloc(r->pool, value.len + 1);
            u_char *nptr = ngx_json_log_str_dup(pool, value);
            if (nptr) {
                char *endptr = (char *) nptr + value->len;
                double val_real = strtod((const char *) nptr, &endptr);
                node = json_real(val_real);
            }
        } else {
            node = json_stringn((const char *) value->data, value->len);
        }
    }

    if (var_value->type == NGX_JSON_LOG_NODE_TABLE_LIST) {
        if (var_value->v.part == NULL) {
            node = json_null();
        } else {
            node = json_object();
            part = var_value->v.part;
            header = part->elts;
            for (i = 0; i < part->nelts; ++i) {
                if (!header[i].key.data || !header[i].key.len) {
                    continue;
                }
                table_key = ngx_pcalloc(pool, header[i].key.len + 1);
                if (!table_key) {
                    //TODO: LOG WARN
                } else {
                    ngx_memcpy(table_key, header[i].key.data, header[i].key.len);
                    if (header[i].value.data == NULL) {
                        item_node = json_null();
                    } else {
                        item_node = json_stringn(
                            (const char *) header[i].value.data,
                            header[i].value.len);
                    }
                    json_object_set(node, (const char *) table_key, item_node);
                }
            }
        }
    }

    if (node) {
        if (!is_array) {
            json_object_set(parent, key, node);
        } else {
            json_array_append(parent, node);
        }
    }
}

ngx_int_t
ngx_json_log_output_add_item(ngx_json_log_module_type_e type, void *rs,
    ngx_json_log_output_cxt_t *output_ctx,
    ngx_json_log_item_t *item)
{

    json_t                         *parent = output_ctx->root;
    uint32_t                        levels = 0;
    ngx_str_t                       str;
    ngx_int_t                       err = 0;
    const char                     *key = NULL;
    ngx_http_request_t             *r = NULL;
#if (NGX_HAVE_STREAM)
    ngx_stream_session_t           *s = NULL;
    ngx_stream_complex_value_t     *stream_ccv = NULL;
#endif
    ngx_json_node_value_t           value;
    ngx_http_complex_value_t       *http_ccv = NULL;

    value.type = NGX_JSON_LOG_NODE_VALUE_STR;
    if (type == NGX_JSON_LOG_HTTP) {

        r = (ngx_http_request_t *) rs;

        if (item != NULL && item->var_name.data != NULL && ngx_strncmp(
                item->var_name.data, "http_json_log_req_headers",
                sizeof("http_json_log_req_headers")-1 ) == 0) {
           value.v.part = &r->headers_in.headers.part;
           value.type = NGX_JSON_LOG_NODE_TABLE_LIST;
        } else {
            http_ccv = (ngx_http_complex_value_t *) item->http_ccv;
            err = ngx_http_complex_value(r, http_ccv, &str);
        }
    }
#if (NGX_HAVE_STREAM)
    else if (type == NGX_JSON_LOG_STREAM) {
        s = (ngx_stream_session_t *) rs;
        stream_ccv = (ngx_stream_complex_value_t *) item->stream_ccv;
        err = ngx_stream_complex_value(s, stream_ccv, &str);
    }
#endif
    else {
        ngx_log_error(NGX_LOG_ERR, current_pool->log, 0,
                      "Invalid module type");
        return NGX_ERROR;
    }

    /* if complex value compilation failed */
    if (err) {
        ngx_log_error(NGX_LOG_ERR, current_pool->log, 0,
                      "failed get value for [%v]", item->name);
        return NGX_ERROR;
    }

    levels = ngx_json_log_str_split_count(item->name, '.');

    if (levels) {
        /* find parent and if need it build it */
        parent = ngx_json_log_find_parent(current_pool,
                                          output_ctx->items, parent, item->name);
    } else {
        /* if it is a basic item  */
        parent = output_ctx->root;
    }

    /* add value to parent location */
    key = ngx_json_log_label_key_dup(current_pool,
                                     item->name, item->name->len);

    if (value.type == NGX_JSON_LOG_NODE_VALUE_STR) {
        value.v.str = &str;
    }

    ngx_json_log_add_json_node(parent,
                               item->is_array, item->type, item->encoding,
                               key, &value);

    return NGX_OK;
}


/* dumps to text format the JSON for the items for this request. */
static char *
ngx_json_log_items_dump_text(ngx_json_log_module_type_e type, void *rs,
    ngx_array_t *items)
{

    char                       *txt = NULL, *dump = NULL;
    size_t                      i, dump_len;
    ngx_http_request_t         *r = NULL;
#if (NGX_HAVE_STREAM)
    ngx_stream_session_t       *s = NULL;
#endif
    ngx_json_log_item_t        *item;
    ngx_json_log_output_cxt_t   ctx;

    if (type == NGX_JSON_LOG_HTTP) {
        r = (ngx_http_request_t *) rs;
        set_current_mem_pool(r->pool);
#if (NGX_HAVE_STREAM)
    } else if (type == NGX_JSON_LOG_STREAM) {
        s = (ngx_stream_session_t *) rs;
        set_current_mem_pool(s->connection->pool);
#endif
    }

    if (ngx_json_log_output_cxt_new(
            current_pool,
            &ctx,
            items->nelts) != NGX_OK) {
        set_current_mem_pool(NULL);
        return txt;
    }

    /* Put each item value */
    item = items->elts;
    for (i = 0; i < items->nelts; i++) {
        ngx_json_log_output_add_item(type, rs, &ctx, &item[i]);
    }

    dump = json_dumps(ctx.root,
                      JSON_INDENT(0) |
                      JSON_REAL_PRECISION(2) |
                      JSON_COMPACT);

    set_current_mem_pool(NULL);

    if (!dump) {
        return NULL;
    }

    dump_len = strlen(dump);
    /* Empty text  */
    if (!dump_len) {
        return NULL;
    }

    if (type == NGX_JSON_LOG_HTTP) {
        txt = ngx_pcalloc(r->pool, dump_len + 2);
    }

#if (NGX_HAVE_STREAM)
    if (type == NGX_JSON_LOG_STREAM) {
        txt = ngx_pcalloc(s->connection->pool, dump_len + 2);
    }
#endif

    if (!txt) {
        return NULL;
    }

    ngx_memcpy(txt, dump, dump_len);
    txt[dump_len] = '\n';

    return txt;
}

/* compares two items by name */
static ngx_int_t
ngx_json_log_items_cmp(
        const void *left,
        const void *right)
{
    const ngx_json_log_item_t   *l = left;
    const ngx_json_log_item_t   *r = right;

    return ngx_strncasecmp(
            l->name->data, r->name->data,
            ngx_max(l->name->len, r->name->len));
}

#if (NGX_HAVE_LIBRDKAFKA)

/* kafka configuration */
static rd_kafka_topic_conf_t *
ngx_json_log_kafka_topic_conf_new(ngx_pool_t *pool) {
    rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
    if (!topic_conf) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "json_log: Error allocating kafka topic conf");
    }
    return topic_conf;
}

/* create new configuration */
static rd_kafka_conf_t *
ngx_json_log_kafka_conf_new(ngx_pool_t *pool)
{
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    if (!conf) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "json_log: Error allocating kafka conf");
    }
    return conf;
}

/* set an integer config value */
static ngx_int_t
ngx_json_log_kafka_conf_set_int(ngx_pool_t *pool, rd_kafka_conf_t *conf,
    const char * key, intmax_t value)
{
    char                 buf[NGX_INT64_LEN + 1] = {0};
    char                 errstr[NGX_JSON_LOG_KAFKA_ERROR_MSG_LEN] = {0};
    uint32_t             errstr_sz = sizeof(errstr);
    rd_kafka_conf_res_t  ret;

    snprintf(buf, NGX_INT64_LEN, "%lu", value);

    ret = rd_kafka_conf_set(conf, key, buf, errstr, errstr_sz);
    if (ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to set kafka conf [%s] => [%s] : %s",
                key, buf, errstr);
        return NGX_ERROR;
    }
    return NGX_OK;
}

/* set a string config value */
static ngx_int_t
ngx_json_log_kafka_conf_set_str(ngx_pool_t *pool, rd_kafka_conf_t *conf,
    const char *key, ngx_str_t *str)
{
    char                 errstr[NGX_JSON_LOG_KAFKA_ERROR_MSG_LEN] = {0};
    uint32_t             errstr_sz = sizeof(errstr);
    rd_kafka_conf_res_t  ret;

    u_char *value = ngx_json_log_str_dup(pool, str);
    if (!value) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to set kafka conf [%s] : %s",
                key, errstr);
        return NGX_ERROR;
    }

    ret = rd_kafka_conf_set(conf, key, (const char *) value, errstr, errstr_sz);
    if (ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to set kafka conf [%s] => [%s] : %s",
                key,(const char *) value, errstr);
        return NGX_ERROR;
    }
    return NGX_OK;
}

static ngx_int_t
ngx_json_log_kafka_topic_conf_set_str(ngx_pool_t *pool,
    rd_kafka_topic_conf_t *topic_conf, const char *key, ngx_str_t *str)
{
    char                  errstr[NGX_JSON_LOG_KAFKA_ERROR_MSG_LEN] = {0};
    uint32_t              errstr_sz = sizeof(errstr);
    rd_kafka_conf_res_t   ret;

    u_char *value = ngx_json_log_str_dup(pool, str);
    if (!value) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to set kafka topic conf [%s] : %s",
                key, errstr);
        return NGX_ERROR;
    }

    ret = rd_kafka_topic_conf_set(topic_conf,
            key, (const char *) value, errstr, errstr_sz);
    if (ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to set kafka topic conf [%s] => [%s] : %s",
                key,(const char *) value, errstr);
        return NGX_ERROR;
    }
    return NGX_OK;
}

/* create a kafka handler for producing messages */
static rd_kafka_t *
ngx_json_log_kafka_producer_new(ngx_pool_t *pool, rd_kafka_conf_t *conf)
{
    char         errstr[NGX_JSON_LOG_KAFKA_ERROR_MSG_LEN] = {0};
    uint32_t     errstr_sz = sizeof(errstr);
    rd_kafka_t   *rk = NULL;

    /* Create Kafka handle */
    if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, errstr_sz))) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "json_log: error allocating kafka handler");
        return NULL;
    }

    return rk;
}

static size_t
ngx_json_log_kafka_add_brokers(ngx_pool_t *pool, rd_kafka_t *rk,
    ngx_array_t *brokers)
{
    u_char     *value = NULL;
    size_t      ret = 0;
    ngx_str_t  *rec;
    ngx_str_t  *broker;

    rec = brokers->elts;
    for (size_t i = 0; i < brokers->nelts; ++i) {

        broker = &rec[i];
        value = ngx_json_log_str_dup(pool, broker);

        if ( rd_kafka_brokers_add(rk, (const char *) value)){
            ngx_log_error(NGX_LOG_INFO, pool->log, 0,
                    "json_log: broker \"%V\" configured", broker);
            ++ret;
        } else {
            ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                    "json_log: failed to configure \"%V\"", broker);
        }
    }
    return ret;
}

static rd_kafka_topic_t *
ngx_json_log_kafka_topic_new(ngx_pool_t *pool, rd_kafka_t *rk,
    rd_kafka_topic_conf_t *topic_conf, ngx_str_t *topic)
{
    u_char *value = ngx_json_log_str_dup(pool, topic);
    rd_kafka_topic_t * rkt;
    if (! rk ) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "json_log: missing kafka handler");
        return NULL;
    }

    rkt = rd_kafka_topic_new(rk, (const char *)value, topic_conf);
    if (!rkt) {
        /* FIX ME - Why sooooo quiet! */
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "json_log: failed to create topic \"%V\"", topic);
    }

    return rkt;
}

static ngx_int_t
ngx_json_log_init_kafka(ngx_pool_t *pool, ngx_json_log_main_kafka_conf_t *kafka)
{
    if (! kafka) {
        return NGX_ERROR;
    }

    kafka->rk                  = NULL;
    kafka->rkc                 = NULL;

    kafka->brokers             = ngx_array_create(pool, 1, sizeof(ngx_str_t));

    if (! kafka->brokers) {
        return NGX_ERROR;
    }

    kafka->client_id.data      = NULL;
    kafka->compression.data    = NULL;
    kafka->log_level           = NGX_CONF_UNSET_UINT;
    kafka->max_retries         = NGX_CONF_UNSET_UINT;
    kafka->buffer_max_messages = NGX_CONF_UNSET_UINT;
    kafka->backoff_ms          = NGX_CONF_UNSET_UINT;
    kafka->partition           = NGX_CONF_UNSET;

    return NGX_OK;
}

static ngx_int_t
ngx_json_log_configure_kafka(ngx_pool_t *pool,
    ngx_json_log_main_kafka_conf_t *conf)
{
    /* kafka */
    /* configuration kafka constants */
    static const char *conf_client_id_key          = "client.id";
    static const char *conf_compression_codec_key  = "compression.codec";
    static const char *conf_log_level_key          = "log_level";
    static const char *conf_max_retries_key        = "message.send.max.retries";
    static const char *conf_buffer_max_msgs_key    = "queue.buffering.max.messages";
    static const char *conf_retry_backoff_ms_key   = "retry.backoff.ms";

    /* - default values - */
    static ngx_str_t  kafka_compression_default_value = ngx_string("snappy");
    static ngx_str_t  kafka_client_id_default_value = ngx_string("nginx");
    static ngx_int_t  kafka_log_level_default_value = 6;
    static ngx_int_t  kafka_max_retries_default_value = 0;
    static ngx_int_t  kafka_buffer_max_messages_default_value = 100000;
    static ngx_msec_t kafka_backoff_ms_default_value = 10;

#if (NGX_DEBUG)
    static const char *conf_debug_key              = "debug";
    static ngx_str_t   conf_all_value              = ngx_string("all");
#endif

    /* create kafka configuration */
    conf->rkc = ngx_json_log_kafka_conf_new(pool);
    if (! conf->rkc) {
        return NGX_ERROR;
    }

    /* configure compression */
    if ((void*) conf->compression.data == NULL) {
        conf->compression = kafka_compression_default_value;
        ngx_json_log_kafka_conf_set_str(pool, conf->rkc,
            conf_compression_codec_key, &kafka_compression_default_value);
    } else {
        ngx_json_log_kafka_conf_set_str(pool, conf->rkc,
            conf_compression_codec_key, &conf->compression);
    }

    /* configure max messages, max retries, retry backoff default values if unset*/
    if (conf->buffer_max_messages == NGX_CONF_UNSET_UINT) {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc,
            conf_buffer_max_msgs_key, kafka_buffer_max_messages_default_value);
    } else {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc,
            conf_buffer_max_msgs_key, conf->buffer_max_messages);
    }

    if (conf->max_retries == NGX_CONF_UNSET_UINT) {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc, conf_max_retries_key,
            kafka_max_retries_default_value);
    } else {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc, conf_max_retries_key,
            conf->max_retries);
    }

    if (conf->backoff_ms == NGX_CONF_UNSET_MSEC) {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc,
            conf_retry_backoff_ms_key, kafka_backoff_ms_default_value);
    } else {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc,
            conf_retry_backoff_ms_key, conf->backoff_ms);
    }

    /* configure default client id if not set*/
    if ((void*) conf->client_id.data == NULL) {
        ngx_json_log_kafka_conf_set_str(pool, conf->rkc, conf_client_id_key,
            &kafka_client_id_default_value);
    } else {
        ngx_json_log_kafka_conf_set_str(pool, conf->rkc, conf_client_id_key,
            &conf->client_id);
    }

    /* configure default log level if not set*/
    if (conf->log_level == NGX_CONF_UNSET_UINT) {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc, conf_log_level_key,
            kafka_log_level_default_value);
    } else {
        ngx_json_log_kafka_conf_set_int(pool, conf->rkc, conf_log_level_key,
            conf->log_level);
    }

    /* configure partition */
    if (conf->partition == NGX_CONF_UNSET) {
        conf->partition = RD_KAFKA_PARTITION_UA;
    }

#if (NGX_DEBUG)
    /* configure debug */
    ngx_json_log_kafka_conf_set_str(pool,conf->rkc, conf_debug_key,
        &conf_all_value);
#endif
    /* create kafka handler */
    conf->rk = ngx_json_log_kafka_producer_new(pool, conf->rkc);

    if (! conf->rk) {
        return NGX_ERROR;
    }
    /* set client log level */
    if (conf->log_level == NGX_CONF_UNSET_UINT) {
        rd_kafka_set_log_level(conf->rk, kafka_log_level_default_value);
    } else {
        rd_kafka_set_log_level(conf->rk, conf->log_level);
    }
    /* configure brokers */
    conf->valid_brokers = ngx_json_log_kafka_add_brokers(pool,
            conf->rk, conf->brokers);

    if (!conf->valid_brokers) {
        ngx_log_error(NGX_LOG_ALERT, pool->log, 0,
                "json_log: failed to configure at least a kafka broker.");
        return NGX_ERROR;
    }

    return NGX_OK;
}

static void
ngx_json_log_kafka_topic_disable_ack(ngx_pool_t *pool,
    rd_kafka_topic_conf_t *rktc)
{
    static const char *conf_req_required_acks_key  = "request.required.acks";
    static ngx_str_t   conf_zero_value             = ngx_string("0");

    if (! pool) {
        return;
    }

    if (! rktc) {
        return;
    }

    ngx_json_log_kafka_topic_conf_set_str(pool, rktc,
        conf_req_required_acks_key, &conf_zero_value);
}

static void
ngx_json_log_kafka_produce(ngx_pool_t *pool, rd_kafka_t *rk,
    rd_kafka_topic_t * rkt, ngx_int_t partition, char * txt, ngx_str_t *msg_id)
{
    int   err;

    /* FIXME : Reconnect support */
    /* Send/Produce message. */
    if ((err = rd_kafka_produce(
        rkt,
        partition,
        RD_KAFKA_MSG_F_COPY,
        /* Payload and length */
        txt, strlen(txt),
        /* Optional key and its length */
        msg_id && msg_id->data ? (const char *) msg_id->data: NULL,
        msg_id ? msg_id->len : 0,
        /* Message opaque, provided in
        * delivery report callback as
        * msg_opaque. */
        NULL)) == -1) {

        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
            "%% Failed to produce to topic %s "
            "partition %i: %s - %d\n",
            rd_kafka_topic_name(rkt),
            partition,
#if RD_KAFKA_VERSION < 0x000b01ff
            rd_kafka_err2str(rd_kafka_errno2err(err)),
#else
            rd_kafka_err2str(rd_kafka_last_error()),
#endif
            err);
    }
#if (NGX_DEBUG)

    if (err) {
        ngx_log_error(NGX_LOG_DEBUG, pool->log, 0,
                "http_json_log: kafka msg:[%s] ERR:[%d] QUEUE:[%d]",
                txt, err, rd_kafka_outq_len(rk));
    }
#endif

}

#endif


/* duplicates and set as null terminated */
static u_char *
ngx_json_log_str_dup(ngx_pool_t *pool, ngx_str_t *src)
{
    u_char  *dst;

    dst = ngx_pcalloc(pool, src->len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, src->len);
    return dst;
}


//static ngx_str_t *
//ngx_json_log_str_dup_from_buf_len(ngx_pool_t *pool,
//        ngx_str_t *src, size_t len)
//{
//    ngx_str_t           *str;
//
//    str = ngx_pcalloc(pool, sizeof(ngx_str_t));
//    if (str == NULL) {
//        return NULL;
//    }
//
//    str->data = ngx_pcalloc(pool, len);
//    if (str->data == NULL) {
//        return NULL;
//    }
//
//    ngx_memcpy(str->data, src, len);
//    str->len = len;
//
//    return str;
//}


static const char *
ngx_json_log_buf_dup_len(ngx_pool_t *pool, u_char *src, size_t len)
{
    char                *dst;

    dst = ngx_pcalloc(pool, len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src, len);
    return dst;
}


//static u_char *
//ngx_json_log_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len)
//{
//    u_char  *dst;
//    size_t l = ngx_min(src->len, len);
//
//    dst = ngx_pcalloc(pool, l+1);
//    if (dst == NULL) {
//        return NULL;
//    }
//
//    ngx_memcpy(dst, src->data, l);
//    return dst;
//}


//static ngx_int_t
//ngx_json_log_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst)
//{
//    if (! src) {
//        return NGX_ERROR;
//    }
//
//    dst->data = ngx_pcalloc(pool, src->len);
//    if (!dst->data) {
//        return NGX_ERROR;
//    }
//
//    ngx_cpystrn(dst->data, src->data, src->len+1);
//    dst->len = src->len;
//    return NGX_OK;
//}


/* counts the number of items found in str `value` separated
 * by given `separator`.
 */
static ngx_uint_t
ngx_json_log_str_split_count(ngx_str_t *value, u_char separator)
{
    ngx_uint_t ret = 0;
    u_char has = 0;
    size_t i;

    if (!value || !value->data || !value->len)
        return ret;

    for (i=0; i < value->len; ++i) {
        if (has && value->data[i] == separator) {
            ++ret;
            has = 0;
            continue;
        }
        if (!has && !isspace(value->data[i])
                && value->data[i] != separator) {
            has = 1;
        }
    }
    return ret;
}


static size_t
ngx_json_log_hexdump_length(size_t len, size_t blocksz)
{
    size_t                   sz = 0;
    if (!len) {
        return sz;
    }

    size_t b = len / blocksz;
    if (b && (len % blocksz)) {
        b++;
    }

    if (!b) {
        b++;
    }

    sz = (((blocksz * 5) * b) - (2 * b) - (10 * b));

    return sz;
}


static void
ngx_json_log_hexdump(ngx_str_t *src, ngx_str_t *dst)
{
    static const size_t            blocksz = 16;
    size_t                         b, i, l;
    unsigned char                 *pos;
    size_t                         blocks;
    ngx_int_t                      finished = 0;
    static u_char                  hex[16] = "0123456789abcdef";
    u_char                         c = 0;

    if (! dst) {
        return;
    }

    if (! src || ! src->data || ! src->len) {
        return;
    }

    if (! dst || ! dst->data || ! src->len) {
        return;
    }

    blocks = dst->len / blocksz;
    pos = dst->data;
    for (b = 0; b <= blocks ; ++b) {

        for (i = 0; i < blocksz ; ++i) {
            l = (b * blocksz)  + i;
            if (l >= src->len) {
                finished = 1;
                break;
            }

            c = (src->data[l] >> 4);
            ngx_memset(pos++, hex[c], 1);

            c = (src->data[l] - (c * 16));
            ngx_memset(pos++, hex[c], 1);

            ngx_memset(pos++, ' ', 1);
            if (l && (l % (blocksz/2) == 0) && (l % blocksz)) {
                ngx_memset(pos++, ' ', 1);
            }
        }

        for (; i < blocksz; ++i) {
            ngx_snprintf(pos, 3, ".. ");
            pos +=3;
            /* half separator */
            if (i % (blocksz/2) == 0) {
                //printf(" ");
                ngx_memset(pos++, ' ', 1);
            }
        }

        /* printable chars */
        ngx_memset(pos++, '|', 1);
        for (i = 0; i < blocksz ; ++i) {
            l = (b * blocksz)  + i;
            if (l >= src->len) {
                finished = 1;
                break;
            }
            if (isprint(src->data[l])) {
                ngx_snprintf(pos, 1,
                        "%c", src->data[l]);
                pos ++;
            } else {
                ngx_memset(pos++, '.', 1);
            }
        }

        /* right pad */
        ngx_memset(pos, ' ', blocksz - i);
        pos += (blocksz - i);

        /* end printable chars */
        ngx_memset(pos++, '|', 1);
        ngx_memset(pos++, '\n', 1);
        if (finished) {
            break;
        }
    }

}