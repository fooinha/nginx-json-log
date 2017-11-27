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

#include <jansson.h>
#include "ngx_json_log_text.h"
#include "ngx_json_log_str.h"
#include "ngx_http_json_log_module.h"
#include "ngx_stream_json_log_module.h"

#include "ngx_http_json_log_filter_module.h"
#include "ngx_http_json_log_variables.h"

struct ngx_json_log_item_s {
    json_type                            type;
    ngx_str_t                            *name;
    ngx_str_t                            var_name;
    ngx_int_t                            is_array;
    ngx_http_compile_complex_value_t     *http_ccv;
#if nginx_version >= 1011002
    ngx_stream_compile_complex_value_t   *stream_ccv;
#endif
};
typedef struct ngx_json_log_item_s  ngx_json_log_item_t;

struct ngx_json_log_output_cxt_s {
    /* array to keep levels node values */
    /* no need for hash or list struct */
    /* as it should be very small */
    json_t        *root;
    ngx_array_t   *items;
};


/* format prefixes types and values */
static const char *ngx_json_log_true_value               = "true";
static const char *ngx_json_log_array_prefix             = "a:";
static const char *ngx_json_log_boolean_prefix           = "b:";
static const char *ngx_json_log_string_prefix            = "s:";
static const char *ngx_json_log_real_prefix              = "r:";
static const char *ngx_json_log_int_prefix               = "i:";
static const char *ngx_json_log_null_prefix              = "n:";

typedef struct ngx_json_log_output_cxt_s ngx_json_log_output_cxt_t;

struct ngx_json_log_value_s {
    ngx_str_t                        label;
    json_t                           *node;
};

typedef struct ngx_json_log_value_s       ngx_json_log_value_t;

/* memory pool for json objects */
static ngx_pool_t * current_pool;


