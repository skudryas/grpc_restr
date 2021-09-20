#pragma once

#include <assert.h>
#include "Loop.hpp"
#include <stdlib.h>
#include <sys/eventfd.h>
#include <math.h>

class EventDlgt;

class Event : public Conn {
  public:
    Event(Loop *loop, EventDlgt *dlgt);
    virtual ~Event();
    virtual void onEvent(Task) override;
    void setEvent();
    uint64_t stopEvent();
    void disableEvent();
    bool disabled() const { return disabled_; }
  private:
    bool disabled_;
    EventDlgt *dlgt_;
};

class EventDlgt
{
  public:
    enum Error {
      FD = 0,
    };
    virtual void onError(Event *event, Error error, int code) = 0;
    virtual void onEvent(Event *event) = 0;
};

