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
#ifndef __NGX_JSON_LOG_OUTPUT_H__
#define __NGX_JSON_LOG_OUTPUT_H__

#include <ngx_core.h>
#include "ngx_json_log_text.h"
#include "ngx_json_log_kafka.h"

#define NGX_JSON_LOG_FILE_OUT_LEN (sizeof("file:") - 1)
#define NGX_JSON_LOG_HAS_FILE_PREFIX(str)                   \
    (ngx_strncmp(str->data,                                 \
                 "file:",                                   \
                 NGX_JSON_LOG_FILE_OUT_LEN) ==  0 )

#if (NGX_HAVE_LIBRDKAFKA)
#define NGX_JSON_LOG_KAFKA_OUT_LEN (sizeof("kafka:") - 1)
#define NGX_JSON_LOG_HAS_KAFKA_PREFIX(str)                  \
    (ngx_strncmp(str->data,                                 \
                 "kafka:",                                  \
                 NGX_JSON_LOG_KAFKA_OUT_LEN) ==  0 )
#endif

typedef enum {
    NGX_JSON_LOG_SINK_FILE = 0,
#if (NGX_HAVE_LIBRDKAFKA)
    NGX_JSON_LOG_SINK_KAFKA = 1
#endif
} ngx_json_log_sink_e;

struct ngx_json_log_output_location_s {
    ngx_str_t                                 location;
    ngx_json_log_sink_e                       type;
    ngx_json_log_format_t                     format;
    ngx_open_file_t                           *file;
#if (NGX_HAVE_LIBRDKAFKA)
    ngx_json_log_kafka_conf_t                 kafka;
#endif
};

typedef struct ngx_json_log_output_location_s ngx_json_log_output_location_t;

ngx_int_t
ngx_json_log_write_sink_file(ngx_log_t *log,
        ngx_fd_t fd, const char *txt);

#endif // __NGX_JSON_LOG_OUTPUT_H__

