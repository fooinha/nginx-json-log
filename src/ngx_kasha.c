#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_variables.h>
#include <ngx_log.h>
#include <ngx_rbtree.h>

#include <jansson.h>

#include <ctype.h>

#define KASHA_VER    "0.0.1"

static const char *  kasha_file_prefix = "file:";
#define KASHA_FILE_OUT_LEN (sizeof("file:") - 1)
#define KASHA_LOG_HAS_FILE_PREFIX(str) \
   (  ngx_strncmp(str->data, kasha_file_prefix, KASHA_FILE_OUT_LEN) ==  0 )

static ngx_str_t      var_kasha_recipe = ngx_string("kasha_recipe");

typedef enum {
    NGX_KASHA_SINK_FILE = 0
} ngx_kasha_sink_e;

typedef struct {
    ngx_str_t          label;
    json_t             *node;
} ngx_kasha_json_value_t;

typedef struct {
    json_type          type;
    ngx_str_t          *name;
    ngx_http_compile_complex_value_t *ccv;
} ngx_kasha_ingredient_t;

typedef struct {
    ngx_str_t          recipe;
    ngx_str_t          sink;
    ngx_kasha_sink_e   sink_type;
    uint32_t           ingredients_len;
    ngx_array_t        *ingredients;
    ngx_array_t        *mixed;
} ngx_kasha_variable_t;

struct ngx_kasha_loc_conf_s {
    ngx_str_t          filename;
    ngx_open_file_t *  file;
};
typedef struct ngx_kasha_loc_conf_s ngx_kasha_loc_conf_t;

/* Configuration callbacks */
static char * ngx_kasha_recipe_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void * ngx_kasha_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_kasha_init(ngx_conf_t *cf);
static ngx_int_t ngx_kasha_init_worker(ngx_cycle_t *cycle);
static void ngx_kasha_exit_worker(ngx_cycle_t *cycle);

/* json memory functions */
void ngx_kasha_json_free(void *p);
void * ngx_kasha_json_malloc(size_t size);

/* memory pool for json objects */
static ngx_pool_t * json_pool;

