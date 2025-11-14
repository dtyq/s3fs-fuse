#!/bin/sh

# MinIO 初始化脚本
# 用于创建测试存储桶和配置

echo "等待 MinIO 服务启动..."
sleep 5

# 设置 MinIO 服务器别名
echo "配置 MinIO 客户端..."
mc alias set minio http://minio:9000 minioadmin minioadmin

# 检查连接
echo "检查 MinIO 连接..."
mc admin info minio

# 创建测试存储桶
echo "创建测试存储桶..."
mc mb minio/s3fs-test --ignore-existing

# 设置存储桶策略为公共读写（仅测试环境）
echo "设置存储桶策略..."
mc anonymous set public minio/s3fs-test

# 创建测试文件
echo "创建测试文件..."
echo "Hello from MinIO! This is a test file for s3fs-fuse" | mc pipe minio/s3fs-test/welcome.txt

# 列出存储桶内容
echo "存储桶列表:"
mc ls minio/

echo "s3fs-test 内容:"
mc ls minio/s3fs-test/

echo "MinIO 初始化完成！"
echo "访问信息:"
echo "  API 地址: http://localhost:9000"
echo "  Web 控制台: http://localhost:9001"
echo "  用户名: minioadmin"
echo "  密码: minioadmin"
