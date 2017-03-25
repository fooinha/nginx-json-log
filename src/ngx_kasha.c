#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_variables.h>
#include <ngx_log.h>
#include <ngx_rbtree.h>

#include <jansson.h>

#include <ctype.h>
#include <assert.h>

#include "ngx_kasha_kafka.h"
#include "ngx_kasha_str.h"

#define KASHA_VER    "0.0.2"

#define KASHA_FILE_OUT_LEN (sizeof("file:") - 1)
#define KASHA_LOG_HAS_FILE_PREFIX(str) \
    (  ngx_strncmp(str->data, kasha_file_prefix, KASHA_FILE_OUT_LEN) ==  0 )

#define KASHA_KAFKA_OUT_LEN (sizeof("kafka:") - 1)
#define KASHA_LOG_HAS_KAFKA_PREFIX(str) \
    (  ngx_strncmp(str->data, kasha_kafka_prefix, KASHA_KAFKA_OUT_LEN) ==  0 )

/* output prefixes */
static const char *kasha_file_prefix              = "file:";
static const char *kasha_kafka_prefix             = "kafka:";

/* recipe prefixes types and values */
static const char *kasha_true_value               = "true";
static const char *kasha_boolean_prefix           = "b:";
static const char *kasha_string_prefix            = "s:";
static const char *kasha_real_prefix              = "r:";
static const char *kasha_int_prefix               = "i:";
static const char *kasha_null_prefix              = "n:";

static ngx_int_t   kasha_has_kafka_locations      = NGX_CONF_UNSET;

typedef enum {
    NGX_KASHA_SINK_FILE = 0,
    NGX_KASHA_SINK_KAFKA = 1
} ngx_kasha_sink_e;

/* configuration kafka constants */
static const char *conf_client_id_key             = "client.id";
static const char *conf_compression_codec_key     = "compression.codec";
static const char *conf_debug_key                 = "debug";
static const char *conf_log_level_key             = "log_level";
static const char *conf_max_retries_key           = "message.send.max.retries";
static const char *conf_buffer_max_msgs_key       = "queue.buffering.max.messages";
static const char *conf_req_required_acks_key     = "request.required.acks";
static const char *conf_retry_backoff_ms_key      = "retry.backoff.ms";
static ngx_str_t   conf_all_value                 = ngx_string("all");
static ngx_str_t   conf_zero_value                = ngx_string("0");

/* nginx complex variables */
static ngx_str_t   var_kasha_recipe               = ngx_string("kasha_recipe");

/* data structures */
struct ngx_kasha_json_value_s {
    ngx_str_t          label;
    json_t             *node;
};

struct ngx_kasha_ingredient_s {
    json_type          type;
    ngx_str_t          *name;
    ngx_http_compile_complex_value_t *ccv;
};

struct ngx_kasha_variable_s {
    ngx_str_t          recipe;
    ngx_str_t          sink;
    ngx_kasha_sink_e   sink_type;
    uint32_t           ingredients_len;
    ngx_array_t        *ingredients;
    ngx_array_t        *mixed;
};

struct ngx_kasha_loc_kafka_conf_s {
    rd_kafka_topic_t       *rkt;                 /* kafka topic */
    rd_kafka_topic_conf_t  *rktc;                /* kafka topic configuration */
    ngx_str_t              topic;                /* topic name */
    ngx_int_t              partition;            /* kafka partition */
};

/* configuration data structures */
struct ngx_kasha_main_kafka_conf_s {
    rd_kafka_t             *rk;                  /* kafka connection handler */
    rd_kafka_conf_t        *rkc;                 /* kafka configuration */
    ngx_array_t            *brokers;             /* kafka list of brokers */
    size_t                 valid_brokers;        /* number of brokers correctly added */
    ngx_str_t              client_id;            /* kafka client id */
    ngx_str_t              compression;          /* kafka communication compression */
    ngx_uint_t             log_level;            /* kafka client log level */
    ngx_uint_t             max_retries;          /* kafka client max retries */
    ngx_uint_t             buffer_max_messages;  /* max. num of messages to have at send buffer */
    ngx_msec_t             backoff_ms;           /* ms to wait for ... */
};

