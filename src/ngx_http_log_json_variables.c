#include "ngx_http_log_json_variables.h"

#include <jansson.h>

#include "ngx_http_log_json_str.h"
#include "ngx_http_log_json_module.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

ngx_int_t ngx_http_log_json_get_variable_req_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_log_json_get_variable_resp_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_log_json_get_variable_req_body(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_log_json_get_variable_req_body_file(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

/* variables list */
static ngx_http_variable_t  ngx_http_log_json_variables_list[] = {
        {   ngx_string("http_log_json_req_headers"), NULL,
            ngx_http_log_json_get_variable_req_headers, 0, 0, 0
        },
        {   ngx_string("http_log_json_req_body"), NULL,
            ngx_http_log_json_get_variable_req_body,
            (uintptr_t) ngx_http_log_json_get_req_body_queue_cb, 0, 0
        },
        {   ngx_string("http_log_json_req_body_file"), NULL,
            ngx_http_log_json_get_variable_req_body_file, 0, 0, 0
        }
};

ngx_http_variable_t *ngx_http_log_json_variables(size_t *len) {

    *len =
      sizeof(ngx_http_log_json_variables_list)
      / sizeof(ngx_http_variable_t);

    return ngx_http_log_json_variables_list;
}

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

ngx_int_t ngx_http_log_json_get_variable_req_body(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_queue_t                                   *q, *values;
    ngx_http_log_json_req_body_t                  *body;
    ngx_queue_t *(*get_values)(ngx_http_request_t *r);
    ngx_str_t                                     base64;

    get_values = (ngx_queue_t *(*)(ngx_http_request_t *r)) data;

    values = get_values(r);

    if (!values) {
        return NGX_ERROR;
    }

    if (ngx_queue_empty(values)) {
        return NGX_OK;
    }

    for (q = ngx_queue_head(values);
            q != ngx_queue_sentinel(values);
            q = ngx_queue_next(q))
    {
        body = ngx_queue_data(q, ngx_http_log_json_req_body_t, queue);

        /* Missing body? */
        if (!body) {
            return NGX_OK;
        }

        if (body->r == r) {

            base64.len = ngx_base64_encoded_length(body->payload.len);
            base64.data = ngx_pcalloc(r->pool, base64.len);
            if (!base64.data) {
                return NGX_ERROR;
            }

            ngx_encode_base64(&base64, &body->payload);

            json_t *object = json_stringn((const char *) base64.data,
                    base64.len);

            v->data = (void *) object;

            ngx_queue_remove(&body->queue);
            break;
        }

    }

    return NGX_OK;
}

ngx_int_t ngx_http_log_json_get_variable_req_body_file(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data) {

    static ngx_str_t name = ngx_string("request_body_file");
    ngx_str_t low;
    ngx_str_t base64;
    ngx_str_t payload;

    const char *tempfilename;
    struct stat fdstat;
    int payload_len = 512;
    int bytes = 0;
    int  fd;

    json_t *object = NULL;

    ngx_http_log_json_str_clone(r->pool, &name, &low);
    ngx_int_t key = ngx_hash_strlow(low.data, name.data, name.len);
    ngx_http_variable_value_t *vv = ngx_http_get_variable(r, &name, key);

    if (vv && vv->data && vv->len) {
        tempfilename = ngx_http_log_json_buf_dup_len(
                r->pool, vv->data, vv->len);

        if (!tempfilename) {
            return NGX_ERROR;
        }

        fd = open((const char *) tempfilename, O_RDONLY, 0600);

        if (fd == -1) {
            return NGX_ERROR;
        }

        if (fstat(fd, &fdstat) == -1 ) {
            return NGX_ERROR;
        }

        if (fdstat.st_size < payload_len) {
            payload_len = fdstat.st_size;
        }

        if (payload_len > 0) {

            payload.data = ngx_pcalloc(r->pool, payload_len);
            if (!payload.data) {
                close(fd);
                return NGX_ERROR;
            }

            bytes = read(fd, payload.data, payload_len-1);
            if (bytes < 1) {
                close(fd);
                return NGX_ERROR;
            }

            payload.len = bytes;
            base64.len = ngx_base64_encoded_length(bytes);
            base64.data = ngx_pcalloc(r->pool, base64.len);
            if (!base64.data) {
                close(fd);
                return NGX_ERROR;
            }

            ngx_encode_base64(&base64, &payload);

            object = json_stringn((const char *)base64.data, base64.len);
        }
        close(fd);
        unlink(tempfilename);
    }

    v->data = (void *) object;

    return NGX_OK;
}
