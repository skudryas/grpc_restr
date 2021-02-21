#include "TcpAccept.hpp"
#include <string.h>
#include <unistd.h>
#include <iostream>

#undef ERROR
#define ERROR(code) dlgt_->onError(this, TcpAcceptDlgt::Error::code, errno); \
                    closeNow()

TcpAccept::TcpAccept(Loop *loop, TcpAcceptDlgt *dlgt, const char *host,
    const char *port, int backlog, int flags):
       state_(State::WAIT_BIND), bindAddr_(NULL), remoteAddr_(NULL),
       Conn(loop), dlgt_(dlgt) {
  assert(backlog > 0);
  assert(dlgt != NULL);
  // Fuck blocking
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *rpfirst = NULL;
  int ret = getaddrinfo(host, port, &hints, &rpfirst);
  if (ret != 0) {
    ERROR(RESOLV);
    return;
  }
  assert(rpfirst != NULL);
  for (struct addrinfo *rp = rpfirst; rp != NULL; rp = rp->ai_next) {
    fd_ = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK,
       rp->ai_protocol); 
    if (fd_ < 0) {
      continue;
    }
    static const int reuseport_opt = 1;
    if (-1 == setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT,
                      &reuseport_opt, sizeof(reuseport_opt))) {
      continue;
    }
    if (bind(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
      assert(bindAddr_ == NULL);
      bindAddr_ = (sockaddr*)calloc(rp->ai_addrlen, 1);
      remoteAddr_ = (sockaddr*)calloc(rp->ai_addrlen, 1);
      memcpy(bindAddr_, rp->ai_addr, rp->ai_addrlen);
      localAddrLen_ = rp->ai_addrlen;
      listen(fd_, backlog);
      if (!loop_->addTask(Task::IN, this)) {
        freeaddrinfo(rpfirst);
        ERROR(SOCKET);
        return;
      }
      state_ = State::LISTEN;
      break;
    }
    closeNow();
  }
  freeaddrinfo(rpfirst);
  if (fd_ < 0) {
    state_ = State::WAIT_BIND;
    ERROR(SOCKET);
    return;
  } else {
    state_ = State::LISTEN;
  }
}

TcpAccept::~TcpAccept() {
  closeNow();
  if (bindAddr_) free(bindAddr_);
  if (remoteAddr_) free(remoteAddr_);
}

void TcpAccept::onEvent(Task evt) {
  assert(fd_ != -1);
  assert(state_ == State::LISTEN);
  if (evt & Task::HUP || evt & Task::ERR) {
    ERROR(LISTEN);
    return;
  }
  if (evt & Task::IN) {
    DLOG(DEBUG) << "onRead";
    int newfd;
    while (true) {
      socklen_t sl = localAddrLen_;
      newfd = accept4(fd_, remoteAddr_, &sl, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (newfd != -1) {
        struct sockaddr *localAddr = (struct sockaddr*)alloca(sl);
        assert(0 == getsockname(fd_, localAddr, &sl));
        dlgt_->onAccept(this, newfd, localAddr, remoteAddr_);
      } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          if (errno == ENFILE) {
            if (!loop_->modifyTask(Task::NONE, this)) {
              ERROR(ACCEPT);
            } else {
              state_ = State::LISTEN_PAUSED;
              dlgt_->onError(this, TcpAcceptDlgt::Error::ACCEPT, errno);
              // but don't close!
            }
          } else {
            ERROR(ACCEPT);
          }
          break;
        }
        break;
      }
    }
  }
}

void TcpAccept::resumeAccept() {
  assert(state_ == State::LISTEN_PAUSED);
  if (!loop_->modifyTask(Task::IN, this)) {
    ERROR(ACCEPT);
  } else {
    state_ = State::LISTEN;
  }
}

void TcpAccept::closeNow() {
  if (fd_ == -1) {
    return;
  }
  if (state_ != State::LISTEN && state_ != State::LISTEN_PAUSED) {
    loop_->removeTask(this);
  }
  assert(0 == close(fd_));
  
  fd_ = -1;
  state_ = State::CLOSED;
}

