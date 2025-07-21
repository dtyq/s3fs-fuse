# Multi-stage build for s3fs-fuse
# Stage 1: Build environment
FROM --platform=linux/amd64 public.ecr.aws/ubuntu/ubuntu:20.04 AS builder

# Set labels
LABEL maintainer="s3fs-fuse-builder"
LABEL description="s3fs-fuse compilation environment"
LABEL version="1.0"

# Set non-interactive environment to avoid apt prompts
ENV DEBIAN_FRONTEND=noninteractive

# Set working directory
WORKDIR /usr/src/s3fs-fuse

# Configure Alibaba Cloud mirror for faster package downloads in China
# RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list && \
#     sed -i 's@//.*security.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list

# Update package index and install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    automake \
    autotools-dev \
    libtool \
    autoconf \
    autogen \
    git \
    pkg-config \
    libfuse-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libxml2-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source code to working directory
COPY . .

# Ensure autogen.sh is executable and run it to generate configure script
RUN chmod +x ./autogen.sh && ./autogen.sh

# Configure build with OpenSSL support
RUN ./configure --prefix=/usr --with-openssl

# Compile project with parallel processing
RUN make -j$(nproc)

# Install to system path
RUN make install

# Stage 2: Runtime environment
FROM --platform=linux/amd64 open-registry-cn-beijing.cr.volces.com/vke/tos-launcher:v0.2.0

# Set labels
LABEL maintainer="s3fs-fuse-runtime"
LABEL description="s3fs-fuse - FUSE-based file system backed by Amazon S3"
LABEL version="1.0"

# Install runtime dependencies for s3fs
RUN apt-get update && apt-get install -y \
    libfuse2 \
    libcurl4 \
    libssl1.1 \
    libxml2 \
    && rm -rf /var/lib/apt/lists/*

# Backup existing s3fs if it exists, then copy new s3fs from builder stage
RUN if [ -f /usr/bin/s3fs ]; then \
        cp /usr/bin/s3fs /usr/bin/s3fs.backup.$(date +%Y%m%d_%H%M%S); \
        echo "Existing s3fs backed up to /usr/bin/s3fs.backup.$(date +%Y%m%d_%H%M%S)"; \
    fi

# Copy executable from builder stage
COPY --from=builder /usr/bin/s3fs /usr/bin/s3fs