/* kasha commands */
static ngx_command_t ngx_kasha_commands[] = {
    { ngx_string("kasha_recipe"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_kasha_recipe_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL
    }
};

/* kasha config preparation */
static ngx_http_module_t ngx_kasha_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_kasha_init,                        /* postconfiguration */

    NULL, // ngx_kasha_create_main_conf,   /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_kasha_create_loc_conf,             /* create location configuration */
    NULL, //ngx_kasha_merge_loc_conf       /* merge location configuration */
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

/* Initialized stuff for kasha maker */
static ngx_int_t ngx_kasha_init_worker(ngx_cycle_t *cycle) {
    return NGX_OK;
}
/* Things that a kasha maker must do before go home*/
static void ngx_kasha_exit_worker(ngx_cycle_t *cycle) {

}

static void
set_json_mem_pool(ngx_pool_t *pool)
{
    json_pool = pool;
}

static ngx_pool_t *
get_json_mem_pool()
{
    return json_pool;
}

/* Counts the ingredients based on separator */
static uint32_t
ngx_kasha_count(ngx_str_t *value, u_char separator) {
    if (!value || !value->data || !value->len)
        return 0;

    u_char has = 0;
    uint32_t ret = 0;
    for (size_t i=0; i < value->len; ++i) {
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
    for (size_t j=0; j < arr_levels->nelts; j++) {

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

    char *copy = NULL;
    int l = ngx_min(path->len, max);
    int start= l - 1;
    for (int i = start; i>=0; --i) {
        if (path->data[i] == '.') {
            copy = ngx_pcalloc(pool, start-i+1);
            if (copy == NULL) {
               return NULL;
            }
            ngx_copy(copy, &path->data[i+1], start-i);
            return copy;
        }
    }
    /* full copy - single label*/
    copy = ngx_pcalloc(pool, l+1);
    if (copy == NULL) {
        return NULL;
    }

    ngx_copy(copy, path->data, l);
    return copy;
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
                        "Failed allocate new kasha level");
                return NULL;
            }

            level->label.len = len;
            level->label.data = ngx_palloc(pool, len);
            if (!level->label.data) {
                ngx_log_error(NGX_LOG_EMERG, pool->log, 0,
                        "Failed allocate new kasha level label");
                return NULL;
            }
            ngx_copy(level->label.data, path->data, len);

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

/* main soup recipe routine */
static ngx_int_t ngx_kasha_log_handler(ngx_http_request_t *r)
{
    ngx_kasha_variable_t * kv = NULL;
    ngx_kasha_loc_conf_t  *klcf;
    /* Json structures */
    json_t * obj;

    set_json_mem_pool(r->pool);

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

    /* array to keep levels node values */
    /* no need for hash or list struct */
    /* as it should be very small */
    ngx_array_t * arr_levels =
        ngx_array_create(r->pool,
                         kv->ingredients_len,
                         sizeof(ngx_kasha_json_value_t));

    ngx_kasha_ingredient_t * cv = kv->mixed->elts;
    for (size_t i = 0; i < kv->mixed->nelts; i++) {

        ngx_str_t value;
        uint32_t levels = 0;

        ngx_http_complex_value_t * ccv = (ngx_http_complex_value_t *) cv[i].ccv;
        ngx_int_t err = ngx_http_complex_value(r, ccv, &value);

        /* if complex value compilation failed */
        if (err) {
            ngx_log_error(NGX_LOG_ERR, r->pool->log, 0,
                    "Failed get value for [%V]", cv[i].name);
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
        if (cv[i].type == JSON_STRING) {
            /* it's a string type */
            json_object_set(parent,
                    key,
                    json_stringn((const char *)value.data, value.len));
        } else if (cv[i].type == JSON_INTEGER) {
            /* it's a integer type*/
            ngx_int_t val_int = ngx_atoi(value.data, value.len);
            json_object_set(parent,
                    key,
                    json_integer(val_int));
        } else if (cv[i].type == JSON_REAL) {
            /* it's a real type */
            u_char *nptr  = ngx_palloc(r->pool, value.len + 1);
            char *endptr = (char *) nptr + value.len;
            ngx_cpystrn(nptr, value.data, value.len+1);
            double val_real = strtold((const char *)nptr, &endptr);
            json_object_set(parent,
                    key,
                    json_real(val_real));
        }

    } // mixed loop

    /* log who ate kasha in this location  */
    //TODO: write to a buffer
    if (kv->sink_type == NGX_KASHA_SINK_FILE){
        char * txt = json_dumps(obj, JSON_INDENT(0) | JSON_REAL_PRECISION(2) | JSON_COMPACT);
        ssize_t n = ngx_write_fd(klcf->file->fd, (u_char *)txt, strlen(txt));
        ngx_write_fd(klcf->file->fd, "\n", 1);
    }

    set_json_mem_pool(NULL);
    return NGX_OK;
}

/* allocated json data in memory pool */
void * ngx_kasha_json_malloc(size_t size) {
    ngx_pool_t *pool=get_json_mem_pool();
    if (!pool) {
        return NULL;
    }
    void *mem =  ngx_palloc(json_pool, size);
    if (!mem) {
        ngx_log_error(NGX_LOG_EMERG, json_pool->log, 0,
                "Failed to allocate memory at json pool");
    }
    return mem;
}
/* free json data memory from pool */
void ngx_kasha_json_free(void *p) {
    ngx_pool_t *pool=get_json_mem_pool();
    if (!pool) {
        return;
    }
    ngx_pfree(pool, p);
}

static ngx_int_t
ngx_kasha_init(ngx_conf_t *cf)
{
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
ngx_kasha_recipe_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_kasha_variable_t *kv = (ngx_kasha_variable_t *) data;

    v->not_found = 0;
    v->escape = 1;
    v->no_cacheable = 1;
    v->len = -1; /* kv->recipe.len */
    v->data = (void *) data;

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

/*TODO: Refactor this obviously.
 *       Messy workbench while preparing ingredients
 *TODO: Log missing errors */
static ngx_int_t
ngx_kasha_read_recipe(ngx_conf_t *cf, ngx_kasha_variable_t *kv, ngx_pool_t *pool) {

    ngx_str_t *recipe;
    u_char *key = NULL;
    u_char *value = NULL;
    u_char has = 0;
    u_char has_space = 0;
    u_char has_key = 0;
    u_char *start;
    u_char *start_value;
    u_char *start_key;
    size_t val_len = 0;
    size_t key_len = 0;

    recipe = &kv->recipe;
    start = &recipe->data[0];
    for (size_t i=0; i < recipe->len; ++i) {
        val_len = 0;
        key_len = 0;
        start_value = 0;
        start_key = 0;
        value = 0;
        key = 0;
        if (has && recipe->data[i] == ';') {

            size_t len = &recipe->data[i] - start;
            /* save ingredient line*/
            ngx_str_t *line = ngx_array_push(kv->ingredients);
            if (line == NULL) {
                ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                        "Failed to configure kasha recipe.");
                return NGX_ERROR;
            }
            line->data = ngx_palloc(pool, len);
            if (! line->data) {
                ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                        "Failed to configure kasha recipe.");
                return NGX_ERROR;
            }
            ngx_memzero(line->data, len) ;
            ngx_cpymem(line->data, start, len);
            line->len = len;

            ngx_kasha_ingredient_t *mixed = ngx_array_push(kv->mixed);
            if (mixed == NULL) {
                ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                        "Failed to configure kasha recipe.");
                return NGX_ERROR;
            }

            has_space = 0;
            has_key = 0;
            start_key = &line->data[0];
            key = 0;
            /* compile lines */
            for (size_t j = 0; j < len; ++j) {
                if (!has_key) {
                    if (!isspace(line->data[j])) {
                        has_key = 1;
                        start_key = &line->data[j];
                    }
                    continue;
                }
                if (isspace(line->data[j])) {
                    key_len = &line->data[j] - start_key;
                    size_t key_total_len = key_len;
                    key = ngx_palloc(pool, len);
                    if (! line->data) {
                        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                                "Failed to configure kasha recipe.");
                        return NGX_ERROR;
                    }
                    ngx_memzero(key, key_len) ;
                    ngx_cpymem(key, start_key, key_len);
                    /* trim spaces */
                    for (size_t k = key_len - 1; k > 0; --k) {
                        if (isspace(key[k])) {
                            key[k] = '\0';
                            continue;
                        }
                        break;
                    }

                    val_len = len-key_len;
                    size_t val_total_len = val_len;
                    value = ngx_palloc(pool, val_len);
                    if (!value) {
                        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                                "Failed to configure kasha recipe.");
                        return NGX_ERROR;
                    }
                    start_value = &line->data[j];

                    /* trim spaces */
                    /* TODO: FIX ME */
                    for (;;) {
                        if (isspace(*start_value)) {
                            ++start_value;
                            --val_len;
                            continue;
                        }
                        break;
                    }
                    ngx_memzero(value, val_len) ;
                    ngx_cpymem(value, start_value, val_len);
                    break;
                }
            }
            if (! key || !value) {
                ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                        "Failed to configure kasha recipe.");
                return NGX_ERROR;
            }
            /* COMPILE */
            {
                ngx_str_t *value_str = ngx_palloc(pool, sizeof(ngx_str_t));
                ngx_str_t *key_str = ngx_palloc(pool, sizeof(ngx_str_t));

                if (!key_str) {
                    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                            "Failed to configure kasha recipe.");
                    return NGX_ERROR;
                }

                key_str->data = key;
                key_str->len = key_len;

                if (!value_str) {
                    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                            "Failed to configure kasha recipe.");
                    return NGX_ERROR;
                }
                value_str->data = value;
                value_str->len = val_len;

                /* Compile complex value expression */

                ngx_http_complex_value_t           *cv = NULL;
                ngx_http_compile_complex_value_t   ccv;
                cv = ngx_palloc(pool, sizeof(ngx_http_complex_value_t));
                if (cv == NULL) {
                    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                            "Failed to configure kasha recipe.");
                    return NGX_ERROR;
                }
                ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
                ccv.cf = cf;
                ccv.value = value_str;
                ccv.complex_value = cv;
                if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
                    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                            "Failed to configure kasha recipe.");
                    return NGX_ERROR;
                }

                mixed->name = key_str;
                /* CHECK TYPE */
                mixed->type = JSON_STRING;
                //TODO: support boolean type
                if (ngx_strncmp(mixed->name->data, "i:", 2) == 0) {
                    mixed->type = JSON_INTEGER;
                    mixed->name->data += 2;
                    mixed->name->len  -= 2;
                } else if (ngx_strncmp(mixed->name->data, "r:", 2) == 0) {
                    mixed->type = JSON_REAL;
                    mixed->name->data += 2;
                    mixed->name->len  -= 2;
                } else if (ngx_strncmp(mixed->name->data, "s:", 2) == 0) {
                    mixed->type = JSON_STRING;
                    mixed->name->data += 2;
                    mixed->name->len  -= 2;
                } else {
                    mixed->type = JSON_STRING;
                }
                mixed->ccv = (ngx_http_compile_complex_value_t *) cv;
            }
            has = 0;
            continue;
        }
        if (!has && !isspace(recipe->data[i]) && recipe->data[i] != ';') {
            start = &recipe->data[i];
            has = 1;
        }
    }
    /* sort ingredients */
    ngx_sort(kv->mixed->elts, (size_t) kv->mixed->nelts, sizeof(ngx_kasha_ingredient_t),
             ngx_kasha_ingredients_cmp);

    return NGX_OK;
}

