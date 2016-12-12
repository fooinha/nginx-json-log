#ifndef __NGX_KASHA_STR_H__
#define __NGX_KASHA_STR_H__

#include <ngx_core.h>

u_char *
ngx_kasha_str_dup(ngx_pool_t *pool, ngx_str_t *src);

u_char *
ngx_kasha_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len);

ngx_int_t
ngx_kasha_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst);

#endif //__NGX_KASHA_STR_H__
