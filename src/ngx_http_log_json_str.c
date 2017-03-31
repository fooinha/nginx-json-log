#include <ngx_http_log_json_str.h>

/* duplicates and set as null terminated */
u_char *
ngx_http_log_json_str_dup(ngx_pool_t *pool, ngx_str_t *src) {

    u_char  *dst;

    dst = ngx_pcalloc(pool, src->len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, src->len);
    return dst;
}

u_char *
ngx_http_log_json_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len) {

    u_char  *dst;
    size_t l = ngx_min(src->len, len);

    dst = ngx_pcalloc(pool, l+1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, l);
    return dst;
}

ngx_int_t
ngx_http_log_json_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst) {

    if (! src) {
        return NGX_ERROR;
    }

    dst->data = ngx_pcalloc(pool, src->len);
    if (!dst->data) {
        return NGX_ERROR;
    }

    ngx_cpystrn(dst->data, src->data, src->len+1);
    dst->len = src->len;
    return NGX_OK;
}

/* counts the number of items found in str `value` separated
 * by given `separator`.
 */
ngx_uint_t
ngx_http_log_json_str_split_count(ngx_str_t *value, u_char separator) {

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