typedef struct ngx_kasha_json_value_s      ngx_kasha_json_value_t;
typedef struct ngx_kasha_ingredient_s      ngx_kasha_ingredient_t;
typedef struct ngx_kasha_variable_s        ngx_kasha_variable_t;
typedef struct ngx_kasha_main_kafka_conf_s ngx_kasha_main_kafka_conf_t;
typedef struct ngx_kasha_loc_kafka_conf_s  ngx_kasha_loc_kafka_conf_t;

struct ngx_kasha_main_conf_s {
    ngx_kasha_main_kafka_conf_t kafka;
};

struct ngx_kasha_loc_conf_s {
    ngx_str_t                      filename;
    ngx_open_file_t                * file;
    ngx_kasha_loc_kafka_conf_t     kafka;
};

typedef struct ngx_kasha_loc_conf_s        ngx_kasha_loc_conf_t;
typedef struct ngx_kasha_main_conf_s       ngx_kasha_main_conf_t;

/* Configuration callbacks */
static char *        ngx_kasha_recipe_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *        ngx_kasha_create_main_conf(ngx_conf_t *cf);
static void *        ngx_kasha_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t     ngx_kasha_init_worker(ngx_cycle_t *cycle);
static void          ngx_kasha_exit_worker(ngx_cycle_t *cycle);

static ngx_int_t     ngx_kasha_init(ngx_conf_t *cf);

/* json memory functions */
void ngx_kasha_json_free(void *p);
void * ngx_kasha_json_malloc(size_t size);

/* memory pool for json objects */
static ngx_pool_t * current_pool;

/* kasha commands */
static ngx_command_t ngx_kasha_commands[] = {
    /* RECIPE */
    { ngx_string("kasha_recipe"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
        ngx_kasha_recipe_block,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    /* KAFKA */
    {
        ngx_string("kasha_kafka_client_id"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.client_id),
        NULL
    },
    {
        ngx_string("kasha_kafka_brokers"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_1MORE,
        ngx_conf_set_str_array_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.brokers),
        NULL
    },
    {
        ngx_string("kasha_kafka_compression"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.compression),
        NULL
    },
    {
        ngx_string("kasha_kafka_partition"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_kasha_loc_conf_t, kafka.partition),
        NULL
    },
    {
        ngx_string("kasha_kafka_log_level"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.log_level),
        NULL
    },
    {
        ngx_string("kasha_kafka_max_retries"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.max_retries),
        NULL
    },
    {
        ngx_string("kasha_kafka_buffer_max_messages"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.buffer_max_messages),
        NULL
    },
    {
        ngx_string("kasha_kafka_backoff_ms"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_kasha_main_conf_t, kafka.backoff_ms),
        NULL
    },

};

/* kasha config preparation */
static ngx_http_module_t ngx_kasha_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_kasha_init,                        /* postconfiguration */

    ngx_kasha_create_main_conf,            /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_kasha_create_loc_conf,             /* create location configuration */
    NULL                                   /* merge location configuration */
};

/* kasha delivery */
ngx_module_t ngx_kasha_module = {
    NGX_MODULE_V1,
    &ngx_kasha_module_ctx,                 /* module context */
    ngx_kasha_commands,                    /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_kasha_init_worker,                 /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_kasha_exit_worker,                 /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/* Initialized stuff per kasha worker.*/
static ngx_int_t ngx_kasha_init_worker(ngx_cycle_t *cycle) {

    if (kasha_has_kafka_locations == NGX_CONF_UNSET ) {
        return NGX_OK;
    }

    ngx_kasha_main_conf_t  *conf = ngx_http_cycle_get_module_main_conf(cycle, ngx_kasha_module);

    /*TODO - to check if kafka stuff is needed. */

    /* kafka */
    /* - default values - */
    static ngx_str_t  kasha_kafka_compression_default_value          = ngx_string("snappy");
    static ngx_str_t  kasha_kafka_client_id_default_value            = ngx_string("ngx_kasha");
    static ngx_int_t  kasha_kafka_log_level_default_value            = 6;
    static ngx_int_t  kasha_kafka_max_retries_default_value          = 0;
    static ngx_int_t  kasha_kafka_buffer_max_messages_default_value  = 100000;
    static ngx_msec_t kasha_kafka_backoff_ms_default_value           = 10;

    /* create kafka configuration */
    conf->kafka.rkc = kasha_kafka_conf_new(cycle->pool);
    if (! conf->kafka.rkc) {
        return NGX_ERROR;
    }

    /* configure compression */
    if ((void*) conf->kafka.compression.data == NULL) {
        kasha_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                                 conf_compression_codec_key,
                                 &kasha_kafka_compression_default_value);
    } else {
        kasha_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                                 conf_compression_codec_key,
                                 &conf->kafka.compression);
    }
    /* configure max messages, max retries, retry backoff default values if unset*/
    if (conf->kafka.buffer_max_messages == NGX_CONF_UNSET_UINT) {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_buffer_max_msgs_key,
                                 kasha_kafka_buffer_max_messages_default_value);
    } else {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_buffer_max_msgs_key,
                                 conf->kafka.buffer_max_messages);
    }
    if (conf->kafka.max_retries == NGX_CONF_UNSET_UINT) {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_max_retries_key,
                                 kasha_kafka_max_retries_default_value);
    } else {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_max_retries_key,
                                 conf->kafka.max_retries);
    }
    if (conf->kafka.backoff_ms == NGX_CONF_UNSET_MSEC) {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_retry_backoff_ms_key,
                                 kasha_kafka_backoff_ms_default_value);
    } else {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_retry_backoff_ms_key,
                                 conf->kafka.backoff_ms);
    }
    /* configure default client id if not set*/
    if ((void*) conf->kafka.client_id.data == NULL) {
        kasha_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                                 conf_client_id_key,
                                 &kasha_kafka_client_id_default_value);
    } else {
        kasha_kafka_conf_set_str(cycle->pool, conf->kafka.rkc,
                                 conf_client_id_key,
                                 &conf->kafka.client_id);
    }
    /* configure default log level if not set*/
    if (conf->kafka.log_level == NGX_CONF_UNSET_UINT) {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_log_level_key,
                                 kasha_kafka_log_level_default_value);
    } else {
        kasha_kafka_conf_set_int(cycle->pool, conf->kafka.rkc,
                                 conf_log_level_key,
                                 conf->kafka.log_level);
    }

