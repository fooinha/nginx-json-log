FROM debian:sid

LABEL maintainer "fooinha@gmail.com"

# Build arguments
ARG DEBIAN_REPO_HOST=httpredir.debian.org
ARG GIT_LOCATION=https://github.com/fooinha/nginx-json-log.git
ARG GIT_BRANCH=master

# Mirror to my location
RUN echo "deb http://${DEBIAN_REPO_HOST}/debian sid main" > /etc/apt/sources.list
RUN echo "deb-src http://${DEBIAN_REPO_HOST}/debian sid main" >> /etc/apt/sources.list

# Update
RUN DEBIAN_FRONTEND=noninteractive apt-get update || true

# Install build dependencies
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --fix-missing \
    apt-utils \
    git-core \
    build-essential \
    devscripts \
    make \
    exuberant-ctags \
    valgrind \
    autoconf \
    automake \
    dh-autoreconf \
    cpanminus \
    libtool \
    zlib1g \
    zlib1g-dev \
    libpcre3 \
    libpcre3-dbg \
    libpcre3-dev \
    libmagic-dev \
    libjansson-dev \
    librdkafka-dev \
    libgeoip1 \
    libgeoip-dev \
    libperl-dev \
    python \
    mercurial \
    vim \
    bind9-host \
    procps \
    telnet \
    tcpflow \
    ngrep \
    wget \
    jq \
    curl

RUN mkdir -p /build

WORKDIR /build

# Fetches and clones from git location
RUN git clone ${GIT_LOCATION}
RUN cd nginx-json-log && git checkout ${GIT_BRANCH}

# Clone from nginx
RUN hg clone http://hg.nginx.org/nginx

WORKDIR /build/nginx
# Configure , make and install
RUN ./auto/configure --add-module=/build/nginx-json-log --with-debug --with-http_perl_module --with-http_geoip_module --with-mail  --with-stream --with-ld-opt="-Wl,-E"

RUN make install

# Get test framework
RUN git clone https://github.com/openresty/test-nginx.git

# Install test framework and dependencies
RUN cd test-nginx/ && cpanm . && cpanm install JSON

# Run exuberant ctags
RUN cd /build/nginx-json-log && ctags -R src/ ../nginx/src/

# Install files
COPY nginx.conf /usr/local/nginx/conf/nginx.conf
COPY vimrc /etc/vim/vimrc
