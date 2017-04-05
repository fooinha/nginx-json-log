FROM debian:sid

LABEL maintainer "fooinha@gmail.com"

# Build arguments
ARG DEBIAN_REPO_HOST=httpredir.debian.org
ARG GIT_LOCATION=https://github.com/fooinha/nginx-http-log-json.git
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
RUN cd nginx-http-log-json && git checkout ${GIT_BRANCH}

# Get nginx source code
RUN apt-get source nginx

# Configure , make and install
RUN cd nginx-`apt-cache show nginx | grep "Version:"| cut -d ' ' -f2- | cut -d'-' -f1` && ./configure --with-debug --with-http_perl_module --add-module=/build/nginx-http-log-json --with-http_geoip_module && make install

# Get test framework
RUN git clone https://github.com/openresty/test-nginx.git

# Install test framework and dependencies
RUN cd test-nginx/ && cpanm . && cpanm install JSON

# Run exuberant ctags
RUN cd nginx-http-log-json && ctags -R src/ ../nginx-1.*/src/

# Install files
COPY nginx.conf /usr/local/nginx/conf/nginx.conf
COPY vimrc /etc/vim/vimrc
