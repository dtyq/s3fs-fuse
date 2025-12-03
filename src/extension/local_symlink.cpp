#include "local_symlink.h"
#include "s3fs_logger.h"
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

//-------------------------------------------------------------------
// LocalSymlinkConfig implementation
//-------------------------------------------------------------------
LocalSymlinkConfig::LocalSymlinkConfig()
    : base_path("/tmp/s3fs-local-mounts")
{
}

bool LocalSymlinkConfig::is_valid() const
{
    return !base_path.empty();
}

bool LocalSymlinkConfig::is_enabled() const
{
    return !symlink_paths.empty();
}

void LocalSymlinkConfig::parse_symlink_paths(const char* paths_str)
{
    if (!paths_str || *paths_str == '\0') {
        return;
    }

    symlink_paths.clear();
    std::string paths(paths_str);
    size_t start = 0;
    size_t end = 0;

    // Split by colon
    while (end != std::string::npos) {
        end = paths.find(':', start);
        std::string path_name = paths.substr(start, (end == std::string::npos) ? std::string::npos : end - start);

        // Skip empty paths
        if (!path_name.empty()) {
            symlink_paths.insert(path_name);
            S3FS_PRN_INFO("Added local symlink path pattern: %s", path_name.c_str());
        }

        start = end + 1;
    }

    S3FS_PRN_INFO("Total local symlink path patterns: %zu", symlink_paths.size());
}

//-------------------------------------------------------------------
// LocalSymlinkManager implementation
//-------------------------------------------------------------------
LocalSymlinkManager::LocalSymlinkManager()
    : initialized(false)
{
}

LocalSymlinkManager::~LocalSymlinkManager()
{
}

bool LocalSymlinkManager::initialize(const LocalSymlinkConfig& cfg)
{
    if (initialized) {
        S3FS_PRN_WARN("LocalSymlinkManager already initialized");
        return false;
    }

    if (!cfg.is_valid()) {
        S3FS_PRN_ERR("Invalid local symlink configuration");
        return false;
    }

    config = cfg;
    initialized = true;

    S3FS_PRN_INFO("LocalSymlinkManager initialized: base_path=%s, patterns=%zu",
                  config.base_path.c_str(), config.symlink_paths.size());

    return true;
}

std::string LocalSymlinkManager::get_basename(const char* path)
{
    std::string spath(path);
    // Remove trailing slash if present
    while (!spath.empty() && spath.back() == '/') {
        spath.pop_back();
    }
    size_t pos = spath.rfind('/');
    if (pos != std::string::npos) {
        return spath.substr(pos + 1);
    }
    return spath;
}

bool LocalSymlinkManager::should_create_local_symlink(const char* path) const
{
    if (!initialized || config.symlink_paths.empty()) {
        return false;
    }
    std::string dirname = get_basename(path);
    return config.symlink_paths.find(dirname) != config.symlink_paths.end();
}

std::string LocalSymlinkManager::get_local_target_path(const char* path) const
{
    return config.base_path + path;
}

int LocalSymlinkManager::create_local_directory(const char* local_path, mode_t mode, uid_t uid, gid_t gid)
{
    // Create the directory
    if (0 != ::mkdir(local_path, mode)) {
        if (errno != EEXIST) {
            S3FS_PRN_ERR("Failed to create local directory: %s (errno=%d)", local_path, errno);
            return -errno;
        }
    }

    // Set ownership
    if (0 != ::chown(local_path, uid, gid)) {
        S3FS_PRN_WARN("Failed to chown local directory: %s (errno=%d)", local_path, errno);
        // Don't fail on chown error, just warn
    }

    return 0;
}

int LocalSymlinkManager::prepare_local_directory(const char* path, mode_t mode, uid_t uid, gid_t gid, std::string& local_target)
{
    if (!initialized) {
        S3FS_PRN_ERR("LocalSymlinkManager not initialized");
        return -EINVAL;
    }

    // Build local target path
    local_target = get_local_target_path(path);

    // Create parent directories if needed
    std::string local_parent = local_target.substr(0, local_target.rfind('/'));
    if (!local_parent.empty()) {
        std::string mkdir_cmd = "mkdir -p \"" + local_parent + "\"";
        int sys_result = system(mkdir_cmd.c_str());
        if (sys_result != 0) {
            S3FS_PRN_ERR("Failed to create local parent directory: %s (result=%d)", 
                         local_parent.c_str(), sys_result);
            return -EIO;
        }
    }

    // Create the actual directory
    int result = create_local_directory(local_target.c_str(), mode, uid, gid);
    if (result != 0) {
        return result;
    }

    S3FS_PRN_INFO("Prepared local directory: %s", local_target.c_str());
    return 0;
}

LocalSymlinkManager& LocalSymlinkManager::instance()
{
    static LocalSymlinkManager manager;
    return manager;
}

//-------------------------------------------------------------------
// C-style API implementation
//-------------------------------------------------------------------
bool init_local_symlink(const LocalSymlinkConfig& config)
{
    return LocalSymlinkManager::instance().initialize(config);
}

bool is_local_symlink_enabled()
{
    return LocalSymlinkManager::instance().get_config().is_enabled();
}

bool should_path_be_local_symlink(const char* path)
{
    return LocalSymlinkManager::instance().should_create_local_symlink(path);
}

int prepare_local_symlink_directory(const char* path, mode_t mode, uid_t uid, gid_t gid, std::string& local_target)
{
    return LocalSymlinkManager::instance().prepare_local_directory(path, mode, uid, gid, local_target);
}

