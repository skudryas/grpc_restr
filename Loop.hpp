#pragma once

#include <sys/epoll.h>
#include "Log.hpp"

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

class Conn
{
  protected:
    Loop *loop_; // weak-ref
    Task taskset_;
    int fd_;
  public:
    Conn(Loop *loop): loop_(loop), taskset_(Task::NONE), fd_(-1) {}
    Conn(const Conn &) = delete;
    Conn(Conn &&) = delete;
    virtual ~Conn() = default;
    int fd() { return fd_; }
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

