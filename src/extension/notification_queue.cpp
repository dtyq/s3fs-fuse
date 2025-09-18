#include "notification_queue.h"
#include "s3fs_logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

FileOperationEvent::FileOperationEvent(const char* path, const char* op, size_t size, int is_dir)
    : operation(op ? op : ""), file_size(size), is_directory(is_dir)
{
    // Remove leading slash from file_path to make it relative to mount point
    if (path && path[0] == '/' && path[1] != '\0') {
        file_path = path + 1;  // Skip the leading '/'
    } else if (path) {
        file_path = path;
    } else {
        file_path = "";
    }
    
    // Generate Unix timestamp
    auto now = std::chrono::system_clock::now();
    timestamp = std::chrono::system_clock::to_time_t(now);
}

std::string FileOperationEvent::to_json() const
{
    // Use nlohmann::json to automatically handle JSON escaping
    nlohmann::json j;
    j["timestamp"] = timestamp;
    j["operation"] = operation;
    j["file_path"] = file_path;
    j["file_size"] = file_size;
    j["is_directory"] = is_directory;
    
    return j.dump();
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