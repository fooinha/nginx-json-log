#include "ngx_json_log_output.h"

ngx_int_t
ngx_json_log_write_sink_file(ngx_log_t *log,
        ngx_fd_t fd, const char *txt)
{
    size_t written = 0, len = 0;
    if (!txt) {
        return NGX_ERROR;
    }

    len = strlen(txt);
    written = ngx_write_fd(fd, (u_char *)txt, len);
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
