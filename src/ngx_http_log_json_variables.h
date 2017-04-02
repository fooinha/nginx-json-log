#ifndef __NGX_HTTP_LOG_JSON_VARIABLES_H__
#define __NGX_HTTP_LOG_JSON_VARIABLES_H__

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_variables.h>

struct ngx_http_log_json_req_body_s {
    ngx_http_request_t                       *r;
    ngx_str_t                                 payload;
    ngx_queue_t                               queue;
};

typedef struct ngx_http_log_json_req_body_s
    ngx_http_log_json_req_body_t;

ngx_http_variable_t *ngx_http_log_json_variables(size_t *len);

#endif // __NGX_HTTP_LOG_JSON_VARIABLES_H__

