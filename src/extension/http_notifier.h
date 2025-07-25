#ifndef HTTP_NOTIFIER_H_
#define HTTP_NOTIFIER_H_

#include "notification_queue.h"
#include <thread>
#include <atomic>
#include <string>
#include <curl/curl.h>

struct NotificationConfig {
    std::string webhook_url;
    int timeout_ms;
    int max_retries;
    int retry_delay_ms;
    
    NotificationConfig();
    bool is_valid() const;
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
    
    static HttpNotifier& instance();
};

#ifdef __cplusplus
extern "C" {
#endif

bool init_http_notifications(const char* webhook_url, int timeout_ms = 5000);
int notify_file_operation_async(const char* file_path, const char* operation, size_t file_size);
int notify_file_operation_sync(const char* file_path, const char* operation, size_t file_size);
void cleanup_http_notifications();

#ifdef __cplusplus
}
#endif

#endif // HTTP_NOTIFIER_H_ 