#if (NGX_DEBUG)
    /* configure debug */
    kasha_kafka_conf_set_str(cycle->pool, conf->kafka.rkc, conf_debug_key, &conf_all_value);
#endif

    /* create kafka handler */
    conf->kafka.rk = kasha_kafka_producer_new(cycle->pool, conf->kafka.rkc);
    if (! conf->kafka.rk) {
        return NGX_ERROR;
    }
    /* set client log level */
    if (conf->kafka.log_level == NGX_CONF_UNSET_UINT) {
        rd_kafka_set_log_level(conf->kafka.rk, kasha_kafka_log_level_default_value);
    } else {
        rd_kafka_set_log_level(conf->kafka.rk, conf->kafka.log_level);
    }
    /* configure brokers */
    conf->kafka.valid_brokers = kasha_kafka_add_brokers(cycle->pool, conf->kafka.rk, conf->kafka.brokers);
    if (!conf->kafka.valid_brokers) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                "kasha: failed to configure at least a kafka broker.");
        return NGX_OK;
    }
    return NGX_OK;
}

/* Things that a kasha maker must do before go home. */
void
ngx_kasha_exit_worker(ngx_cycle_t *cycle) {
    //TODO: cleanup kafka stuff
}

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

/* Counts the ingredients based on separator */
static uint32_t
ngx_kasha_count(ngx_str_t *value, u_char separator) {

    if (!value || !value->data || !value->len)
        return 0;

    u_char has = 0;
    uint32_t ret = 0;
    size_t i;
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

/* finds the saved parent for a ingredient name */
static ngx_kasha_json_value_t *
ngx_kasha_find_saved_parent(ngx_pool_t *pool, ngx_array_t *arr_levels, ngx_str_t *name, size_t len) {

    ngx_kasha_json_value_t *rec = arr_levels->elts;
    size_t j;
    for (j=0; j < arr_levels->nelts; j++) {

        ngx_kasha_json_value_t * r = &rec[j];

        if ( r && ngx_strncasecmp(r->label.data, name->data, len) == 0) {
            return r;
        }
    }
    return NULL;
}

/* creates a string from last label from path */
static const char *
ngx_kasha_label_key_dup(ngx_pool_t *pool, ngx_str_t *path, size_t max) {

    if (!path || !path->data || !path->len)
        return NULL;

    u_char *copy = NULL;
    int l = ngx_min(path->len, max);
    int start= l - 1;
    int i;
    for (i = start; i>=0; --i) {
        if (path->data[i] == '.') {
            copy = ngx_pcalloc(pool, start-i+1);
            if (copy == NULL) {
                return NULL;
            }
            ngx_cpystrn(copy, &path->data[i+1], start-i+1);
            return (const char *) copy;
        }
    }

    /* full copy - single label*/
    copy = ngx_pcalloc(pool, l+1);
    if (copy == NULL) {
        return NULL;
    }

    ngx_cpystrn(copy, path->data, l+1);
    return (const char *) copy;
}

/* helper function to find next ingredient level position in path*/
static u_char *
ngx_kasha_has_label_pos(u_char *path) {

    if (!path)
        return NULL;

    u_char * ptr =  (u_char *) strchr((const char *)path, '.');
    return ptr;
}

/* Find the place to put the new ingredient */
static json_t *
ngx_kasha_find_parent(ngx_pool_t *pool, ngx_array_t *arr_levels, json_t *parent, ngx_str_t *path) {

    if (!path || !path->data || !path->len)
        return parent;

    json_t * p = parent;
    ngx_kasha_json_value_t *level = NULL;
    u_char * pos = &path->data[0];

    while((pos = ngx_kasha_has_label_pos(pos)) !=NULL) {
        size_t len = pos-path->data;
        ngx_kasha_json_value_t * saved = ngx_kasha_find_saved_parent(pool, arr_levels, path, len);

        if (! saved) {
            level = ngx_array_push(arr_levels);
            if (!level) {
                ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                        "kasha: Failed allocate new kasha level");
                return NULL;
            }

            level->label.len = len;
            level->label.data = ngx_palloc(pool, len);
            if (!level->label.data) {
                ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                        "Failed allocate new kasha level label");
                return NULL;
            }
            ngx_cpystrn(level->label.data, path->data, len+1);

            /* FIX ME - breaks first level*/
            /*
            if (ngx_kasha_str_clone(pool, path, &level->label) != NGX_OK) {
                ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                        "kasha: Failed allocate new kasha level");
            }
            */

            level->node = json_object();
            /* set node to parent */
            const char *key = ngx_kasha_label_key_dup(pool, &level->label, level->label.len);
            json_object_set(p, key, level->node);
            p = level->node;
        } else {
            p = saved->node;
        }
        ++pos;
    }
    return p;
}

