#include "Async.hpp"
#include <unistd.h>

#undef ERROR
#define ERROR(code) if (dlgt_) dlgt_->onError(this, AsyncDlgt::Error::code, errno);

Async::Async(Loop *loop, AsyncDlgt *dlgt) : Conn(loop),
           dlgt_(dlgt), disabled_(false) {
  assert(loop != 0);
  fd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
  if (fd_ == -1) {
    ERROR(FD);
    return;
  }

  taskset_ = Task::IN; 
  if (!loop_->addTask(taskset_, this)) {
    ERROR(FD);
    close(fd_);
    loop_->removeTask(this);
    fd_ = -1;
    return;
  }
}

Async::~Async() {
  DLOG(DEBUG) << "~Async";
  dlgt_ = nullptr;
  while (0 < stopAsync()); // XXX epoll still wake up watcher with already closed fd in some corner cases...
  if (fd_ != -1) {
    close(fd_);
    loop_->removeTask(this);
  }
}

void Async::disableAsync() {
  while (0 < stopAsync());
  disabled_ = true;
  if (fd_ != -1) {
    loop_->removeTask(this);
    close(fd_);
    fd_ = -1;
  }
}

void Async::setAsync() {
  if (disabled_)
    return;
  assert(fd_ != -1);
  uint64_t to_write = 1;
  int ret = write(fd_, &to_write, sizeof(to_write));
  if (ret != 8 && errno != EAGAIN) {
    ERROR(FD);
    close(fd_);
    loop_->removeTask(this);
    fd_ = -1;
    return;
  }
}

uint64_t Async::stopAsync() {
  if (disabled_)
    return -1;
  uint64_t stored = 0;
  int ret = read(fd_, &stored, 8);
  if (ret != 8 && errno != EAGAIN) {
    ERROR(FD);
    close(fd_);
    loop_->removeTask(this);
    fd_ = -1;
    return -1;
  }
  return stored;
}

void Async::onEvent(Task evt) {
  assert(!disabled_);
  assert(fd_ != -1);
  if (evt & Task::IN) {
    stopAsync();
    if (dlgt_) dlgt_->onAsync(this);
  }
}
