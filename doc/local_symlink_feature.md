# S3FS Local Symlink Feature - 技术报告

## 1. 问题背景

### 1.1 初始问题

在 s3fs 的 `s3fs_mkdir` 函数中添加 `sleep(120)` 测试代码后，发现：
- 执行 `mkdir` 命令会阻塞 120 秒
- **其他所有操作**（如 `ls`、`cat`、`touch` 等）也被阻塞

这表明在 FUSE 回调函数中的阻塞会影响整个挂载点的其他操作。

### 1.2 实际需求

用户需要在 `s3fs_mkdir` 内部创建软链接，但发现直接调用 `symlink()` 系统调用会导致**死锁**。

---

## 2. 死锁原因分析

### 2.1 Linux 内核 VFS 层的锁机制

当执行 `mkdir` 操作时，Linux 内核的调用链如下：

```
用户空间: mkdir("/mnt/s3fs/testdir")
        ↓
系统调用: sys_mkdir() / sys_mkdirat()
        ↓
VFS层:   do_mkdirat()                    [fs/namei.c]
        ↓
        filename_create()                [fs/namei.c]
        ↓
        start_dirop()                    [fs/namei.c]
        ↓
        __start_dirop()                  [fs/namei.c]
        ↓
┌─────────────────────────────────────────────────────────────┐
│  inode_lock_nested(dir, I_MUTEX_PARENT);  ← 获取父目录锁！   │
└─────────────────────────────────────────────────────────────┘
        ↓
        vfs_mkdir()                      [fs/namei.c]
        ↓
        dir->i_op->mkdir()               (调用 FUSE 的 fuse_mkdir)
        ↓
FUSE层:  fuse_mkdir()                    [fs/fuse/dir.c]
        ↓
        fuse_simple_request()            → 发送请求到用户空间并等待
        ↓
用户空间: s3fs_mkdir()                    ← 用户代码在这里执行
        ↓
        sleep(120) / symlink()           ← 阻塞或尝试再次获取锁
        ↓
返回后:  end_dirop()                     [fs/namei.c]
        ↓
        inode_unlock()                   ← 释放父目录锁
```

### 2.2 关键代码位置（Linux 内核）

**锁的获取：`__start_dirop` 函数**（fs/namei.c:2851）

```c
static struct dentry *__start_dirop(struct dentry *parent, struct qstr *name,
                                    unsigned int lookup_flags,
                                    unsigned int state)
{
    struct dentry *dentry;
    struct inode *dir = d_inode(parent);

    // 获取父目录的 i_rwsem 写锁
    inode_lock_nested(dir, I_MUTEX_PARENT);
    
    dentry = lookup_one_qstr_excl(name, parent, lookup_flags);
    if (IS_ERR(dentry))
        inode_unlock(dir);
    return dentry;
}
```

**锁的释放：`end_dirop` 函数**（fs/namei.c:2885）

```c
void end_dirop(struct dentry *de)
{
    if (!IS_ERR(de)) {
        inode_unlock(de->d_parent->d_inode);  // 释放父目录锁
        dput(de);
    }
}
```

### 2.3 死锁形成过程

```
Thread A: mkdir /mnt/s3fs/dir1
    ↓
    获取 /mnt/s3fs 目录的 i_rwsem 写锁
    ↓
    调用 s3fs_mkdir()
    ↓
    在 s3fs_mkdir 中调用 symlink("/mnt/s3fs/dir1/link", ...)
    ↓
    symlink() 进入内核 → do_symlinkat()
    ↓
    尝试获取 /mnt/s3fs/dir1 父目录（即 /mnt/s3fs）的锁
    ↓
    💀 死锁！锁已被 Thread A 自己持有
```

### 2.4 FUSE 的阻塞等待机制

在 FUSE 内核模块中（fs/fuse/dev.c:544）：

```c
static void request_wait_answer(struct fuse_req *req)
{
    // 阻塞等待用户空间响应
    err = wait_event_interruptible(req->waitq,
                    test_bit(FR_FINISHED, &req->flags));
    // ...
}
```

