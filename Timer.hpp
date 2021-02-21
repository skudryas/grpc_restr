#pragma once

#include <assert.h>
#include "Loop.hpp"
#include <stdlib.h>
#include <sys/timerfd.h>
#include <math.h>

class TimerDlgt;

class Timer : public Conn {
  public:
    using Time = double;
    enum Type {
      REL = 0,
      ABS = 1,
    };
    Timer(Loop *loop, TimerDlgt *dlgt, int clocktype = CLOCK_MONOTONIC);
    virtual ~Timer();
    virtual void onEvent(Task) override;
    void setTimer(const struct timespec &after, const struct timespec &repeat,
        Type t = Type::REL);
    void setTimer(Time ts, Type t = Type::REL) {
      struct timespec after = {.tv_sec = (time_t)trunc(ts),
                               .tv_nsec = (time_t)(t - trunc(ts))},
                      repeat = {.tv_sec = (time_t)trunc(ts),
                                .tv_nsec = (time_t)(t - trunc(ts))};
      setTimer(after, repeat, t);
    }
    void stopTimer();
  private:
    TimerDlgt *dlgt_;
};

class TimerDlgt
{
  public:
    enum Error {
      FD = 0,
    };
    virtual void onError(Timer *timer, Error error, int code) = 0;
    virtual void onTimer(Timer *timer) = 0;
};

