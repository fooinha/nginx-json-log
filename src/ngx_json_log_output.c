#include "ngx_json_log_output.h"

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
