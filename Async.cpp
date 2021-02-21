#include "Async.hpp"
#include <unistd.h>

#undef ERROR
#define ERROR(code) if (dlgt_) dlgt_->onError(this, AsyncDlgt::Error::code, errno);

Async::Async(Loop *loop, AsyncDlgt *dlgt) : Conn(loop),
           dlgt_(dlgt) {
  assert(loop != 0);
  fd_ = eventfd(1, EFD_NONBLOCK);
  if (fd_ == -1) {
    ERROR(FD);
    return;
  }

  taskset_ = Task::IN; 
  if (!loop_->addTask(taskset_, this)) {
    ERROR(FD);
    close(fd_);
    fd_ = -1;
    return;
  }
}

Async::~Async() {
  DLOG(DEBUG) << "~Async";
  dlgt_ = nullptr;
  if (fd_ != -1)
    close(fd_);
}

void Async::setAsync() {
  assert(fd_ != -1);
  uint64_t to_write = 1;
  int ret = write(fd_, &to_write, sizeof(to_write));
  if (ret != 8 && ret != EAGAIN) {
    ERROR(FD);
    close(fd_);
    fd_ = -1;
    return;
  }
}

void Async::stopAsync() {
  uint64_t unused;
  int ret = read(fd_, &unused, 8);
  if (ret != 8 && ret != EAGAIN) {
    ERROR(FD);
    close(fd_);
    fd_ = -1;
    return;
  }
}

void Async::onEvent(Task evt) {
  assert(fd_ != -1);
  if (evt & Task::IN) {
    stopAsync();
    if (dlgt_) dlgt_->onAsync(this);
  }
}
