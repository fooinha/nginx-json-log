FROM alpine

LABEL maintainer "fooinha@gmail.com"

# Build arguments
ARG GIT_LOCATION=https://github.com/fooinha/nginx-json-log.git
ARG GIT_BRANCH=master

RUN echo "http://dl-cdn.alpinelinux.org/alpine/edge/community" >> /etc/apk/repositories
RUN echo "http://dl-cdn.alpinelinux.org/alpine/edge/main" >> /etc/apk/repositories
RUN apk update

RUN apk add busybox-static apk-tools-static
RUN apk.static update
RUN apk.static upgrade --no-self-upgrade --available --simulate
RUN apk.static upgrade --no-self-upgrade --available

RUN apk add --no-cache pcre
RUN apk add --no-cache jansson jansson-dev
RUN apk add --no-cache librdkafka librdkafka-dev
RUN apk add --no-cache git
RUN apk add --no-cache mercurial

RUN mkdir -p /build

WORKDIR /build

# Fetches and clones from git location
RUN git clone ${GIT_LOCATION}
RUN cd nginx-json-log && git checkout ${GIT_BRANCH}

# Clone from nginx
RUN hg clone http://hg.nginx.org/nginx

WORKDIR /build/nginx

RUN apk add --no-cache gcc
RUN apk add --no-cache alpine-sdk
RUN apk add --no-cache pcre-dev
RUN apk add --no-cache zlib-dev
RUN apk add --no-cache perl-dev

RUN auto/configure --with-cc=/usr/bin/gcc --add-module=/build/nginx-json-log --with-debug --with-mail --with-stream --with-ld-opt="-Wl,-E"
RUN cat objs/ngx_modules.c | awk '{ sub("\"ngx_http_json_log_module\",", "");  sub("\"ngx_http_header_filter_module\"", "\"ngx_http_json_log_module\", \"ngx_http_header_filter_module\""); print}' > objs/ngx_modules.c.dat && mv objs/ngx_modules.c.dat objs/ngx_modules.c
RUN make install

COPY nginx.conf /usr/local/nginx/conf/nginx.conf

# Remove build locations
RUN rm -rf /build

# Remove pacakges
RUN apk del git
RUN apk del mercurial
RUN apk del gcc
RUN apk del alpine-sdk
RUN apk del pcre-dev
RUN apk del zlib-dev
RUN apk del jansson-dev
RUN apk del librdkafka-dev
RUN apk del zlib-dev

CMD /usr/local/nginx/sbin/nginx
WORKDIR /usr/local/nginx/conf
