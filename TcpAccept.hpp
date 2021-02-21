#pragma once

#include "Loop.hpp"
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>

class TcpAccept;

class TcpAcceptDlgt
{
  public:
    enum Error {
      SOCKET,
      RESOLV,
      LISTEN,
      ACCEPT,
      CLOSE,
    };
    virtual void onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr) = 0;
    virtual void onError(TcpAccept *acc, Error error, int code) = 0;
};

class TcpAccept : public Conn {
  public:
    TcpAccept(Loop *loop, TcpAcceptDlgt *dlgt, const char *host, const char *port,
        int backlog = 1024, int flags = 0);
    ~TcpAccept();
    virtual void onEvent(Task) override;
    void closeNow();
    void resumeAccept();
    enum class State {
      WAIT_BIND = 1,
      LISTEN,
      LISTEN_PAUSED,
      CLOSED
    };
    const struct sockaddr *bindAddr() { return bindAddr_; }
  private:
    struct sockaddr *bindAddr_;
    struct sockaddr *remoteAddr_;
    socklen_t localAddrLen_;
    State state_;
    TcpAcceptDlgt *dlgt_;
};
