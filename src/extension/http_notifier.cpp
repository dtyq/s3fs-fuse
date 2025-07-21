#include "http_notifier.h"
#include "s3fs_logger.h"
#include <chrono>
#include <thread>
#include <cstring>

extern std::string mountpoint;

NotificationConfig::NotificationConfig()
    : timeout_ms(5000), max_retries(3), retry_delay_ms(1000)
{
}

bool NotificationConfig::is_valid() const
{
    return !webhook_url.empty() && timeout_ms > 0 && max_retries >= 0;
}

HttpNotifier::HttpNotifier() : initialized(false)
{
}

HttpNotifier::~HttpNotifier()
{
    shutdown();
}

bool HttpNotifier::initialize(const NotificationConfig& cfg)
{
    if (initialized.load()) {
        S3FS_PRN_WARN("HttpNotifier already initialized");
        return false;
    }
    
    if (!cfg.is_valid()) {
        S3FS_PRN_ERR("Invalid notification configuration");
        return false;
    }
    
    config = cfg;
    
    // Start worker thread
    worker_thread = std::thread(&HttpNotifier::worker_loop, this);
    initialized.store(true);
    
    S3FS_PRN_INFO("HTTP notification initialized: %s", cfg.webhook_url.c_str());
    return true;
}

void HttpNotifier::notify_async(const FileOperationEvent& event)
{
    if (!initialized.load()) {
        return;
    }
    event_queue.enqueue(event);
}

int HttpNotifier::notify_sync(const FileOperationEvent& event)
{
    if (!initialized.load()) {
        S3FS_PRN_WARN("HTTP notifications not initialized, skipping sync notification");
        return -1;
    }
    
    // Send notification directly in current thread
    return send_notification(event);
}

void HttpNotifier::shutdown()
{
    if (!initialized.load()) {
        return;
    }
    
    initialized.store(false);
    event_queue.shutdown();
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    
    S3FS_PRN_INFO("HTTP notification shutdown");
}

HttpNotifier& HttpNotifier::instance()
{
    static HttpNotifier instance;
    return instance;
}

CURL* HttpNotifier::create_curl_handle()
{
    CURL* curl = curl_easy_init();
    if (curl) {
        // Basic configuration
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "s3fs-notification/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        
        // SSL configuration
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    return curl;
}

void HttpNotifier::cleanup_curl_handle(CURL* curl)
{
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

int HttpNotifier::send_notification(const FileOperationEvent& event)
{
    CURL* curl = create_curl_handle();
    if (!curl) {
        S3FS_PRN_ERR("Failed to create CURL handle for notification");
        return -1;
    }
    
    std::string json_data = event.to_json();
    struct curl_slist* headers = nullptr;
    
    // Set HTTP headers
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    
    // Configure CURL options
    curl_easy_setopt(curl, CURLOPT_URL, config.webhook_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.length());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.timeout_ms);
    
    // Retry logic
    int result = -1;
    for (int attempt = 0; attempt <= config.max_retries; ++attempt) {
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code >= 200 && response_code < 300) {
                S3FS_PRN_DBG("Notification sent successfully: %s %s", 
                           event.operation.c_str(), event.file_path.c_str());
                result = 0;
                break;
            } else {
                S3FS_PRN_WARN("HTTP notification failed with response code: %ld", response_code);
            }
        } else {
            S3FS_PRN_WARN("CURL error during notification (attempt %d): %s", 
                         attempt + 1, curl_easy_strerror(res));
        }
        
        if (attempt < config.max_retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.retry_delay_ms));
        }
    }
    
    if (result != 0) {
        S3FS_PRN_ERR("Failed to send notification after %d attempts: %s %s",
                    config.max_retries + 1, event.operation.c_str(), event.file_path.c_str());
    }
    
    // Clean up resources
    curl_slist_free_all(headers);
    cleanup_curl_handle(curl);
    
    return result;
}

void HttpNotifier::worker_loop()
{
    S3FS_PRN_INFO("HTTP notification worker thread started");
    
    while (initialized.load()) {
        FileOperationEvent event("", "", 0, "");
        if (event_queue.dequeue(event, 1000)) {
            send_notification(event);
        }
    }
    
    // Process remaining events
    FileOperationEvent event("", "", 0, "");
    while (event_queue.dequeue(event, 0)) {
        send_notification(event);
    }
    
    S3FS_PRN_INFO("HTTP notification worker thread stopped");
}

bool init_http_notifications(const char* webhook_url, int timeout_ms)
{
    if (!webhook_url || strlen(webhook_url) == 0) {
        return false;
    }
    
    NotificationConfig config;
    config.webhook_url = webhook_url;
    config.timeout_ms = timeout_ms;
    
    return HttpNotifier::instance().initialize(config);
}

void notify_file_operation_async(const char* file_path, const char* operation, size_t file_size)
{
    if (!file_path || !operation) {
        return;
    }
    
    FileOperationEvent event(file_path, operation, file_size, mountpoint.c_str());
    HttpNotifier::instance().notify_async(event);
}

int notify_file_operation_sync(const char* file_path, const char* operation, size_t file_size)
{
    if (!file_path || !operation) {
        return -1;
    }
    
    FileOperationEvent event(file_path, operation, file_size, mountpoint.c_str());
    return HttpNotifier::instance().notify_sync(event);
}

void cleanup_http_notifications()
{
    HttpNotifier::instance().shutdown();
}