/* adds a typed json node to a parent node */
static void ngx_kasha_add_json_node(ngx_pool_t *pool, json_t *parent, json_type type, const char *key, ngx_str_t *value) {

    if (type == JSON_STRING) {
        /* it's a string type */
        json_object_set(parent,
                key,
                json_stringn((const char *)value->data, value->len));
    } else if (type == JSON_INTEGER) {
        /* it's a integer type*/
        ngx_int_t val_int = ngx_atoi(value->data, value->len);
        json_object_set(parent,
                key,
                json_integer(val_int));
    } else if (type == JSON_TRUE) {
        /* it's a true type*/
        json_object_set(parent,
                key,
                json_true());
    } else if (type == JSON_FALSE) {
        /* it's a false type*/
        json_object_set(parent,
                key,
                json_false());
    } else if (type == JSON_NULL) {
        /* it's a null type*/
        json_object_set(parent,
                key,
                json_null());
    } else if (type == JSON_REAL) {
        /* it's a real type */
        //u_char *nptr  = ngx_palloc(r->pool, value.len + 1);
        u_char *nptr  = ngx_kasha_str_dup(pool, value);
        if (nptr) {
            char *endptr = (char *) nptr + value->len;
            //ngx_cpystrn(nptr, value.data, value.len+1);
            double val_real = strtold((const char *)nptr, &endptr);
            json_object_set(parent,
                    key,
                    json_real(val_real));
        }
    }
}

static ngx_int_t ngx_kasha_write_sink_file(ngx_fd_t fd, const char *txt) {
    size_t to_write = strlen(txt);
    size_t written = ngx_write_fd(fd, (u_char *)txt, strlen(txt));
    if (to_write != written) {
        return NGX_ERROR;
    }
    ngx_write_fd(fd, "\n", 1);
    return NGX_OK;
}

