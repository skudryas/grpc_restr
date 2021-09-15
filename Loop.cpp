#include "Loop.hpp"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <iostream>

volatile thread_local Loop *g_current_loop = nullptr;

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
      Conn *c = (Conn*)events_[n].data.ptr;
      assert(c != 0);
      //DLOG(DEBUG) << "gotEvent for " << c->fd() << " event=" << events_[n].events;
      c->onEvent((Task)events_[n].events);
    }
  }
  g_current_loop = nullptr;
}

bool Loop::addTask(Task events, Conn *ptr)
{
  struct epoll_event ev;
  ev.events = (unsigned int)events;
  ev.data.ptr = ptr;
  DLOG(DEBUG) << "addTask: " << events << " fd: " << ptr->fd() << " ptr: " << ptr;
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_ADD, ptr->fd(), &ev);
}

bool Loop::modifyTask(Task events, Conn *ptr)
{
  struct epoll_event ev;
  ev.events = (unsigned int)events;
  ev.data.ptr = ptr;
  DLOG(DEBUG) << "modifyTask: " << events << " fd: " << ptr->fd() << " ptr: " << ptr;
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_MOD, ptr->fd(), &ev);
}

bool Loop::removeTask(Conn *ptr)
{
  struct epoll_event ev;
  ev.data.ptr = ptr;
  DLOG(DEBUG) << "deleteTask fd: " << ptr->fd() << " ptr: " << ptr;
  return -1 != epoll_ctl(epollfd_, EPOLL_CTL_DEL, ptr->fd(), &ev);
}

Loop::~Loop() {
  delete [] events_;
  close(epollfd_);
}

