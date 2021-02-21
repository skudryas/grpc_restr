#pragma once

#include <list>
#include <assert.h>
#include "Chain.hpp"
#include "Loop.hpp"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <atomic>

class AtomicUnlock
{
  std::atomic_flag &a_;
public:
  AtomicUnlock(AtomicUnlock&&) = delete;
  AtomicUnlock& operator=(AtomicUnlock &) = delete;
  AtomicUnlock& operator=(AtomicUnlock &&) = delete;
  AtomicUnlock(std::atomic_flag &a): a_(a)
  {
  }
  ~AtomicUnlock()
  {
    a_.clear(std::memory_order_relaxed);
  }
};

class TcpConnDlgt;

// TODO timerfd_create, asyncs ...
#define SOCKADDR_SIZE(__sa) ((__sa)->sa_family == AF_INET6 ? \
    sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) 

class TcpConn : public Conn {
  public:
    TcpConn(Loop *loop, TcpConnDlgt *dlgt, int fd, struct sockaddr *remote,
           struct sockaddr *local, int flags = 0);
    TcpConn(Loop *loop, TcpConnDlgt *dlgt, int flags = 0):
          Conn(loop),  dlgt_(dlgt), flags_(flags),
          state_(State::WAIT_CONNECT), rpfirst_(NULL) {
      assert(loop != 0);
    }
    virtual ~TcpConn();
    void connect(const char *host, const char *port);
    void readStop();
    void writeSome();
    void readSome();
    void closeSoon(); // use this
    void closeNow(); // don't use this
    virtual void onEvent(Task) override;
    enum class State {
      WAIT_CONNECT = 1,
      CONNECTING,
      CONNECTED,
      FLUSHING,
      CLOSED
    };
    State state() const {
      return state_;
    }
    const struct sockaddr* localAddr() const {
      return (struct sockaddr*)&localAddr_;
    }
    const struct sockaddr* remoteAddr() const {
      return (struct sockaddr*)&remoteAddr_;
    }
    Chain& input() {
      return input_;
    }
    Chain& output() {
      return output_;
    }
  private:
#ifdef THREADED_POLLING
    std::atomic_flag inEvent_ = ATOMIC_FLAG_INIT;
#endif
    size_t readWinShift_ = 0;
    void tryConnect();
    struct addrinfo *rp_, *rpfirst_;
    int flags_;
    Chain input_, output_;
    TcpConnDlgt *dlgt_;
    State state_;
    typedef union {
      struct sockaddr_in s_in;
      struct sockaddr_in6 s_in6;
    } TcpAddress;
    TcpAddress localAddr_;
    TcpAddress remoteAddr_;
};

class TcpConnDlgt
{
  public:
    enum Error {
      SOCKET = 0,
      NOTAVAIL,
      RESOLV,
      CONNECT,
      READ,
      WRITE,
      CLOSE,
    };
    virtual void onError(TcpConn *conn, Error error, int code) = 0;
    virtual void onRead(TcpConn *conn, size_t) = 0;
    virtual void onWrite(TcpConn *conn, Chain::Buffer&& buf) = 0;
    virtual void onConnected(TcpConn *conn) = 0;
    virtual void onClosed(TcpConn *conn) = 0;
};

