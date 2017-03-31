#ifndef __NGX_HTTP_LOG_JSON_VARIABLES_H__
#define __NGX_HTTP_LOG_JSON_VARIABLES_H__

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_variables.h>
#include <ngx_log.h>
#include <ngx_rbtree.h>
#include <ngx_http_variables.h>

ngx_int_t ngx_http_log_json_get_variable_req_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_log_json_get_variable_resp_headers(ngx_http_request_t *r,
            ngx_http_variable_value_t *v, uintptr_t data);


ngx_http_variable_t *ngx_http_log_json_variables(size_t *len);

#endif // __NGX_HTTP_LOG_JSON_VARIABLES_H__

