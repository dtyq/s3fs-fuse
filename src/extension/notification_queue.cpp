#include "notification_queue.h"
#include "s3fs_logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

FileOperationEvent::FileOperationEvent(const char* path, const char* op, size_t size, const char* mount)
    : file_path(path ? path : ""), operation(op ? op : ""), file_size(size), mount_point(mount ? mount : "")
{
    // Generate ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    timestamp = ss.str();
}

std::string FileOperationEvent::to_json() const
{
    std::stringstream json;
    json << "{"
         << "\"timestamp\":\"" << timestamp << "\","
         << "\"operation\":\"" << operation << "\","
         << "\"file_path\":\"" << file_path << "\","
         << "\"file_size\":" << file_size << ","
         << "\"mount_point\":\"" << mount_point << "\""
         << "}";
    return json.str();
}

NotificationQueue::NotificationQueue() : shutdown_flag(false)
{
}

NotificationQueue::~NotificationQueue()
{
    shutdown();
}

void NotificationQueue::enqueue(const FileOperationEvent& event)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (shutdown_flag.load()) {
        return;
    }
    event_queue.push(event);
    queue_cv.notify_one();
}

bool NotificationQueue::dequeue(FileOperationEvent& event, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    
    if (queue_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                         [this] { return !event_queue.empty() || shutdown_flag.load(); })) {
        if (!event_queue.empty()) {
            event = event_queue.front();
            event_queue.pop();
            return true;
        }
    }
    return false;
}

void NotificationQueue::shutdown()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    shutdown_flag.store(true);
    queue_cv.notify_all();
}

size_t NotificationQueue::size() const
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return event_queue.size();
} 