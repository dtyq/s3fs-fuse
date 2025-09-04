# Multi-stage build for s3fs-fuse
# Stage 1: Build environment
FROM public.ecr.aws/ubuntu/ubuntu:24.04 AS builder

# Set labels for maintainer info
LABEL maintainer="s3fs-fuse-builder" \
      description="s3fs-fuse compilation environment" \
      version="2.0"

# Set non-interactive environment to avoid apt prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Set working directory
WORKDIR /usr/src/s3fs-fuse

# Install build dependencies in single layer for efficiency
RUN apt-get update && apt-get install -y --no-install-recommends \
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
    && ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && echo "Asia/Shanghai" > /etc/timezone \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Copy source code
COPY . .

# Generate configure script, configure, compile and install
RUN chmod +x ./autogen.sh && \
    ./autogen.sh && \
    ./configure --prefix=/usr --with-openssl && \
    make -j$(nproc) && \
    make install DESTDIR=/install

# Stage 2: Runtime environment
FROM public.ecr.aws/ubuntu/ubuntu:24.04 AS runtime

# Set labels
LABEL maintainer="s3fs-fuse-runtime" \
      description="s3fs-fuse - FUSE-based file system backed by Amazon S3" \
      version="2.0"

# Set non-interactive environment
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    libfuse2 \
    libcurl4 \
    libssl3 \
    libxml2 \
    ca-certificates \
    tzdata \
    tini \
    && ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
    && echo "Asia/Shanghai" > /etc/timezone \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Copy s3fs binary from builder stage
COPY --from=builder /install/usr/bin/s3fs /usr/bin/s3fs

# Set proper permissions
RUN chmod +x /usr/bin/s3fs

# Set tini as the init process
ENTRYPOINT ["/usr/bin/tini", "--"]
