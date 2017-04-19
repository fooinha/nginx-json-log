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
#ifndef __NGX_HTTP_JSON_LOG_VARIABLES_H__
#define __NGX_HTTP_JSON_LOG_VARIABLES_H__

#include <ngx_config.h>
#include <ngx_http_variables.h>

void
ngx_http_json_log_register_variables(ngx_conf_t *cf);

void
ngx_http_json_log_set_variable_resp_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

void
ngx_http_json_log_set_variable_req_body(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t
ngx_http_json_log_is_local_variable(ngx_str_t *name);

ngx_int_t
ngx_http_json_log_local_variable_needs_body_filter(ngx_str_t *name);

ngx_int_t
ngx_http_json_log_local_variable_needs_header_filter(ngx_str_t *name);


#endif // __NGX_HTTP_JSON_LOG_VARIABLES_H__

