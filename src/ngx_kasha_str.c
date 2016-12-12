#include <ngx_kasha_str.h>

/* duplicates and set as null terminated */
u_char *
ngx_kasha_str_dup(ngx_pool_t *pool, ngx_str_t *src) {

    u_char  *dst;

    dst = ngx_pcalloc(pool, src->len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, src->len);
    return dst;
}

u_char *
ngx_kasha_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len) {

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
ngx_kasha_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst) {

    if (! src) {
        return NGX_ERROR;
    }

    dst->data = ngx_pcalloc(pool, src->len);
    if (!dst->data) {
        return NGX_ERROR;
    }
    ngx_copy(dst->data, src->data, src->len);
    dst->len = src->len;
    return NGX_OK;
}