/* main soup recipe routine */
static ngx_int_t ngx_kasha_log_handler(ngx_http_request_t *r) {

    ngx_kasha_variable_t * kv = NULL;
    ngx_kasha_loc_conf_t  *klcf;
    ngx_kasha_main_conf_t  *mcf;
    /* Json structures */
    json_t * obj;

    set_current_mem_pool(r->pool);

    /* Get recipe */
    ngx_http_variable_value_t * recipe =
        ngx_http_get_variable(r, &var_kasha_recipe,
                ngx_hash_key(var_kasha_recipe.data,
                    var_kasha_recipe.len));

    klcf = ngx_http_get_module_loc_conf(r, ngx_kasha_module);

    /* If recipe was not found */
    if (!recipe) {
        return NGX_OK;
    }

    /* Location to eat kasha was not found */
    if (!klcf) {
        return NGX_OK;
    }

    /* If no mixed and prepared ingredients for recipe */
    kv = (ngx_kasha_variable_t *) recipe->data;
    if (!kv || !kv->mixed || !kv->mixed->nelts) {
        return NGX_OK;
    }

    obj = json_object();
    if (obj == NULL) {
        return NGX_ERROR;
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_kasha_module);

    /* don't do anything if no kafka brokers to send */
    if (kv->sink_type == NGX_KASHA_SINK_KAFKA){
        if (!mcf->kafka.valid_brokers) {
            return NGX_OK /* or ERROR ? */;
        }
    }

    /* array to keep levels node values */
    /* no need for hash or list struct */
    /* as it should be very small */
    ngx_array_t * arr_levels =
        ngx_array_create(r->pool,
                kv->ingredients_len,
                sizeof(ngx_kasha_json_value_t));

    ngx_kasha_ingredient_t * cv = kv->mixed->elts;
    /* Put each ingredient value */
    size_t i;
    for (i = 0; i < kv->mixed->nelts; i++) {

        ngx_str_t value;
        uint32_t levels = 0;

        ngx_http_complex_value_t * ccv = (ngx_http_complex_value_t *) cv[i].ccv;
        ngx_int_t err = ngx_http_complex_value(r, ccv, &value);

        /* if complex value compilation failed */
        if (err) {
            ngx_log_error(NGX_LOG_ERR, r->pool->log, 0,
                    "failed get value for [%v]", cv[i].name);
            continue;
        }

        json_t *parent = obj;
        levels = ngx_kasha_count(cv[i].name, '.');
        /* if it is a basic ingredient  */

        if (levels) {
            /* find parent and if need it build it */
            parent = ngx_kasha_find_parent(r->pool, arr_levels, parent, cv[i].name);
        }

        /* orphan level, or can't remember father
         * something went wrong .*/
        if (! parent) {
            ngx_log_error(NGX_LOG_ERR, r->pool->log, 0,
                    "kasha: It's your name Luke?");
            continue;
        }

        /* add value to parent location */
        const char *key = ngx_kasha_label_key_dup(r->pool, cv[i].name, cv[i].name->len);
        ngx_kasha_add_json_node(r->pool, parent, cv[i].type,key, &value);

    } // mixed loop

    /* log who ate kasha in this location  */
    //TODO: write to a buffer
    char * txt = json_dumps(obj, JSON_INDENT(0) | JSON_REAL_PRECISION(2) | JSON_COMPACT);

    /* JSON encoding fails */
    if (!txt) {
        return NGX_OK;
    }

    if (kv->sink_type == NGX_KASHA_SINK_FILE) {
        if ( ngx_kasha_write_sink_file(klcf->file->fd, txt) == NGX_ERROR) {
            set_current_mem_pool(NULL);
            return NGX_ERROR;
        }
    }

    if (kv->sink_type == NGX_KASHA_SINK_KAFKA){
        int err = -1;

        if (klcf->kafka.rkt == NGX_CONF_UNSET_PTR || !klcf->kafka.rkt)  {
            /* create topic conf */
            klcf->kafka.rktc = kasha_kafka_topic_conf_new(r->pool);
            if (! klcf->kafka.rktc) {
                set_current_mem_pool(NULL);
                return NGX_ERROR;
            }

            /* configure topic acks */
            kasha_kafka_topic_conf_set_str(r->pool, klcf->kafka.rktc, conf_req_required_acks_key, &conf_zero_value);

            /* configure and create topic */
            klcf->kafka.rkt = kasha_kafka_topic_new(r->pool, mcf->kafka.rk, klcf->kafka.rktc, &klcf->kafka.topic);
            if (! klcf->kafka.rkt) {
                klcf->kafka.rkt = NGX_CONF_UNSET_PTR;
                set_current_mem_pool(NULL);
                return NGX_ERROR;
            }
        }

        /* FIXME : Reconnect support */
        /* Send/Produce message. */
        if ((err =  rd_kafka_produce(
                        klcf->kafka.rkt,
                        klcf->kafka.partition,
                        RD_KAFKA_MSG_F_COPY,
                        /* Payload and length */
                        (char *) txt, strlen(txt),
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
                    rd_kafka_topic_name(klcf->kafka.rkt),
                    klcf->kafka.partition,
                    errstr);
        } else {
#if (NGX_DEBUG)
            if (mcf) {
                ngx_log_error(NGX_LOG_DEBUG, r->pool->log, 0,
                        "kasha: kafka msg:[%s] ERR:[%d] QUEUE:[%d]",
                        txt, err, rd_kafka_outq_len(mcf->kafka.rk));
            }
#endif
        }

    }
    set_current_mem_pool(NULL);
    return NGX_OK;
}

/* allocated json data in memory pool */
void * ngx_kasha_json_malloc(size_t size) {

    ngx_pool_t *pool=get_current_mem_pool();
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
void ngx_kasha_json_free(void *p) {

    ngx_pool_t *pool=get_current_mem_pool();
    if (!pool) {
        return;
    }
    ngx_pfree(pool, p);
}

static ngx_int_t
ngx_kasha_init(ngx_conf_t *cf) {

    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    /* Register custom json memory functions */
    json_set_alloc_funcs(ngx_kasha_json_malloc, ngx_kasha_json_free);

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_kasha_log_handler;
    return NGX_OK;
}

/* Gets the kasha variable value for this request's location */
static ngx_int_t
ngx_kasha_recipe_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_kasha_variable_t *kv = (ngx_kasha_variable_t *) data;
    v->not_found = 0;
    v->escape = 1;
    v->no_cacheable = 1;
    v->len = 0; /* kv->recipe.len */
    v->data = (void *) kv;

    return NGX_OK;
}

/* Init ingredients workbench */
static ngx_int_t
ngx_kasha_ingredients_init(ngx_kasha_variable_t *kv, ngx_pool_t *pool) {

    if(! kv->ingredients_len) {
        return NGX_OK;
    }

    ngx_array_t * mixed =
        ngx_array_create(pool,
                kv->ingredients_len,
                sizeof(ngx_kasha_ingredient_t));
    if (!mixed) {
        return NGX_ERROR;
    }

    ngx_array_t * ingredients =
        ngx_array_create(pool,
                kv->ingredients_len,
                sizeof(ngx_str_t));
    if (!ingredients) {
        return NGX_ERROR;
    }

    kv->ingredients = ingredients;
    kv->mixed = mixed;
    return NGX_OK;
}

/* Compares two ingredients by name */
static
ngx_int_t ngx_kasha_ingredients_cmp(const void *left, const void *right) {

    const ngx_kasha_ingredient_t * l = left;
    const ngx_kasha_ingredient_t * r = right;

    return ngx_strncasecmp(l->name->data, r->name->data,
            ngx_min(l->name->len, r->name->len));
}

/* Reads recipe from configuration */
static ngx_int_t
ngx_kasha_read_recipe(ngx_conf_t *cf, ngx_kasha_variable_t *kv, ngx_pool_t *pool) {

/* This requires PCRE */
#if (NGX_PCRE)
    u_char errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t rc;
    ngx_str_t *recipe;
    int ovector[1024] = {0};
    char value[1025] = {0};
    ngx_str_t pattern = ngx_string("\\s*([^\\s]+)\\s+([^\\s;]+);");

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

    /* Tries to match recipe to regex and verify format */
    recipe = &kv->recipe;

    /* While we find group lines for the recipe */
    int matched = ngx_regex_exec(rc.regex, recipe, ovector, 1024);
    while (matched > 0) {
    int offset = 0;

        if (matched < 1) {
            ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                    "Failed to configure kasha recipe.");
            return NGX_ERROR;
        }

        ngx_str_t *key_str = ngx_palloc(pool, sizeof(ngx_str_t));
        ngx_str_t *value_str = ngx_palloc(pool, sizeof(ngx_str_t));
        int i;
        for (i=0; i < matched; i++) {
            int ret = pcre_copy_substring((const char *)recipe->data, ovector, matched, i, value, 1024);
            /* i = 0 => all match with isize */
            if (i == 0) {
                offset = ret;
            }
            /* i = 1 => key - ingredient name */
            if (i == 1) {
                key_str->data = ngx_palloc(pool, ret);
                key_str->len = ret;
                ngx_cpystrn(key_str->data, (u_char *)value, ret+1);
            }
            /* i = 2 => value */
            if (i == 2) {
                value_str->data = ngx_palloc(pool, ret);
                value_str->len = ret;
                ngx_cpystrn(value_str->data, (u_char *)value, ret+1);
            }
        }

        ngx_http_complex_value_t           *cv = NULL;
        ngx_http_compile_complex_value_t   ccv;
        cv = ngx_palloc(pool, sizeof(ngx_http_complex_value_t));
        if (cv == NULL) {
            ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                    "#Failed to configure kasha recipe.");
            return NGX_ERROR;
        }
        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
        ccv.cf = cf;
        ccv.value = value_str;
        ccv.complex_value = cv;
        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                    "#Failed to configure kasha recipe.");
            return NGX_ERROR;
        }

        ngx_kasha_ingredient_t *mixed = ngx_array_push(kv->mixed);
        if (mixed == NULL) {
            ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                    "#Failed to configure kasha recipe.");
            return NGX_ERROR;
        }

        mixed->name = key_str;

        /* Check and save type from name prefix */
        /* Default is JSON_STRING type */
        mixed->type = JSON_STRING;
        if (ngx_strncmp(mixed->name->data, kasha_null_prefix, 2) == 0) {
            mixed->type = JSON_NULL;
            mixed->name->data += 2;
            mixed->name->len  -= 2;
        } else if (ngx_strncmp(mixed->name->data, kasha_int_prefix, 2) == 0) {
            mixed->type = JSON_INTEGER;
            mixed->name->data += 2;
            mixed->name->len  -= 2;
        } else if (ngx_strncmp(mixed->name->data, kasha_real_prefix, 2) == 0) {
            mixed->type = JSON_REAL;
            mixed->name->data += 2;
            mixed->name->len  -= 2;
        } else if (ngx_strncmp(mixed->name->data, kasha_string_prefix, 2) == 0) {
            mixed->type = JSON_STRING;
            mixed->name->data += 2;
            mixed->name->len  -= 2;
        } else if (ngx_strncmp(mixed->name->data, kasha_boolean_prefix, 2) == 0) {
            if (ngx_strncmp(value_str->data, kasha_true_value, 4) == 0) {
                mixed->type = JSON_TRUE;
            } else {
                mixed->type = JSON_FALSE;
            }
            mixed->name->data += 2;
            mixed->name->len  -= 2;
        } else {
            mixed->type = JSON_STRING;
        }
        mixed->ccv = (ngx_http_compile_complex_value_t *) cv;

        /* adjust pointers and size for reading the next ingredient*/
        recipe->data+=offset;
        recipe->len-=offset;

        matched = ngx_regex_exec(rc.regex, recipe, ovector, 1024);
    }
