#!/bin/bash

# 简单的 s3fs 测试脚本

set -e

# 配置
BUCKET_NAME="s3fs-test"
MOUNT_POINT="./mnt/s3fs"
PASSWD_FILE="./s3fs-passwd"
S3FS_BIN="../src/s3fs"
LOG_FILE="./s3fs-debug.log"

# 颜色
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

mount_s3fs() {
    echo -e "${YELLOW}挂载 s3fs...${NC}"
    
    # 创建挂载点
    mkdir -p "$MOUNT_POINT"
    
    # 检查并卸载已存在的挂载
    if mount | grep -q "$MOUNT_POINT" || df | grep -q "$MOUNT_POINT"; then
        echo -e "${YELLOW}卸载现有挂载...${NC}"
        if command -v fusermount >/dev/null 2>&1; then
            fusermount -u "$MOUNT_POINT" 2>/dev/null || umount "$MOUNT_POINT" 2>/dev/null || true
        else
            umount "$MOUNT_POINT" 2>/dev/null || true
        fi
    fi
    
    # 清空挂载点目录（避免 nonempty 错误）
    rm -rf "$MOUNT_POINT"/*
    rm -rf "$MOUNT_POINT"/.[^.]*
    
    # 检查 s3fs 二进制文件
    if [ ! -x "$S3FS_BIN" ]; then
        echo -e "${RED}错误: $S3FS_BIN 不存在或不可执行${NC}"
        echo "请先编译 s3fs: make -C ../src"
        exit 1
    fi
    
    # 清空旧日志
    > "$LOG_FILE"
    echo -e "${YELLOW}日志输出到: $LOG_FILE${NC}"
    
    # 挂载命令
    "$S3FS_BIN" "$BUCKET_NAME" "$MOUNT_POINT" \
        -o dbglevel=debug \
        -o passwd_file="$PASSWD_FILE" \
        -o url=http://localhost:9000 \
        -o use_path_request_style \
        -o mp_umask=022 \
        -o stat_cache_expire=0 \
        -o http_notify_url=http://127.0.0.1:8002/api/v1/files/notifications \
        -o http_notify_exclude_path=.asr_recordings:.asr_states \
        -o local_symlink_path=node_modules:vendor \
        -f >> "$LOG_FILE" 2>&1
    
    echo -e "${GREEN}✓ s3fs 已挂载到 $MOUNT_POINT${NC}"
    echo "测试: ls -la $MOUNT_POINT"
    ls -la "$MOUNT_POINT" || true
}

unmount_s3fs() {
    echo -e "${YELLOW}卸载 s3fs...${NC}"
    
    # 检查挂载状态 - 兼容 macOS 和 Linux
    if mount | grep -q "$MOUNT_POINT" || df | grep -q "$MOUNT_POINT"; then
        # 尝试使用 fusermount（Linux）或 umount（macOS）
        if command -v fusermount >/dev/null 2>&1; then
            fusermount -u "$MOUNT_POINT" 2>/dev/null || umount "$MOUNT_POINT" 2>/dev/null
        else
            umount "$MOUNT_POINT" 2>/dev/null
        fi
        echo -e "${GREEN}✓ s3fs 已卸载${NC}"
    else
        echo -e "${YELLOW}s3fs 未挂载${NC}"
    fi
    
    # 额外清理：停止可能的后台进程
    pkill -f "s3fs.*$BUCKET_NAME.*$MOUNT_POINT" 2>/dev/null || true
}

test_s3fs() {
    mount_s3fs
    
    # 等待挂载完成
    sleep 2
    
    echo -e "${YELLOW}测试文件操作...${NC}"
    echo "Hello MinIO from s3fs!" > "$MOUNT_POINT/test.txt"
    cat "$MOUNT_POINT/test.txt"
    
    echo -e "${GREEN}✓ 测试完成${NC}"
    
    # 等待文件同步
    sleep 1
    unmount_s3fs
}

case "${1:-mount}" in
    "mount")
        mount_s3fs
        ;;
    "unmount")
        unmount_s3fs
        ;;
    "test")
        test_s3fs
        ;;
    *)
        echo "用法: $0 {mount|unmount|test}"
        echo "  mount   - 挂载 s3fs"
        echo "  unmount - 卸载 s3fs" 
        echo "  test    - 快速测试"
        exit 1
        ;;
esac