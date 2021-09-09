#include <vector>
#include <thread>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <cassert>

template <typename T>
class BoundedBlockingQueue
{
    size_t limit_;
    size_t size_; // actual size, <= limit_
    size_t start_; // index of queue head 
    std::vector<T> ringbuf_;
    mutable std::mutex mtx_;
    mutable std::condition_variable read_;
    mutable std::condition_variable write_;
  public:
    BoundedBlockingQueue(size_t limit): limit_(limit), size_(0), start_(0) {
      if (limit_ < 1)
        throw std::out_of_range("queue limit must be > 1");
      ringbuf_.reserve(limit);
    }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    T front() const {
      std::unique_lock lock(mtx_);
      bool need_notify = false;
      while (empty()) {
        read_.wait(lock);
        need_notify = true;
      }
      T ret = ringbuf_[start_];
      if (need_notify) read_.notify_one();
      return ret;
    }

    T back() const {
      std::unique_lock lock(mtx_);
      bool need_notify = false;
      while (empty()) {
        read_.wait(lock);
        need_notify = true;
      }
      T ret = ringbuf_[(start_ + size_ - 1) % limit_];
      if (need_notify) read_.notify_one();
      return ret;
    }

    T pop_front() {
      std::unique_lock lock(mtx_);
      while (empty()) {
        read_.wait(lock);
      }
      T ret = std::move(ringbuf_[start_]);
      start_ = (start_ + 1) % limit_;
      --size_;
      write_.notify_one();
      return ret;
    }

    void push_back(const T& val) {
      std::unique_lock lock(mtx_);
      while (size_ == limit_) {
        write_.wait(lock);
      }
      if (ringbuf_.size() != limit_) {
        ringbuf_.push_back(val);
      } else {
        size_t pos = (start_ + size_) % limit_;
        ringbuf_[pos] = val;
      }
      ++size_;
      read_.notify_one();
    }

    void push_back(T&& val) {
      std::unique_lock lock(mtx_);
      while (size_ == limit_) {
        write_.wait(lock);
      }
      if (ringbuf_.size() != limit_) {
        ringbuf_.push_back(val);
      } else {
        size_t pos = (start_ + size_) % limit_;
        ringbuf_[pos] = val;
      }
      ++size_;
      read_.notify_one();
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
      std::unique_lock lock(mtx_);
      while (size_ == limit_) {
        write_.wait(lock);
      }
      if (ringbuf_.size() != limit_) {
        ringbuf_.emplace_back(args...);
      } else {
        size_t pos = (start_ + size_) % limit_;
        *(ringbuf_.begin() + pos) = T(args...);
      }
      ++size_;
      read_.notify_one();
    }
    // no swap..
};

