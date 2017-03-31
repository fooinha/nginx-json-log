#include "ngx_http_log_json_variables.h"

#include <jansson.h>
#include "ngx_http_log_json_str.h"

/* variables list */
static ngx_http_variable_t  ngx_http_log_json_variables_list[] = {
        {   ngx_string("http_log_json_req_headers"), NULL,
            ngx_http_log_json_get_variable_req_headers, 0, 0, 0
        },
/*        {   ngx_string("http_log_json_resp_headers"), NULL,
            ngx_http_log_json_get_variable_resp_headers, 0, 0, 0
        }
        */
};

ngx_http_variable_t *ngx_http_log_json_variables(size_t *len) {

    *len =
      sizeof(ngx_http_log_json_variables_list)
      / sizeof(ngx_http_variable_t);

    return ngx_http_log_json_variables_list;
}

static ngx_str_t response_headers_vars[] = {
    ngx_string("sent_http_content_type"),
    ngx_string("sent_http_content_length"),
    ngx_string("sent_http_location"),
    ngx_string("sent_http_last_modified"),
    ngx_string("sent_http_connection"),
    ngx_string("sent_http_keep_alive"),
    ngx_string("sent_http_transfer_encoding"),
    ngx_string("sent_http_cache_control"),
    ngx_null_string
};

ngx_int_t ngx_http_log_json_get_variable_req_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_uint_t i;
    ngx_list_part_t *part = &r->headers_in.headers.part;
    ngx_table_elt_t *header = part->elts;
    u_char *key, *value;

    json_t *object = json_object();

    for (i = 0; i < part->nelts ; ++i) {
        key = ngx_http_log_json_str_dup(r->pool, &header[i].key);
        value = ngx_http_log_json_str_dup(r->pool, &header[i].value);

        json_object_set(object, (const char *) key,
                json_string((const char *) value));
    }
    v->data = (void *) object;

    return NGX_OK;
}

/* NOT WORKING */
ngx_int_t ngx_http_log_json_get_variable_resp_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data) {

    u_char                    *key;
    ngx_http_variable_value_t *vv;
    ngx_uint_t                varkey;
    json_t                    *obj = json_object();
    ngx_uint_t                 i;
    ngx_str_t                 *header_var;

    for (i = 0; response_headers_vars[i].data ; ++i) {


        header_var = &response_headers_vars[i];


        /* get var value by key */
        varkey = ngx_hash_strlow(
                header_var->data,
                header_var->data,
                header_var->len
                );

        printf("DOOOOOOOOO\n");

        vv = ngx_http_get_variable(r,
                &response_headers_vars[i],
                varkey);

        key = ngx_http_log_json_str_dup(r->pool, &response_headers_vars[i]);

        if (vv && vv->data) {
            /* puts value node under parent */
            json_object_set(obj, (const char *) key, 
                    json_stringn((const char *) vv->data, vv->len));
        }
    }

    v->data = (void *) obj;

    return NGX_OK;
}
