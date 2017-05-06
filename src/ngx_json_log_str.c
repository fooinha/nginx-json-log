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
#include <ngx_json_log_str.h>

/* duplicates and set as null terminated */
u_char *
ngx_json_log_str_dup(ngx_pool_t *pool, ngx_str_t *src) {

    u_char  *dst;

    dst = ngx_pcalloc(pool, src->len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, src->len);
    return dst;
}

ngx_str_t *
ngx_json_log_str_dup_from_buf_len(ngx_pool_t *pool,
        ngx_str_t *src, size_t len) {

    ngx_str_t *str = ngx_pcalloc(pool, sizeof(ngx_str_t));
    if (str == NULL) {
        return NULL;
    }

    str->data = ngx_pcalloc(pool, len);
    if (str->data == NULL) {
        return NULL;
    }

    ngx_memcpy(str->data, src, len);
    str->len = len;

    return str;
}

const char *
ngx_json_log_buf_dup_len(ngx_pool_t *pool, u_char *src, size_t len) {

    char *dst;

    dst = ngx_pcalloc(pool, len + 1);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src, len);
    return dst;
}

u_char *
ngx_json_log_str_dup_len(ngx_pool_t *pool, ngx_str_t *src, size_t len) {

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
ngx_json_log_str_clone(ngx_pool_t *pool, ngx_str_t *src, ngx_str_t *dst) {

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
ngx_json_log_str_split_count(ngx_str_t *value, u_char separator) {

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

size_t
ngx_json_log_hexdump_length(size_t len, size_t blocksz) {
    size_t                   sz = 0;
    if (!len) {
        return sz;
    }

    size_t b = len / blocksz;
    if (b && (len % blocksz)) {
        b++;
    }

    if (!b) {
        b++;
    }

    sz = (((blocksz * 5) * b) - (2 * b) - (10 * b));

    return sz;
}

void
ngx_json_log_hexdump(ngx_str_t *src, ngx_str_t *dst) {
    static const size_t            blocksz = 16;
    size_t                         b, i, l;
    unsigned char                 *pos;
    size_t                         blocks;
    ngx_int_t                      finished = 0;

    if (! dst) {
        return;
    }

    if (! src || ! src->data || ! src->len) {
        return;
    }

    if (! dst || ! dst->data || ! src->len) {
        return;
    }

    blocks = dst->len / blocksz;
    pos = dst->data;
    for (b = 0; b <= blocks ; ++b) {
/*
        if (b * blocksz < src->len) {
            snprintf((char *)pos, 8,
                    "%08X", (unsigned int) (b * blocksz));

            pos += 8;
            ngx_snprintf(pos, 2, "  ");
            pos += 2;
            //printf("%08x  ", (unsigned int) (b * blocksz));
        } else {
            break;
        }
*/

        for (i = 0; i < blocksz ; ++i) {
            l = (b * blocksz)  + i;
            if (l >= src->len) {
                finished = 1;
                break;
            }
            snprintf((char *)pos, 3,
                    "%x", src->data[l]);
            pos += 2;
            ngx_snprintf(pos, 1, " ");
            pos ++;
            if (l && (l % (blocksz/2) == 0) && (l % blocksz)) {
                ngx_snprintf(pos, 1, " ");
                pos++;
            }
        }

        for (; i < blocksz; ++i) {
            ngx_snprintf(pos, 3, ".. ");
            pos +=3;
            //printf(".. ");
            /* half separator */
            if (i % (blocksz/2) == 0) {
                //printf(" ");
                ngx_snprintf(pos, 1, " ");
                pos ++;
            }
        }

        /* printable chars */
        //printf("|");
        ngx_snprintf(pos, 1, "|");
        pos ++;
        for (i = 0; i < blocksz ; ++i) {
            l = (b * blocksz)  + i;
            if (l >= src->len) {
                finished = 1;
                break;
            }
            if (isprint(src->data[l])) {
                ngx_snprintf(pos, 1,
                        "%c", src->data[l]);
                //printf("%c", src->data[l]);
            } else {
                //printf(".");
                ngx_snprintf(pos, 1, ".");
            }
            pos ++;
        }

        /* right pad */
        for (; i < blocksz; ++i) {
            //printf(" ");
            ngx_snprintf(pos, 1, " ");
            pos++;
        }

        /* end printable chars */
        //printf("|\n");
        ngx_snprintf(pos, 2, "|\n");
        pos += 2;
        if (finished) {
            break;
        }
    }

}
