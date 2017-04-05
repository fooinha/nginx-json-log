FROM alpine

LABEL maintainer "fooinha@gmail.com"

# Build arguments
ARG GIT_LOCATION=https://github.com/fooinha/nginx-http-log-json.git
ARG GIT_BRANCH=master

RUN echo "http://dl-cdn.alpinelinux.org/alpine/edge/community" >> /etc/apk/repositories
RUN apk update

RUN apk add --no-cache pcre
RUN apk add --no-cache jansson jansson-dev
RUN apk add --no-cache librdkafka librdkafka-dev
RUN apk add --no-cache git
RUN apk add --no-cache mercurial

RUN mkdir -p /build

WORKDIR /build

# Fetches and clones from git location
RUN git clone ${GIT_LOCATION}
RUN cd nginx-http-log-json && git checkout ${GIT_BRANCH}

# Clone from nginx
RUN hg clone http://hg.nginx.org/nginx

WORKDIR /build/nginx
RUN apk add --no-cache gcc
RUN apk add --no-cache alpine-sdk
RUN apk add --no-cache pcre-dev
RUN apk add --no-cache zlib-dev

RUN auto/configure --with-debug --with-cc=/usr/bin/gcc --add-module=/build/nginx-http-log-json
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
