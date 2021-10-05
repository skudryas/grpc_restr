#include "Loop.hpp"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <iostream>

volatile thread_local Loop *g_current_loop = nullptr;

struct wrappershead Loop::global_free_wrappers =
  TAILQ_HEAD_INITIALIZER(Loop::global_free_wrappers);
struct wrappershead Loop::global_used_wrappers =
  TAILQ_HEAD_INITIALIZER(Loop::global_used_wrappers);
std::atomic_flag Loop::global_wrappers_lock = ATOMIC_FLAG_INIT;

volatile Loop *Loop::getCurrentLoop() {
  return g_current_loop;
}

Loop::Loop(int evcount): run_(false) {
   epollfd_ = epoll_create1(0); 
   events_ = new struct epoll_event[evcount];
   evcount_ = evcount;
}

void Loop::run() {
  run_ = true;
  g_current_loop = this;
  while (run_) {
    int nfds = epoll_wait(epollfd_, events_, evcount_, -1);
    if (nfds < 0 && errno != EINTR) {
      perror("epoll_wait failure\n");
      exit(EXIT_FAILURE);
    }
    for (int n = 0; n < nfds; ++n) {
      Wrapper *w = (Wrapper*)events_[n].data.ptr;
      assert(w != 0);
      //DLOG(DEBUG) << "gotEvent for " << c->fd() << " event=" << events_[n].events;
      w->onEvent((Task)events_[n].events);
    }
  }
  g_current_loop = nullptr;
}

bool Loop::addTask(Task events, Conn *ptr)
{
  connectWrapper(ptr);
  struct epoll_event ev;
  ev.events = (unsigned int)events;
  ev.data.ptr = ptr->wrapper();;
  DLOG(DEBUG) << "addTask: " << events << " fd: " << ptr->fd() << " ptr: " << ptr;
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_ADD, ptr->fd(), &ev);
}

bool Loop::modifyTask(Task events, Conn *ptr)
{
  struct epoll_event ev;
  ev.events = (unsigned int)events;
  ev.data.ptr = ptr->wrapper();
  DLOG(DEBUG) << "modifyTask: " << events << " fd: " << ptr->fd() << " ptr: " << ptr;
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_MOD, ptr->fd(), &ev);
}

bool Loop::removeTask(Conn *ptr)
{
  struct epoll_event ev;
  ev.data.ptr = ptr->wrapper();
  DLOG(DEBUG) << "deleteTask fd: " << ptr->fd() << " ptr: " << ptr;
  disconnectWrapper(ptr);
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_DEL, ptr->fd(), &ev);
}

Loop::~Loop() {
  delete [] events_;
  close(epollfd_);
}

/*static*/ void Loop::connectWrapper(Conn *conn)
{
  // lock
  while (global_wrappers_lock.test_and_set(std::memory_order_relaxed));
  AtomicUnlock lock(global_wrappers_lock);

  Wrapper *w = nullptr;
  if (TAILQ_EMPTY(&global_free_wrappers)) {
    w = new Wrapper();
  } else {
    w = TAILQ_FIRST(&global_free_wrappers);
    TAILQ_REMOVE(&global_free_wrappers, w, next);
  }
  TAILQ_INSERT_TAIL(&global_used_wrappers, w, next);

  conn->setWrapper(w);
  w->setConn(conn);
}

/*static*/ void Loop::disconnectWrapper(Conn *conn)
{
  // lock
  while (global_wrappers_lock.test_and_set(std::memory_order_relaxed));
  AtomicUnlock lock(global_wrappers_lock);

  Wrapper *w = conn->wrapper();
  TAILQ_INSERT_TAIL(&global_free_wrappers, w, next);
  TAILQ_REMOVE(&global_used_wrappers, w, next);

  conn->setWrapper(nullptr);
  w->setConn(nullptr);
}