这意味着内核线程会一直等待，直到用户空间（s3fs）处理完请求并返回。在此期间，父目录的 `i_rwsem` 锁一直被持有。

---

## 3. 解决方案

### 3.1 核心思路

**绕过 VFS 层**：不通过系统调用创建软链接，而是直接使用 S3 API 创建软链接对象。

### 3.2 实现方案

#### 3.2.1 创建内部函数 `create_symlink_object`

提取 `s3fs_symlink` 的核心逻辑，创建一个可以在 FUSE 回调中安全调用的内部函数：

```cpp
// 内部函数：直接通过 S3 API 创建软链接（绕过 VFS）
static int create_symlink_object(const char* target, const char* linkpath, 
                                   uid_t uid, gid_t gid, std::string& out_target)
{
    // 直接操作 S3，不触发 VFS 层的锁
    headers_t headers;
    headers["x-amz-meta-mode"] = std::to_string(S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO);
    // ... 设置其他 metadata
    
    // 使用 FdEntity 写入软链接内容到 S3
    AutoFdEntity autoent;
    FdEntity* ent = autoent.Open(linkpath, &headers, ...);
    ent->Write(autoent.GetPseudoFd(), target, 0, strlen(target));
    ent->Flush(autoent.GetPseudoFd(), true);
    
    return 0;
}
```

#### 3.2.2 面向对象封装

创建 `LocalSymlinkConfig` 和 `LocalSymlinkManager` 类，放在 `src/extension/` 目录下：

**local_symlink.h**
```cpp
struct LocalSymlinkConfig {
    std::set<std::string> symlink_paths;  // 需要软链接的目录名
    std::string base_path;                 // 本地基础目录
    
    void parse_symlink_paths(const char* paths_str);
    bool is_enabled() const;
};

class LocalSymlinkManager {
public:
    bool should_create_local_symlink(const char* path) const;
    int prepare_local_directory(const char* path, mode_t mode, 
                                uid_t uid, gid_t gid, std::string& local_target);
    static LocalSymlinkManager& instance();
};
```

---

## 4. 功能实现：Local Symlink Path

### 4.1 功能描述

将特定目录（如 `node_modules`、`vendor`）自动创建为软链接，指向本地文件系统，而不是存储在 S3 上。

**优势**：
- 避免大量小文件存储在 S3 上造成的性能问题
- 本地磁盘 I/O 速度，无网络延迟
- 适合 npm/composer 等包管理器的依赖目录

### 4.2 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `local_symlink_path=` | 需要软链接的目录名（冒号分隔） | 空（不启用） |
| `local_symlink_base=` | 本地目录的基础路径 | `/tmp/s3fs-local-mounts` |

### 4.3 使用示例

```bash
# 挂载时配置
s3fs mybucket /mnt/s3fs \
    -o local_symlink_path=node_modules:vendor \
    -o local_symlink_base=/data/local-cache \
    -o passwd_file=/etc/s3fs-passwd
```

### 4.4 行为说明

```
配置: local_symlink_path=node_modules:vendor
      local_symlink_base=/tmp/s3fs-local-mounts

mkdir /mnt/s3fs/project/src          → 正常创建 S3 目录
mkdir /mnt/s3fs/project/node_modules → 创建软链接
mkdir /mnt/s3fs/app/vendor           → 创建软链接
```

软链接映射：
```
/mnt/s3fs/project/node_modules → /tmp/s3fs-local-mounts/project/node_modules
/mnt/s3fs/app/vendor           → /tmp/s3fs-local-mounts/app/vendor
```

### 4.5 工作流程

```
用户执行: mkdir /mnt/s3fs/project/node_modules
                    ↓
s3fs_mkdir() 被调用
                    ↓
检查目录名 "node_modules" 是否在 local_symlink_path 中
                    ↓ 匹配
Step 1: 创建本地目录
        mkdir -p /tmp/s3fs-local-mounts/project/node_modules
                    ↓
Step 2: 在 S3 上创建软链接对象
        create_symlink_object(
            target = "/tmp/s3fs-local-mounts/project/node_modules",
            linkpath = "/mnt/s3fs/project/node_modules"
        )
                    ↓
完成: /mnt/s3fs/project/node_modules → /tmp/s3fs-local-mounts/project/node_modules
```