/* Helper functions for allocate/free from memory pool for libjsasson. */
void
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
ngx_json_log_output_cxt_new(
        ngx_pool_t *pool,
        ngx_json_log_output_cxt_t * ctx,
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
static void * ngx_json_log_malloc(size_t size)
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
static void ngx_json_log_free(void *p)
{
    ngx_pool_t *pool = get_current_mem_pool();
    if (!pool) {
        return;
    }
    ngx_pfree(pool, p);
}


void ngx_json_log_set_alloc_funcs()
{
    json_set_alloc_funcs(ngx_json_log_malloc, ngx_json_log_free);
}


/* output algorithm */
/* find the place to put the new item */
/* finds the saved parent for a item name */
static ngx_json_log_value_t *
ngx_json_log_find_saved_parent(ngx_pool_t *pool,
        ngx_array_t *arr_items, ngx_str_t *name, size_t len)
{
    ngx_json_log_value_t *rec = arr_items->elts;
    size_t j;

    for (j=0; j < arr_items->nelts; j++) {
        ngx_json_log_value_t * r = &rec[j];

        if ( r && ngx_strncasecmp(r->label.data, name->data, len) == 0) {
            return r;
        }
    }
    return NULL;
}


/* creates a string from last label from path */
static const char *
ngx_json_log_label_key_dup(ngx_pool_t *pool, ngx_str_t *path, size_t max)
{
    u_char                 *copy;
    int                     l = ngx_min(path->len, max);
    int                     start= l - 1;
    int                     i;

    if (!path || !path->data || !path->len)
        return NULL;

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


/* helper function to find next item level position in path*/
static u_char *
ngx_json_log_has_label_pos(u_char *path)
{
    u_char * ptr = NULL;

    if (!path)
        return NULL;

    ptr = (u_char *) strchr((const char *)path, '.');
    return ptr;
}


static json_t *
ngx_json_log_find_parent(ngx_pool_t *pool, ngx_array_t *arr_items,
        json_t *parent, ngx_str_t *path)
{
    json_t * p = parent;
    ngx_json_log_value_t *level = NULL;
    u_char * pos = &path->data[0];
    size_t len;
    ngx_json_log_value_t * saved;

    if (!path || !path->data || !path->len)
        return parent;

    while((pos = ngx_json_log_has_label_pos(pos)) !=NULL) {
        len = pos-path->data;

        saved = ngx_json_log_find_saved_parent(
                pool, arr_items, path, len);

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
        ngx_cpystrn(level->label.data, path->data, len+1);

        /* FIXME - breaks first level*/
        /*
           if (ngx_json_log_str_clone(pool, path, &level->label) != NGX_OK) {
           ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
           "json_log: Failed allocate new http_json_log level");
           }
           */

        level->node = json_object();
        /* set node to parent */
        const char *key = ngx_json_log_label_key_dup(pool,
                &level->label, level->label.len);

        json_object_set(p, key, level->node);
        p = level->node;
        ++pos;
    }
    return p;
}


/* adds a typed json node to a parent node */
static void ngx_json_log_add_json_node( json_t *base,
        int is_array, json_type type, const char *key,
        ngx_str_t *value)
{
    ngx_pool_t *pool = current_pool;
    json_t * parent = base;
    json_t * parent_array = NULL;
    json_t * node = NULL;

    if (is_array) {
       parent_array = json_object_get(base, key) ;
       if (parent_array == NULL) {
           parent_array = json_array();
           json_object_set(parent, key, parent_array);
       }
       parent = parent_array;
    }

    if (type == JSON_STRING) {
        /* it's a string type */
#if JANSSON_VERSION_HEX >= 0x020700
        node = json_stringn((const char *)value->data, value->len);
#else
        node = json_string((const char *)value->data);
#endif

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
        u_char *nptr  = ngx_json_log_str_dup(pool, value);
        if (nptr) {
            char *endptr = (char *) nptr + value->len;
            double val_real = strtod((const char *)nptr, &endptr);
            node = json_real(val_real);
        }
    } else {
#if JANSSON_VERSION_HEX >= 0x020700
        node = json_stringn((const char *)value->data, value->len);
#else
        node = json_string((const char *)value->data);
#endif
    }

    if (node) {
        if (! is_array) {
            json_object_set(parent, key, node);
        } else {
            json_array_append(parent, node);
        }
    }
}


ngx_int_t
ngx_json_log_output_add_item(
        ngx_json_log_module_type_e type, void *rs,
        ngx_json_log_output_cxt_t *output_ctx,
        ngx_json_log_item_t *item)
{
    ngx_str_t                    value;
    uint32_t                     levels = 0;
    json_t                      *parent = output_ctx->root;

    ngx_str_t                    lcname;
    ngx_uint_t                   varkey;
    ngx_http_variable_value_t   *vv;
    const char                  *key = NULL;

    ngx_http_complex_value_t    *http_ccv = NULL;

#if nginx_version >= 1011002
    ngx_stream_session_t        *s = NULL;
    ngx_stream_complex_value_t  *stream_ccv = NULL;
#endif

    ngx_int_t                    err = 0;
    ngx_http_request_t          *r = NULL;

    if (type == NGX_JSON_LOG_HTTP) {
        http_ccv = (ngx_http_complex_value_t *) item->http_ccv;
        r = (ngx_http_request_t *) rs;
        err = ngx_http_complex_value(r, http_ccv, &value);
#if nginx_version >= 1011002
    } else if (type == NGX_JSON_LOG_STREAM) {
        s = (ngx_stream_session_t *) rs;
        stream_ccv = (ngx_stream_complex_value_t *) item->stream_ccv;
        err = ngx_stream_complex_value(s, stream_ccv, &value);
#endif
    } else {
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

    if (type == NGX_JSON_LOG_HTTP && r) {
        if (ngx_http_json_log_is_local_variable(&item->var_name)) {
            lcname.len = item->var_name.len;
            lcname.data = ngx_pcalloc(r->pool, item->var_name.len);
            varkey = ngx_hash_strlow(lcname.data,
                    item->var_name.data, item->var_name.len);

            vv = ngx_http_get_variable(r, &item->var_name, varkey);

            if (vv && vv->data) {
                /* puts value node under parent */
                json_object_set(parent, key, (json_t *) vv->data);
                return NGX_OK;
            }
        }
    }

    ngx_json_log_add_json_node(parent,
            item->is_array, item->type,
            key, &value);

    return NGX_OK;
}


/* dumps to text format the JSON for the items for this request. */
char *
ngx_json_log_items_dump_text(ngx_json_log_module_type_e type, void *rs,
        ngx_array_t *items)
{
    ngx_json_log_output_cxt_t      ctx;
    ngx_json_log_item_t           *item;
    size_t                         i;
    size_t                         dump_len;
    char                          *txt = NULL;
    char                          *dump = NULL;
    ngx_http_request_t            *r = NULL;
#if nginx_version >= 1011002
    ngx_stream_session_t          *s = NULL;
#endif

    if (type == NGX_JSON_LOG_HTTP) {
        r = (ngx_http_request_t *) rs;
        set_current_mem_pool(r->pool);
#if nginx_version >= 1011002
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
#if JANSSON_VERSION_HEX >= 0x020700
            JSON_REAL_PRECISION(2) |
#endif
            JSON_COMPACT);

    set_current_mem_pool(NULL);

    if (! dump) {
        return NULL;
    }

    dump_len = strlen(dump);
    /* Empty text  */
    if (! dump_len) {
        return NULL;
    }

    if (type == NGX_JSON_LOG_HTTP) {
        txt = ngx_pcalloc(r->pool, dump_len + 2);
    }

#if nginx_version >= 1011002
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
ngx_int_t
ngx_json_log_items_cmp(const void *left, const void *right)
{
    const ngx_json_log_item_t * l = left;
    const ngx_json_log_item_t * r = right;

    return ngx_strncasecmp(l->name->data, r->name->data,
            ngx_min(l->name->len, r->name->len));
}


/* reads and parses, format from configuration */
static ngx_int_t
ngx_json_log_read_format(ngx_conf_t *cf, ngx_json_log_module_type_e type,
        ngx_json_log_format_t *format)
{
/* This requires PCRE */
#if (NGX_PCRE)
    u_char                              errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t                 rc;

    ngx_str_t                           *config;
    ngx_str_t                           spec;

    int                                 ovector[1024] = {0};
    char                                value[1025] = {0};
    ngx_str_t             pattern = ngx_string("\\s*([^\\s]+)\\s+([^\\s;]+);");

    ngx_json_log_item_t                 *item;
    ngx_http_complex_value_t            *http_cv = NULL;
    ngx_http_compile_complex_value_t    http_ccv;

#if nginx_version >= 1011002
    ngx_stream_complex_value_t          *stream_cv = NULL;
    ngx_stream_compile_complex_value_t  stream_ccv;
#endif

    int                                 array_prefix_len = 0;

    int                                 i;
    int                                 offset;
    int                                 matched;
    int                                 ret;

    ngx_str_t                           *key_str;
    ngx_str_t                           *value_str;

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


        item = ngx_array_push(format->items);
        if (item == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                    0, "Failed to configure json_log_format.");
            return NGX_ERROR;
        }
        ngx_memzero(item, sizeof(*item));

        if (type == NGX_JSON_LOG_HTTP) {
            http_cv = ngx_pcalloc(cf->pool, sizeof(ngx_http_complex_value_t));
            if (http_cv == NULL) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                        0, "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            ngx_memzero(&http_ccv, sizeof(ngx_http_compile_complex_value_t));
            http_ccv.cf = cf;
            http_ccv.value = value_str;
            http_ccv.complex_value = http_cv;
            if (ngx_http_compile_complex_value(&http_ccv) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                        0, "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            item->http_ccv = (ngx_http_compile_complex_value_t *) http_cv;
        }

#if nginx_version >= 1011002
        if (type == NGX_JSON_LOG_STREAM) {
            stream_cv = ngx_pcalloc(cf->pool, sizeof(ngx_stream_complex_value_t));
            if (stream_cv == NULL) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                        0, "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            ngx_memzero(&stream_ccv, sizeof(ngx_stream_compile_complex_value_t));
            stream_ccv.cf = cf;
            stream_ccv.value = value_str;
            stream_ccv.complex_value = stream_cv;
            if (ngx_stream_compile_complex_value(&stream_ccv) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, cf->pool->log,
                        0, "Failed to configure json_log_format.");
                return NGX_ERROR;
            }
            item->stream_ccv = (ngx_stream_compile_complex_value_t *) stream_cv;
        }
#endif

        item->name = key_str;

        /* Saves var name */
        if (*value_str->data == '$') {
            item->var_name.data= ngx_pcalloc(cf->pool, value_str->len - 1);
            ngx_memcpy(item->var_name.data,
                    value_str->data + 1, value_str->len - 1);
            item->var_name.len = value_str->len - 1;

            /* enables body filter if needed */
            if ( ! ngx_http_json_log_needs_body_filter()
                    && ngx_http_json_log_is_local_variable(&item->var_name)
                    && ngx_http_json_log_local_variable_needs_body_filter(
                        &item->var_name)) {
                ngx_http_json_log_set_needs_body_filter();
            }

            /* enables header filter if needed */
            if ( ! ngx_http_json_log_needs_header_filter()
                    && ngx_http_json_log_is_local_variable(&item->var_name)
                    && ngx_http_json_log_local_variable_needs_header_filter(
                        &item->var_name)) {
                ngx_http_json_log_set_needs_header_filter();
            }
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
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    ngx_json_log_real_prefix, 2) == 0) {
            item->type = JSON_REAL;
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    ngx_json_log_string_prefix, 2) == 0) {
            item->type = JSON_STRING;
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    ngx_json_log_null_prefix, 2) == 0) {
            item->type = JSON_NULL;
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else if (ngx_strncmp(item->name->data + array_prefix_len,
                    ngx_json_log_boolean_prefix, 2) == 0) {
            if (ngx_strncmp(value_str->data + array_prefix_len,
                        ngx_json_log_true_value, 4) == 0) {
                item->type = JSON_TRUE;
            } else {
                item->type = JSON_FALSE;
            }
            item->name->data += 2 + array_prefix_len;
            item->name->len  -= 2 + array_prefix_len;
        } else {
            item->type = JSON_STRING;
            if (item->is_array) {
                item->name->data += array_prefix_len;
                item->name->len  -= array_prefix_len;
            }
        }

        /* adjust pointers and size for reading the next item*/
        spec.data += offset;
        spec.len -= offset;

        matched = ngx_regex_exec(rc.regex, &spec, ovector, 1024);
    }
#endif

    /* sort items .... this is very import for serialization output alg*/
    ngx_sort(format->items->elts, (size_t) format->items->nelts,
            sizeof(ngx_json_log_item_t), ngx_json_log_items_cmp);

    return NGX_OK;
}


