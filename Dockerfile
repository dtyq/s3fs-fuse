
ARG UBUNTU_VERSION=24.04
ARG BASE_IMAGE=public.ecr.aws/docker/library/ubuntu:${UBUNTU_VERSION}
FROM ${BASE_IMAGE} AS builder

ARG TZ=Asia/Shanghai

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=${TZ}

WORKDIR /usr/src/s3fs-fuse

ARG UBUNTU_VERSION=24.04
ARG UBUNTU_APT_MIRROR=mirrors.aliyun.com
ARG UBUNTU_SECURITY_APT_MIRROR=mirrors.aliyun.com
ARG UBUNTU_PORTS_APT_MIRROR=mirrors.aliyun.com
RUN --mount=type=cache,id=ubuntu-apt-${UBUNTU_VERSION},target=/var/cache/apt,sharing=locked \
    # setup apt mirror follow deb-822 format
    sed -i.bak "s|archive.ubuntu.com|${UBUNTU_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i "s|security.ubuntu.com|${UBUNTU_SECURITY_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i "s|ports.ubuntu.com|${UBUNTU_PORTS_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    # remove docker-clean config to keep apt cache
    mv /etc/apt/apt.conf.d/docker-clean /tmp/docker-clean.bak && \
    # install build dependencies
    apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        automake \
        autotools-dev \
        libtool \
        autoconf \
        git \
        pkg-config \
        libfuse-dev \
        libcurl4-openssl-dev \
        libssl-dev \
        libxml2-dev \
        ca-certificates \
        tzdata \
    && \
    # set timezone
    ln -sf /usr/share/zoneinfo/${TZ} /etc/localtime && \
    echo "${TZ}" > /etc/timezone

COPY . .

RUN chmod +x ./autogen.sh && \
    ./autogen.sh && \
    ./configure --prefix=/usr --with-openssl && \
    make -j$(nproc) && \
    make install DESTDIR=/install

ARG UBUNTU_VERSION=24.04
ARG BASE_IMAGE=public.ecr.aws/docker/library/ubuntu:${UBUNTU_VERSION}
FROM ${BASE_IMAGE}

ARG TZ=Asia/Shanghai
ENV TZ=${TZ}

ARG UBUNTU_VERSION=24.04
ARG UBUNTU_APT_MIRROR=mirrors.aliyun.com
ARG UBUNTU_SECURITY_APT_MIRROR=mirrors.aliyun.com
ARG UBUNTU_PORTS_APT_MIRROR=mirrors.aliyun.com
RUN --mount=type=cache,id=ubuntu-apt-${UBUNTU_VERSION},target=/var/cache/apt,sharing=locked \
    # setup apt mirror follow deb-822 format
    sed -i.bak "s|archive.ubuntu.com|${UBUNTU_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i "s|security.ubuntu.com|${UBUNTU_SECURITY_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i "s|ports.ubuntu.com|${UBUNTU_PORTS_APT_MIRROR}|g" /etc/apt/sources.list.d/ubuntu.sources && \
    # remove docker-clean config to keep apt cache
    mv /etc/apt/apt.conf.d/docker-clean /tmp/docker-clean.bak && \
    # install runtime dependencies
    export DEBIAN_FRONTEND=noninteractive ; \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        libfuse2 \
        libcurl4 \
        libssl3 \
        libxml2 \
        ca-certificates \
        tzdata \
        tini \
    && \
    # set timezone
    ln -sf /usr/share/zoneinfo/${TZ} /etc/localtime && \
    echo "${TZ}" > /etc/timezone && \
    # restore mirrors
    mv /etc/apt/sources.list.d/ubuntu.sources.bak /etc/apt/sources.list.d/ubuntu.sources && \
    # restore docker-clean config
    mv /tmp/docker-clean.bak /etc/apt/apt.conf.d/docker-clean

COPY --from=builder /install/usr/bin/s3fs /usr/bin/s3fs

RUN chmod +x /usr/bin/s3fs

ENTRYPOINT ["/usr/bin/tini", "--"]
