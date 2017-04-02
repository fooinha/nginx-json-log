#ifndef __NGX_KASHA_STR_H__
#define __NGX_KASHA_STR_H__

#include <ngx_core.h>

const char *
ngx_http_log_json_buf_dup_len(ngx_pool_t *pool, u_char *src, size_t len);

u_char *
ngx_http_log_json_str_dup(ngx_pool_t *pool, ngx_str_t *src);

ngx_str_t *
ngx_http_log_json_str_dup_from_buf_len(ngx_pool_t *pool,
        ngx_str_t *src, size_t len);

u_char *
ngx_http_log_json_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len);

ngx_int_t
ngx_http_log_json_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst);

ngx_uint_t
ngx_http_log_json_str_split_count(ngx_str_t *value, u_char separator);

#endif //__NGX_KASHA_STR_H__
