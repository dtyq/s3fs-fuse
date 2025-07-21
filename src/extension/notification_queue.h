#ifndef NOTIFICATION_QUEUE_H_
#define NOTIFICATION_QUEUE_H_

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>

struct FileOperationEvent {
    std::string file_path;
    std::string operation;
    size_t file_size;
    std::string timestamp;
    std::string mount_point;
    
    FileOperationEvent(const char* path, const char* op, size_t size, const char* mount);
    std::string to_json() const;
};

class NotificationQueue {
private:
    std::queue<FileOperationEvent> event_queue;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> shutdown_flag;
    
public:
    NotificationQueue();
    ~NotificationQueue();
    
    void enqueue(const FileOperationEvent& event);
    bool dequeue(FileOperationEvent& event, int timeout_ms = 1000);
    void shutdown();
    size_t size() const;
};

#endif // NOTIFICATION_QUEUE_H_ 