static void *
ngx_kasha_create_loc_conf(ngx_conf_t *cf)
{
    ngx_kasha_loc_conf_t  *conf;


    conf = ngx_pcalloc(cf->pool, sizeof(ngx_kasha_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* file */
    conf->filename.len = 0;
    conf->filename.data = NULL;
    conf->file = NULL;

    return conf;
}

/* parses output location ... ony supports file: */
static char *
ngx_kasha_recipe_block_parse_output_location(ngx_conf_t *cf,
    ngx_kasha_loc_conf_t* klcf, ngx_kasha_variable_t *kv,
    ngx_str_t *log){

    if (! log ) {
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
    kv->sink = *log;

    ngx_conf_log_error(NGX_LOG_INFO, cf, 0,
            "kasha: output location [%V]", log);
    return NGX_CONF_OK;
failed:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "invalid recipe log output \"%V\"", log);
    return NGX_CONF_ERROR;
}

static char *
ngx_kasha_recipe_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                  *value;
    ngx_str_t                  *log;
    ngx_http_variable_t        *v;
    ngx_kasha_variable_t       *kv;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_kasha_loc_conf_t       *klcf = conf;

    value = cf->args->elts;
    /* this should never happen, but we check it anyway */
    if (! value) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid recipe", &value[1]);
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
                "invalid recipe");
        return NGX_CONF_ERROR;
    }
    if (ngx_kasha_read_recipe(cf, kv, cf->pool) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "invalid recipe");
        return NGX_CONF_ERROR;
    }
    v->get_handler = ngx_kasha_recipe_variable;
    v->data = (uintptr_t) kv;

    return NGX_CONF_OK;
}

