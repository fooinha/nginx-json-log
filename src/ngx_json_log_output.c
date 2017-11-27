#include "ngx_json_log_output.h"

#include <ngx_config.h>


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

#define NGX_JSON_LOG_SYSLOG_OUT_LEN (sizeof("syslog:") - 1)
#define NGX_JSON_LOG_HAS_SYSLOG_PREFIX(str)                 \
    (ngx_strncmp(str->data,                                 \
                 "syslog:",                                 \
                 NGX_JSON_LOG_SYSLOG_OUT_LEN) ==  0 )



/* Check if format exists by name */
ngx_json_log_format_t *
ngx_json_log_check_format(ngx_array_t *formats,
        ngx_str_t *name)
{
    ngx_json_log_format_t                *format;
    size_t                                i;

    /* Check if format exists by name */
    format = formats->elts;
    for (i = 0; i < formats->nelts; i++) {
        if (ngx_strncmp(name->data, format[i].name.data,
                    format[i].name.len) == 0) {
            return &format[i];
        }
    }

    return NULL;
}

ngx_json_log_output_location_t *
ngx_json_log_output_location_conf(ngx_conf_t *cf,
        ngx_json_log_format_t *format,
        ngx_array_t *locations,
        ngx_str_t *value)
{
    ngx_json_log_output_location_t       *new_location = NULL;
    size_t                                prefix_len;

    if (! NGX_JSON_LOG_HAS_FILE_PREFIX(value)
#if (NGX_HAVE_LIBRDKAFKA)
        && ! NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)
#endif
        && ! NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)
        ) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Invalid prefix [%v] JSON log output location", value);
        return NULL;
    }

    new_location = ngx_array_push(locations);
    if (!new_location) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "Failed to add [%v] JSON log output location", value);
        return NULL;
    }
    ngx_memzero(new_location, sizeof(*new_location));

    prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
    if (NGX_JSON_LOG_HAS_FILE_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_FILE;
        prefix_len = NGX_JSON_LOG_FILE_OUT_LEN;
#if (NGX_HAVE_LIBRDKAFKA)
    } else if (NGX_JSON_LOG_HAS_KAFKA_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_KAFKA;
        prefix_len = NGX_JSON_LOG_KAFKA_OUT_LEN;
#endif
    } else if (NGX_JSON_LOG_HAS_SYSLOG_PREFIX(value)) {
        new_location->type = NGX_JSON_LOG_SINK_SYSLOG;
        prefix_len = NGX_JSON_LOG_SYSLOG_OUT_LEN;
    }

    /* Saves location without prefix. */
    new_location->location       = *value;
    new_location->location.len   -= prefix_len;
    new_location->location.data  += prefix_len;
    new_location->format         = *format;

    /* If sink type is file, then try to open it and save */
    if (new_location->type == NGX_JSON_LOG_SINK_FILE) {
        new_location->file = ngx_conf_open_file(cf->cycle,
                &new_location->location);
    }

    /* If sink type is syslog */
    if (new_location->type == NGX_JSON_LOG_SINK_SYSLOG) {
        new_location->syslog = ngx_pcalloc(cf->pool, sizeof(ngx_syslog_peer_t));
        if (new_location->syslog == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "Failed to allocated JSON log output location to syslog");
            return NULL;
        }

        if (ngx_syslog_process_conf(cf, new_location->syslog) != NGX_CONF_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "Failed to process syslog JSON log output location");
            return NULL;
        }
    }

    return new_location;
}

ngx_int_t
ngx_json_log_write_sink_file(ngx_log_t *log,
        ngx_fd_t fd, const char *txt)
{
    ssize_t                   written = 0;
    ssize_t                   len = 0;
    if (!txt) {
        return NGX_ERROR;
    }

    len = strlen(txt);
    written = ngx_write_fd(fd, (u_char *)txt, (size_t) len);
    if (len && len != written) {
        ngx_log_error(NGX_LOG_EMERG,
                log, 0, "mismatch size: fd=%d len=%d written=%d",
                fd, len, written);
        return NGX_ERROR;
    }
    return NGX_OK;
}

ngx_int_t
ngx_json_log_write_sink_syslog(ngx_log_t *log,
        ngx_pool_t* pool,
        ngx_syslog_peer_t *syslog,
        const char *txt) {

    ssize_t                     n = 0;
    size_t                      size = 0;
    u_char                     *line, *p;
    size_t                      len = 0;

    len += sizeof("<255>Jan 01 00:00:00 ") - 1
        + ngx_cycle->hostname.len + 1
        + syslog->tag.len + 2
        + strlen(txt);

    line = ngx_pnalloc(pool, len);
    if (line == NULL) {
        return NGX_ERROR;
    }

    p = ngx_syslog_add_header(syslog, line);
    p = ngx_snprintf(p, len, "%s", txt);
    size = p - line;
    n = ngx_syslog_send(syslog, line, size);

    if (n < 0) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                "send() to syslog failed");
        return NGX_ERROR;

    } else if ((size_t) n != size) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                "send() to syslog has written only %z of %uz",
                n, size);
        return NGX_ERROR;
    }
    return NGX_OK;
}