#endif

    /* sort ingredients .... this is very import for serialization output alg*/
    ngx_sort(kv->mixed->elts, (size_t) kv->mixed->nelts, sizeof(ngx_kasha_ingredient_t), ngx_kasha_ingredients_cmp);

    return NGX_OK;
}

static void *
ngx_kasha_create_loc_conf(ngx_conf_t *cf) {

    ngx_kasha_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_kasha_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* file */
    conf->filename.len              = 0;
    conf->filename.data             = NULL;
    conf->file                      = NULL;

    /* kafka */
    conf->kafka.rkt                 = NGX_CONF_UNSET_PTR;
    conf->kafka.rktc                = NULL;
    conf->kafka.partition           = RD_KAFKA_PARTITION_UA;

    return conf;
}

static void *
ngx_kasha_create_main_conf(ngx_conf_t *cf) {

    ngx_kasha_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_kasha_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* kafka */
    conf->kafka.rk                  = NULL;
    conf->kafka.rkc                 = NULL;

    /* default values */
    conf->kafka.brokers             = ngx_array_create(cf->pool, 1 , sizeof(ngx_str_t));
    conf->kafka.client_id.data      = NULL;
    conf->kafka.compression.data    = NULL;
    conf->kafka.log_level           = NGX_CONF_UNSET_UINT;
    conf->kafka.max_retries         = NGX_CONF_UNSET_UINT;
    conf->kafka.buffer_max_messages = NGX_CONF_UNSET_UINT;
    conf->kafka.backoff_ms          = NGX_CONF_UNSET_UINT;

    return conf;
}

