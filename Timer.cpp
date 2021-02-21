#include "Timer.hpp"
#include <unistd.h>

#undef ERROR
#define ERROR(code) if (dlgt_) dlgt_->onError(this, TimerDlgt::Error::code, errno);


Timer::Timer(Loop *loop, TimerDlgt *dlgt, int clocktype) : Conn(loop),
           dlgt_(dlgt) {
  assert(loop != 0);
  fd_ = timerfd_create(clocktype, TFD_NONBLOCK);;
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

Timer::~Timer() {
  DLOG(DEBUG) << "~Timer";
  dlgt_ = nullptr;
  if (fd_ != -1)
    close(fd_);
}

void Timer::setTimer(const struct timespec &after, const struct timespec &repeat,
        Type t) {
  assert(fd_ != -1);
  struct itimerspec its = {.it_interval = repeat, .it_value = after};
  int ret = timerfd_settime(fd_, t == Type::REL ? 0 : TFD_TIMER_ABSTIME,
                 &its, NULL);
  if (ret != 0) {
    ERROR(FD);
    close(fd_);
    fd_ = -1;
    return;
  }
}

void Timer::stopTimer() {
  struct timespec after = {.tv_sec = 0, .tv_nsec = 0},
                  repeat = {.tv_sec = 0, .tv_nsec = 0};
  setTimer(after, repeat);
}

void Timer::onEvent(Task evt) {
  assert(fd_ != -1);
  if (evt & Task::IN) {
    uint64_t unused;
    int ret = read(fd_, &unused, 8);
    if (ret != 8) {
      ERROR(FD);
    } else {
      if (dlgt_) dlgt_->onTimer(this);
    }
  }
}
