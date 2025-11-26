#ifndef HTTP_NOTIFIER_H_
#define HTTP_NOTIFIER_H_

#include "notification_queue.h"
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <curl/curl.h>

struct NotificationConfig {
    std::string webhook_url;
    int timeout_ms;
    int max_retries;
    int retry_delay_ms;
    std::vector<std::string> exclude_paths;
    
    NotificationConfig();
    bool is_valid() const;
    void parse_exclude_paths(const char* paths_str);
};

class HttpNotifier {
private:
    NotificationConfig config;
    NotificationQueue event_queue;
    std::thread worker_thread;
    std::atomic<bool> initialized;
    
    static CURL* create_curl_handle();
    static void cleanup_curl_handle(CURL* curl);
    
    int send_notification(const FileOperationEvent& event);
    void worker_loop();
    
public:
    HttpNotifier();
    ~HttpNotifier();
    
    bool initialize(const NotificationConfig& cfg);
    void notify_async(const FileOperationEvent& event);
    int notify_sync(const FileOperationEvent& event);
    void shutdown();
    bool should_exclude_notification(const char* file_path) const;
    
    static HttpNotifier& instance();
};

namespace FileOperation {
    constexpr const char* CREATE = "CREATE";
    constexpr const char* DELETE = "DELETE";
    constexpr const char* UPDATE = "UPDATE";
}

#ifdef __cplusplus
extern "C" {
#endif

bool init_http_notifications(const NotificationConfig& config);
int notify_file_operation_async(const char* file_path, const char* operation, size_t file_size, int is_directory = 0);
int notify_file_operation_sync(const char* file_path, const char* operation, size_t file_size, int is_directory = 0);
void cleanup_http_notifications();

#ifdef __cplusplus
}
#endif

#endif // HTTP_NOTIFIER_H_ 