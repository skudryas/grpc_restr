#pragma once

#include <sys/epoll.h>
#include <sys/queue.h>
#include <atomic>
#include "Log.hpp"

class AtomicUnlock
{
  std::atomic_flag &a_;
public:
  AtomicUnlock(AtomicUnlock&) = delete;
  AtomicUnlock(AtomicUnlock&& rhs): a_(rhs.a_) {}
  AtomicUnlock& operator=(AtomicUnlock &) = delete;
  AtomicUnlock& operator=(AtomicUnlock &&) = delete;
  AtomicUnlock(std::atomic_flag &a): a_(a)
  {
  }
  bool test_and_set() {
    return a_.test_and_set(std::memory_order_relaxed);
  }
  ~AtomicUnlock()
  {
    a_.clear(std::memory_order_relaxed);
  }
};

//#define THREADED_POLLING

enum Task: unsigned int {
  NONE = 0,
  IN = EPOLLIN,
  OUT = EPOLLOUT,
  HUP = EPOLLHUP,
  RHUP = EPOLLRDHUP,
  ERR = EPOLLERR,
};

class Loop;
class Wrapper;
class Conn;

TAILQ_HEAD(wrappershead, Wrapper);

class Conn
{
  private:
    Wrapper *wrapper_; // weak-ref
  public:
    Wrapper *wrapper() { return wrapper_; }
    void setWrapper(Wrapper *w) { wrapper_ = w; }

  protected:
    Loop *loop_; // weak-ref
    Task taskset_;
    int fd_;
  public:
    Conn(Loop *loop): loop_(loop), taskset_(Task::NONE), fd_(-1) {}
    Conn(const Conn &) = delete;
    Conn(Conn &&) = delete;
    virtual ~Conn() = default;
    int fd() const { return fd_; }
    virtual void onEvent(Task evt) = 0;
    Loop *loop() { return loop_; }
};

class Loop
{
  private:
    int epollfd_;
    struct epoll_event *events_;
    int evcount_;
    bool run_;

  private:
    // Wrappers stuff
static struct wrappershead global_free_wrappers;
static struct wrappershead global_used_wrappers;
static std::atomic_flag global_wrappers_lock;
static void connectWrapper(Conn *conn);
static void disconnectWrapper(Conn *conn);
  public:
    Loop(int evcount = 1024);
    ~Loop();
    void run();
    void stop() { run_ = false; }
    bool addTask(Task events, Conn*);
    bool modifyTask(Task events, Conn*);
    bool removeTask(Conn*);

    int epollfd() { return epollfd_; }

static volatile Loop* getCurrentLoop();
};

class Wrapper
{
  private:
    Conn *conn_;
    mutable std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
  public:
    Wrapper(Conn *conn = nullptr): conn_(conn) {}
    std::atomic_flag &getLock() { return locked_; }
    void setConn(Conn *conn = nullptr)
    {
      conn_ = conn;
    }
    void onEvent(Task evt) {
      // try_lock
      if (locked_.test_and_set(std::memory_order_relaxed))
        return;
      AtomicUnlock lock(locked_);
      if (conn_)
        conn_->onEvent(evt);
    }
    int fd() const {
      while (locked_.test_and_set(std::memory_order_relaxed));
      AtomicUnlock lock(locked_);

      if (!conn_)
        return -1;
      return conn_->fd();
    }
    Loop *loop() {
      while (locked_.test_and_set(std::memory_order_relaxed));
      AtomicUnlock lock(locked_);

      if (!conn_)
        return nullptr;
      return conn_->loop();
    }

    TAILQ_ENTRY(Wrapper) next;
};