char *
ngx_http_json_log_main_format_block(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf)
{

    ngx_str_t                            *args;
    ngx_json_log_format_t                *new_format;
    ngx_http_json_log_main_conf_t        *mcf = conf;
    ngx_http_compile_complex_value_t     ccv;
    ngx_str_t                            s;
    ngx_uint_t                           items_len;

    args = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                cf, 0, "invalid empty format");
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
    new_format->items = ngx_array_create(cf->pool,
            items_len,
            sizeof(ngx_json_log_item_t)
            );

    if (ngx_json_log_read_format(cf, NGX_JSON_LOG_HTTP, new_format) != NGX_OK) {
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
            new_format->http_filter = ccv.complex_value;
        }
    }
    return NGX_CONF_OK;
}


#if nginx_version >= 1011002
char *
ngx_stream_json_log_main_format_block(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf)
{
    ngx_str_t                            *args;
    ngx_json_log_format_t                *new_format;
    ngx_stream_json_log_main_conf_t      *mcf = conf;
    ngx_stream_compile_complex_value_t   ccv;
    ngx_str_t                            s;
    ngx_uint_t                           items_len;

    args = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! args) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                cf, 0, "invalid empty format");
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
    new_format->items = ngx_array_create(cf->pool,
            items_len, sizeof(ngx_json_log_item_t)
            );

    if (ngx_json_log_read_format(cf,
                NGX_JSON_LOG_STREAM, new_format) != NGX_OK) {
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
#endif