---

## 5. 文件结构

### 5.1 新增文件

```
src/extension/
├── local_symlink.h      # LocalSymlinkConfig 和 LocalSymlinkManager 定义
└── local_symlink.cpp    # 实现代码
```

### 5.2 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/s3fs.cpp` | 添加 `create_symlink_object`、修改 `s3fs_mkdir`、添加选项解析 |
| `src/Makefile.am` | 添加 `extension/local_symlink.cpp` |

---

## 6. 关键代码

### 6.1 s3fs_mkdir 核心逻辑

```cpp
static int s3fs_mkdir(const char* _path, mode_t mode)
{
    // ... 权限检查 ...

    // 检查是否需要创建本地软链接
    if(is_local_symlink && LocalSymlinkManager::instance().should_create_local_symlink(path)){
        // 创建本地目录
        std::string local_target;
        result = LocalSymlinkManager::instance().prepare_local_directory(
            path, mode, pcxt->uid, pcxt->gid, local_target);
        
        // 在 S3 上创建软链接（绕过 VFS，不会死锁）
        std::string out_target;
        result = create_symlink_object(local_target.c_str(), path, 
                                         pcxt->uid, pcxt->gid, out_target);
        
        // 更新缓存
        StatCache::getStatCacheData()->AddSymlink(path, out_target);
    } else {
        // 正常创建 S3 目录
        result = create_directory_object(path, mode, ...);
    }
    
    return result;
}
```

### 6.2 选项解析

```cpp
// my_fuse_opt_proc 中的选项解析
else if(is_prefix(arg, "local_symlink_path=")){
    const char* paths_str = strchr(arg, '=') + sizeof(char);
    local_symlink_config.parse_symlink_paths(paths_str);
    is_local_symlink = local_symlink_config.is_enabled();
    return 0;
}
else if(is_prefix(arg, "local_symlink_base=")){
    local_symlink_config.base_path = strchr(arg, '=') + sizeof(char);
    return 0;
}
```

---

## 7. 测试验证

### 7.1 测试命令

```bash
# 挂载
./test-s3fs.sh mount

# 测试 npm install（会创建 node_modules）
cd /mnt/s3fs
npm init -y
npm install next

# 验证软链接
ls -la
# 输出: node_modules -> /tmp/s3fs-local-mounts/node_modules
```

### 7.2 测试结果

```
$ ls -la /mnt/s3fs/
lrwxrwxrwx  1 user  staff  35 Dec  3 14:38 node_modules -> /tmp/s3fs-local-mounts/node_modules
-rw-r--r--  1 user  staff  287 Dec  3 14:38 package.json
-rw-r--r--  1 user  staff  29679 Dec  3 14:39 package-lock.json
```

---

## 8. 总结

### 8.1 问题根因

Linux 内核 VFS 层的 `i_rwsem` 锁机制导致在 FUSE 回调函数中不能通过系统调用对同一目录树进行操作，否则会造成死锁。

### 8.2 解决方案

创建 `create_symlink_object` 函数，直接通过 S3 API 创建软链接对象，绕过 VFS 层，避免死锁。

### 8.3 新增功能

实现 `local_symlink_path` 功能，允许将特定目录（如 `node_modules`、`vendor`）自动软链接到本地文件系统，提升大量小文件场景下的性能。

---

## 附录：相关内核代码位置

| 功能 | 文件 | 函数 |
|------|------|------|
| mkdir 入口 | fs/namei.c | `do_mkdirat()` |
| 获取目录锁 | fs/namei.c | `__start_dirop()` |
| 释放目录锁 | fs/namei.c | `end_dirop()` |
| FUSE mkdir | fs/fuse/dir.c | `fuse_mkdir()` |
| FUSE 请求等待 | fs/fuse/dev.c | `request_wait_answer()` |
| inode 锁定义 | include/linux/fs.h | `struct inode.i_rwsem` |

---

*文档版本: 1.0*  
*日期: 2024-12-03*  
*作者: s3fs-fuse contributor*

