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
#include <ngx_http_variables.h>
#include <ngx_log.h>
#include <ngx_rbtree.h>

#include <jansson.h>
#include "ngx_http_log_json_text.h"
#include "ngx_http_log_json_str.h"
#include "ngx_http_log_json_variables.h"

struct ngx_http_log_json_output_cxt_s {
    /* array to keep levels node values */
    /* no need for hash or list struct */
    /* as it should be very small */
    json_t      *  root;
    ngx_array_t *  items;
};

static const char * TYPE_JSON_STRING     = "JSON_STRING";
static const char * TYPE_JSON_INTEGER    = "JSON_INTEGER";
static const char * TYPE_JSON_REAL       = "JSON_REAL";
static const char * TYPE_JSON_TRUE       = "JSON_TRUE";
static const char * TYPE_JSON_FALSE      = "JSON_FALSE";
static const char * TYPE_JSON_NULL       = "JSON_NULL";

const char * ngx_http_log_json_type_string() {
    return TYPE_JSON_STRING;
}

const char * ngx_http_log_json_type_integer() {
    return TYPE_JSON_INTEGER;
}

const char * ngx_http_log_json_type_real() {
    return TYPE_JSON_REAL;
}

const char * ngx_http_log_json_type_true() {
    return TYPE_JSON_TRUE;
}

const char * ngx_http_log_json_type_false() {
    return TYPE_JSON_FALSE;
}

const char * ngx_http_log_json_type_null() {
    return TYPE_JSON_NULL;
}

typedef struct ngx_http_log_json_output_cxt_s ngx_http_log_json_output_cxt_t;


struct ngx_http_log_json_value_s {
    ngx_str_t                        label;
    json_t                           *node;
};

typedef struct ngx_http_log_json_value_s       ngx_http_log_json_value_t;

/* memory pool for json objects */
static ngx_pool_t * current_pool;

/* Helper functions for allocate/free from memory pool for libjsasson. */
void
set_current_mem_pool(ngx_pool_t *pool) {
    current_pool = pool;
}

static ngx_pool_t *
get_current_mem_pool() {
    return current_pool;
}

ngx_int_t
ngx_http_log_json_output_cxt_new(
        ngx_pool_t *pool,
        ngx_http_log_json_output_cxt_t * ctx,
        ngx_uint_t items_len) {

    set_current_mem_pool(pool);

    ctx->root = json_object();
    if (ctx->root == NULL) {
        return NGX_ERROR;
    }

    ctx->items = ngx_array_create(get_current_mem_pool(), items_len,
            sizeof(ngx_http_log_json_value_t));

    if (!ctx->items) {
        return NGX_ERROR;
    }

    return NGX_OK;
}



/* allocated json data in memory pool */
static void * ngx_http_log_json_malloc(size_t size) {

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
static void ngx_http_log_json_free(void *p) {

    ngx_pool_t *pool = get_current_mem_pool();
    if (!pool) {
        return;
    }
    ngx_pfree(pool, p);
}

void ngx_http_log_json_set_alloc_funcs() {
    json_set_alloc_funcs(ngx_http_log_json_malloc, ngx_http_log_json_free);
}

/* output algorithm */
/* Find the place to put the new item */

/* finds the saved parent for a item name */
static ngx_http_log_json_value_t *
ngx_http_log_json_find_saved_parent(ngx_pool_t *pool,
        ngx_array_t *arr_items, ngx_str_t *name, size_t len) {

    ngx_http_log_json_value_t *rec = arr_items->elts;
    size_t j;

    for (j=0; j < arr_items->nelts; j++) {
        ngx_http_log_json_value_t * r = &rec[j];

        if ( r && ngx_strncasecmp(r->label.data, name->data, len) == 0) {
            return r;
        }
    }
    return NULL;
}

/* creates a string from last label from path */
static const char *
ngx_http_log_json_label_key_dup(ngx_pool_t *pool, ngx_str_t *path, size_t max) {

    u_char *copy = NULL;
    int l = ngx_min(path->len, max);
    int start= l - 1;
    int i;

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
ngx_http_log_json_has_label_pos(u_char *path) {

    u_char * ptr = NULL;

    if (!path)
        return NULL;

    ptr = (u_char *) strchr((const char *)path, '.');
    return ptr;
}


static json_t *
ngx_http_log_json_find_parent(ngx_pool_t *pool, ngx_array_t *arr_items,
        json_t *parent, ngx_str_t *path) {

    json_t * p = parent;
    ngx_http_log_json_value_t *level = NULL;
    u_char * pos = &path->data[0];
    size_t len;
    ngx_http_log_json_value_t * saved;

    if (!path || !path->data || !path->len)
        return parent;


    while((pos = ngx_http_log_json_has_label_pos(pos)) !=NULL) {
        len = pos-path->data;

        saved = ngx_http_log_json_find_saved_parent(
                pool, arr_items, path, len);

        if (saved) {
            p = saved->node;
            ++pos;
            continue;
        }

        level = ngx_array_push(arr_items);
        if (!level) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                    "http_log_json: Failed allocate new http_log_json level");
            return NULL;
        }

        level->label.len = len;
        level->label.data = ngx_palloc(pool, len);
        if (!level->label.data) {
            ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                    "Failed allocate new http_log_json level label");
            return NULL;
        }
        ngx_cpystrn(level->label.data, path->data, len+1);

        /* FIXME - breaks first level*/
        /*
           if (ngx_http_log_json_str_clone(pool, path, &level->label) != NGX_OK) {
           ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
           "http_log_json: Failed allocate new http_log_json level");
           }
           */

        level->node = json_object();
        /* set node to parent */
        const char *key = ngx_http_log_json_label_key_dup(pool,
                &level->label, level->label.len);

        json_object_set(p, key, level->node);
        p = level->node;
        ++pos;
    }
    return p;
}

