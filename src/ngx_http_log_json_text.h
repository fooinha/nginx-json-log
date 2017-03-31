#ifndef __NGX_HTTP_LOG_JSON_TEXT_H__
#define __NGX_HTTP_LOG_JSON_TEXT_H__

static const char * TYPE_JSON_STRING     = "JSON_STRING";
static const char * TYPE_JSON_INTEGER    = "JSON_INTEGER";
static const char * TYPE_JSON_REAL       = "JSON_REAL";
static const char * TYPE_JSON_TRUE       = "JSON_TRUE";
static const char * TYPE_JSON_FALSE      = "JSON_FALSE";
static const char * TYPE_JSON_NULL       = "JSON_NULL";

struct ngx_http_log_json_item_s {
    const char                           *type;
    ngx_str_t                            *name;
    ngx_int_t                            is_array;
    ngx_http_compile_complex_value_t     *ccv;
};

typedef struct ngx_http_log_json_item_s  ngx_http_log_json_item_t;

/* Global alloc funcs registration */
void
ngx_http_log_json_set_alloc_funcs();

/* Dumps to text format the JSON for the items for this request. */
char *
ngx_http_log_json_items_dump_text(ngx_http_request_t *r, ngx_array_t *items);

#endif // __NGX_HTTP_LOG_JSON_TEXT_H__

