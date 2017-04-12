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
#ifndef __NGX_HTTP_JSON_LOG_MODULE_H__
#define __NGX_HTTP_JSON_LOG_MODULE_H__

#define NGX_JSON_LOG_VER    "0.0.5"

#include <ngx_core.h>


struct ngx_http_json_log_loc_conf_s {
    ngx_array_t                               *locations;
    ngx_array_t                               *formats;
};

typedef struct ngx_http_json_log_loc_conf_s   ngx_http_json_log_loc_conf_t;

struct ngx_http_json_log_srv_conf_s {
    ngx_array_t                               *locations;
    ngx_array_t                               *formats;
};

typedef struct ngx_http_json_log_srv_conf_s     ngx_http_json_log_srv_conf_t;

ngx_int_t
ngx_http_json_log_needs_body_filter();

ngx_int_t
ngx_http_json_log_needs_header_filter();

#endif // __NGX_HTTP_JSON_LOG_MODULE_H__