/* adds a typed json node to a parent node */
static void ngx_http_log_json_add_json_node(
        json_t *base, int is_array, const char *type, const char *key,
        ngx_str_t *value) {

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

    if (type == TYPE_JSON_STRING || type == NULL) {
        /* it's a string type */
        node = json_stringn((const char *)value->data, value->len);

    } else if (type == TYPE_JSON_INTEGER) {
        /* it's a integer type*/
        ngx_int_t val_int = ngx_atoi(value->data, value->len);
        node = json_integer(val_int);
    } else if (type == TYPE_JSON_TRUE) {
        /* it's a true type*/
        node = json_true();
    } else if (type == TYPE_JSON_FALSE) {
        /* it's a false type*/
        node = json_false();
    } else if (type == TYPE_JSON_NULL) {
        /* it's a null type*/
        node = json_null();
    } else if (type == TYPE_JSON_REAL) {
        /* it's a real type */
        //u_char *nptr  = ngx_palloc(r->pool, value.len + 1);
        u_char *nptr  = ngx_http_log_json_str_dup(pool, value);
        if (nptr) {
            char *endptr = (char *) nptr + value->len;
            double val_real = strtold((const char *)nptr, &endptr);
            node = json_real(val_real);
        }
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
ngx_http_log_json_output_add_item(
        ngx_http_request_t *r,
        ngx_http_log_json_output_cxt_t *output_ctx,
        ngx_http_log_json_item_t *item) {

    ngx_str_t                   value;
    uint32_t                    levels = 0;
    json_t                      *parent = output_ctx->root;

    ngx_str_t                   lcname;
    ngx_uint_t                  varkey;
    ngx_http_variable_value_t   *vv;
    const char                  *key  = NULL;
    ngx_http_complex_value_t    *ccv = (ngx_http_complex_value_t *) item->ccv;
    ngx_int_t                   err = ngx_http_complex_value(r, ccv, &value);

    /* if complex value compilation failed */
    if (err) {
        ngx_log_error(NGX_LOG_ERR, current_pool->log, 0,
                "failed get value for [%v]", item->name);
        return NGX_ERROR;
    }

    levels = ngx_http_log_json_str_split_count(item->name, '.');

    if (levels) {
        /* find parent and if need it build it */
        parent = ngx_http_log_json_find_parent(current_pool,
                output_ctx->items, parent, item->name);
    } else {
        /* if it is a basic item  */
        parent = output_ctx->root;
    }

    /* add value to parent location */
    key = ngx_http_log_json_label_key_dup(current_pool,
            item->name, item->name->len);

    if (ngx_http_log_json_is_local_variable(&item->var_name)) {
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

    ngx_http_log_json_add_json_node(parent,
            item->is_array, item->type,
            key, &value);

    return NGX_OK;
}

/* Dumps to text format the JSON for the items for this request. */
char *
ngx_http_log_json_items_dump_text(ngx_http_request_t *r,
        ngx_array_t *items) {

    ngx_http_log_json_output_cxt_t ctx;
    ngx_http_log_json_item_t      *item;
    size_t                        i, dump_len;
    char                          *txt = NULL;
    char                          *dump = NULL;

    set_current_mem_pool(r->pool);

    if (ngx_http_log_json_output_cxt_new(
                current_pool,
                &ctx,
                items->nelts) != NGX_OK) {
        set_current_mem_pool(NULL);
        return txt;
    }

    /* Put each item value */
    item = items->elts;
    for (i = 0; i < items->nelts; i++) {
        ngx_http_log_json_output_add_item(r, &ctx, &item[i]);
    }

    dump = json_dumps(ctx.root,
            JSON_INDENT(0) | JSON_REAL_PRECISION(2) | JSON_COMPACT);

    set_current_mem_pool(NULL);

    if (!dump) {
        return NULL;
    }

    dump_len = strlen(dump);
    txt = ngx_pcalloc(r->pool, dump_len + 2);
    if (!txt) {
        return NULL;
    }

    ngx_memcpy(txt, dump, dump_len);
    txt[dump_len] = '\n';

    return txt;
}
