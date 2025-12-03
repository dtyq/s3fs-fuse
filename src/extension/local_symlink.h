#ifndef LOCAL_SYMLINK_H_
#define LOCAL_SYMLINK_H_

#include <string>
#include <set>
#include <sys/types.h>

//-------------------------------------------------------------------
// LocalSymlinkConfig: Configuration for local symlink feature
//-------------------------------------------------------------------
struct LocalSymlinkConfig {
    std::set<std::string> symlink_paths;  // directory names that should be symlinked
    std::string base_path;                 // base directory for local symlinks

    LocalSymlinkConfig();
    bool is_valid() const;
    bool is_enabled() const;
    void parse_symlink_paths(const char* paths_str);
};

//-------------------------------------------------------------------
// LocalSymlinkManager: Manager class for local symlink operations
//-------------------------------------------------------------------
class LocalSymlinkManager {
private:
    LocalSymlinkConfig config;
    bool initialized;

    // Helper functions
    static std::string get_basename(const char* path);
    int create_local_directory(const char* local_path, mode_t mode, uid_t uid, gid_t gid);

public:
    LocalSymlinkManager();
    ~LocalSymlinkManager();

    // Initialize with configuration
    bool initialize(const LocalSymlinkConfig& cfg);

    // Check if a path should create local symlink
    bool should_create_local_symlink(const char* path) const;

    // Get local target path for a given S3 path
    std::string get_local_target_path(const char* path) const;

    // Create local directory and return the local path
    // Returns 0 on success, negative errno on failure
    int prepare_local_directory(const char* path, mode_t mode, uid_t uid, gid_t gid, std::string& local_target);

    // Get configuration (for read-only access)
    const LocalSymlinkConfig& get_config() const { return config; }

    // Singleton instance
    static LocalSymlinkManager& instance();
};

#ifdef __cplusplus
extern "C" {
#endif

// C-style API for convenience
bool init_local_symlink(const LocalSymlinkConfig& config);
bool is_local_symlink_enabled();
bool should_path_be_local_symlink(const char* path);
int prepare_local_symlink_directory(const char* path, mode_t mode, uid_t uid, gid_t gid, std::string& local_target);

#ifdef __cplusplus
}
#endif

#endif // LOCAL_SYMLINK_H_

