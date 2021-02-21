#pragma once

#include <assert.h>
#include "Loop.hpp"
#include <stdlib.h>
#include <sys/eventfd.h>
#include <math.h>

class AsyncDlgt;

class Async : public Conn {
  public:
    Async(Loop *loop, AsyncDlgt *dlgt);
    virtual ~Async();
    virtual void onEvent(Task) override;
    void setAsync();
    void stopAsync();
  private:
    AsyncDlgt *dlgt_;
};

class AsyncDlgt
{
  public:
    enum Error {
      FD = 0,
    };
    virtual void onError(Async *async, Error error, int code) = 0;
    virtual void onAsync(Async *async) = 0;
};