/* parses output location */
static char *
ngx_kasha_recipe_block_parse_output_location(ngx_conf_t *cf,
        ngx_kasha_loc_conf_t* klcf, ngx_kasha_variable_t *kv, ngx_str_t *log) {

    if (! log) {
        goto failed;
    }

    /* check recipes destination sink type */
    if (KASHA_LOG_HAS_FILE_PREFIX(log)) {
        size_t len = log->len - KASHA_FILE_OUT_LEN + 1;
        if (!len) {
            goto failed;
        }
        /* parse log file and try to open it */
        klcf->filename.data = ngx_palloc(cf->pool, len);
        if (klcf->filename.data == NULL) {
            goto failed;
        }
        ngx_cpystrn(klcf->filename.data, &log->data[KASHA_FILE_OUT_LEN], len);
        klcf->filename.len = len;

        klcf->file = ngx_conf_open_file(cf->cycle, &klcf->filename);
        if (! klcf->file) {
            goto failed;
        }
        kv->sink_type = NGX_KASHA_SINK_FILE;
    }

    if (KASHA_LOG_HAS_KAFKA_PREFIX(log)) {
        size_t len = log->len - KASHA_KAFKA_OUT_LEN + 1;
        if (!len) {
            goto failed;
        }

        klcf->kafka.topic.data = ngx_palloc(cf->pool, len);
        if (klcf->kafka.topic.data == NULL) {
            goto failed;
        }

        ngx_cpystrn(klcf->kafka.topic.data, &log->data[KASHA_KAFKA_OUT_LEN], len);
        klcf->kafka.topic.len = len;

        kv->sink_type = NGX_KASHA_SINK_KAFKA;

        if (kasha_has_kafka_locations == NGX_CONF_UNSET ) {
            kasha_has_kafka_locations = NGX_OK;
        }
    }

    kv->sink = *log;

    ngx_conf_log_error(NGX_LOG_NOTICE, cf, 0,
            "kasha: output location [%v]", log);
    return NGX_CONF_OK;
failed:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "invalid recipe log output \"%v\"", log);
    return NGX_CONF_ERROR;
}

static char *
ngx_kasha_recipe_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_str_t                  *value;
    ngx_http_variable_t        *v;
    ngx_kasha_variable_t       *kv;
    ngx_kasha_loc_conf_t       *klcf = conf;

    value = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! value) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid empty recipe", &value[1]);
        return NGX_CONF_ERROR;
    }

    v = ngx_http_add_variable(cf, &var_kasha_recipe, NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }
    kv = ngx_palloc(cf->pool, sizeof(ngx_kasha_variable_t));
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }
    /* parse output log location */
    if (ngx_kasha_recipe_block_parse_output_location(cf, klcf, kv, &value[1]) == NGX_CONF_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid recipe log location");
        return NGX_CONF_ERROR;
    }

    kv->recipe = value[2];
    kv->ingredients_len = ngx_kasha_count(&kv->recipe, ';');
    if (ngx_kasha_ingredients_init(kv, cf->pool) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid recipe init");
        return NGX_CONF_ERROR;
    }
    if (ngx_kasha_read_recipe(cf, kv, cf->pool) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid recipe read");
        return NGX_CONF_ERROR;
    }
    v->get_handler = ngx_kasha_recipe_variable;
    v->data = (uintptr_t) kv;

    return NGX_CONF_OK;
}
