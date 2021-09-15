#include "TcpConn.hpp"
#include <iostream>

#undef ERROR
#define ERROR(code) if (dlgt_) dlgt_->onError(this, TcpConnDlgt::Error::code, errno); \
                    closeNow()

TcpConn::TcpConn(Loop *loop, TcpConnDlgt *dlgt, int fd, struct sockaddr *remote,
           struct sockaddr *local, int flags) : Conn(loop),
           state_(State::CONNECTED), dlgt_(dlgt), flags_(flags),
           rpfirst_(nullptr) {
  assert(loop != 0);
  assert(local != NULL && remote != NULL);
  assert(fd != -1);
  fd_ = fd;

  int tcp_nodelay = 1;
  if (0 != setsockopt(fd_, SOL_TCP, TCP_NODELAY, &tcp_nodelay,
        (socklen_t)sizeof(tcp_nodelay))) {
    ERROR(SOCKET);
    return;
  }

  memcpy(&localAddr_, local, SOCKADDR_SIZE(local));
  memcpy(&remoteAddr_, remote, SOCKADDR_SIZE(remote));

  if (!loop_->addTask(taskset_, this)) {
    state_ = State::WAIT_CONNECT;
    ERROR(CONNECT);
    return;
  }
  if (dlgt_) dlgt_->onConnected(this);
}

TcpConn::~TcpConn() {
  DLOG(DEBUG) << "~TcpConn";
  dlgt_ = nullptr;
  closeNow();
  if (rpfirst_ != NULL) {
    freeaddrinfo(rpfirst_);
  }
}

void TcpConn::connect(const char *host, const char *port) {
  assert(fd_ < 0);
 
  // Fuck blocking
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = IPPROTO_TCP;

  if (rpfirst_ != NULL) {
    freeaddrinfo(rpfirst_);
  }
  int ret = getaddrinfo(host, port, &hints, &rpfirst_);
  if (ret != 0) {
    ERROR(RESOLV);
    return;
  }

  rp_ = rpfirst_;
  state_ = State::CONNECTING;
  tryConnect();
}

void TcpConn::tryConnect() {
  for ( ; rp_ != NULL; rp_ = rp_->ai_next) {
    fd_ = socket(rp_->ai_family, rp_->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
        rp_->ai_protocol);
    if (fd_ < 0) {
      continue;
    }

    int reuseaddr = 1;
    if (0 != setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
          (socklen_t)sizeof(reuseaddr))) {
      ERROR(SOCKET);
      return;
    }

    int tcp_nodelay = 1;
    if (0 != setsockopt(fd_, SOL_TCP, TCP_NODELAY, &tcp_nodelay,
          (socklen_t)sizeof(tcp_nodelay))) {
      ERROR(SOCKET);
      return;
    }
 
    if (::connect(fd_, rp_->ai_addr, rp_->ai_addrlen) < 0) {
      if (errno == EINPROGRESS) {
        taskset_ = Task::OUT;
        if (!loop_->addTask(taskset_, this)) {
          state_ = State::WAIT_CONNECT;
          ERROR(CONNECT);
          return;
        }
        state_ = State::CONNECTING;
        break;
      } else if (errno == EADDRNOTAVAIL) {
        closeNow();
        state_ = State::WAIT_CONNECT;
        ERROR(NOTAVAIL);
        return;
      }
    } else {
      state_ = State::CONNECTED;
      memcpy(&remoteAddr_, rp_->ai_addr, rp_->ai_addrlen);
      socklen_t size = rp_->ai_addrlen;
      assert(0 == getsockname(fd_, (struct sockaddr*)&localAddr_, &size));
      if (dlgt_) dlgt_->onConnected(this);
      break;
    }
    closeNow();
  }
  if (fd_ < 0) {
    state_ = State::WAIT_CONNECT;
    ERROR(SOCKET);
    return;
  }
}

void TcpConn::writeSome() {
  assert(state_ == State::CONNECTED);
  if (/*directWrite()*/ true) {
    Task newset = (Task)(taskset_ | Task::OUT);
    if (taskset_ != newset) {
      DLOG(DEBUG) << "taskset = " << taskset_ << " newset = " << newset << " fd = " << fd() << " ptr = " << this;
      taskset_ = newset;
      if (!loop_->modifyTask(taskset_, this)) {
        ERROR(WRITE);
      }
    }
  }
}

void TcpConn::readSome() {
  assert(state_ == State::CONNECTED);
  Task newset = (Task)(taskset_ | Task::IN);
  if (taskset_ != newset) {
    DLOG(DEBUG) << "taskset = " << taskset_ << " newset = " << newset << " fd = " << fd() << " ptr = " << this;
    taskset_ = newset;
    if (!loop_->modifyTask(taskset_, this)) {
      ERROR(READ);
    }
  }
}

void TcpConn::readStop() {
  assert(state_ == State::CONNECTED);
  Task newset = (Task)(taskset_ & (~Task::IN));
  if (taskset_ != newset) {
    DLOG(DEBUG) << "taskset = " << taskset_ << " newset = " << newset << " fd = " << fd() << " ptr = " << this;
    taskset_ = newset;
    if (!loop_->modifyTask(taskset_, this)) {
      ERROR(READ);
    }
  }
}

void TcpConn::closeSoon() {
  assert(state_ == State::CONNECTED);
  state_ = State::FLUSHING;
  if (output_.size() == 0) {
    if (0 != shutdown(fd_, SHUT_WR)) {
      ERROR(CLOSE);
      return;
    }
    closeNow();
  } else {
    DLOG(DEBUG) << "close soon, flushing...";
    Task newset = (Task)((taskset_ & (~Task::IN)) | Task::OUT);
    if (taskset_ != newset) {
      DLOG(DEBUG) << "taskset = " << taskset_ << " newset = " << newset << " fd = " << fd() << " ptr = " << this;
      taskset_ = newset;
      if (!loop_->modifyTask(taskset_, this)) {
        ERROR(CLOSE);
      }
    }
  }
}

void TcpConn::closeNow() {
  if (fd_ == -1) {
    return;
  }
  if (state_ != State::CONNECTING) {
    loop_->removeTask(this);
  }
  assert(0 == close(fd_));
  fd_ = -1;
  if (state_ != State::CONNECTING) {
    state_ = State::CLOSED;
    DLOG(DEBUG) << "closing tcp conn";
    if (dlgt_) dlgt_->onClosed(this);
  }
}

/*
 * false - ERROR, NO_DATA
 * true  - EAGAIN,
 * */

bool TcpConn::directWrite() {
  int ret = 0;
  do {
    Chain::Buffer buf = output_.writeFrom();
    if (buf.size > 0) {
      ret = write(fd_, buf.buf, buf.size);
    }
    if (ret < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        if (state_ == State::FLUSHING) {
          closeNow();
          return false;
        }
        ERROR(WRITE);
        return false;
      } else {
        return true;
      }
    } else {
      if (ret > 0) {
        if (dlgt_) dlgt_->onWrite(this, Chain::Buffer{buf.buf, (size_t)ret});
        output_.drain(ret);
      }
      if (output_.size() == 0) {
        Task newset = (Task)(taskset_ & (~Task::OUT));
        if (taskset_ != newset) {
          taskset_ = newset;
          DLOG(DEBUG) << "directWrite taskset = " << taskset_<< " fd = " << fd() << " ptr = " << this;
          if (!loop_->modifyTask(taskset_, this)) {
            ERROR(WRITE);
            return false;
          }
        }
        if (state_ == State::FLUSHING) {
          if (0 != shutdown(fd_, SHUT_WR)) {
            ERROR(CLOSE);
            return false;
          }
          closeNow();
          return false;
        }
        return false;
      }
    }
  } while (ret > 0);
  return false; /* NEVER REACHED */
}

void TcpConn::onEvent(Task evt) {

#ifdef THREADED_POLLING
  if (inEvent_.test_and_set())
    return;
  AtomicUnlock ulock(inEvent_);
#endif

  assert(fd_ != -1);
  if (evt & Task::HUP || evt & Task::ERR) {
    if (state_ == State::CONNECTING) {
      state_ = State::WAIT_CONNECT;
      ERROR(CONNECT);
      return;
    }
    // Pass it
  }
  if (evt & Task::IN) {
    assert(state_ == State::CONNECTED);
    size_t total_read = 0;
    while (true) {
      Chain::Buffer buf = input_.readTo(g_pagesize << readWinShift_);
      int ret = recv(fd_, buf.buf, buf.size, flags_);
      DLOG(DEBUG) << "read ..." << buf.size << " ret= " << ret;
      if (ret > 0) {
        total_read += ret;
        input_.fill(ret);
        if (ret < buf.size) {
          break;
        }
        ++readWinShift_;
      } else if (ret == 0) {
        output_.clear();
        closeNow();
        return;
      } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          ERROR(READ);
          return;
        }
        break;
      }
    }
    if (readWinShift_) {
      --readWinShift_;
    }
    if (dlgt_) dlgt_->onRead(this, total_read);
  }
  if (evt & Task::OUT) {
    // Connecting...
    if (state_ == State::CONNECTING) {
      int sockstat = 0;
      socklen_t err = sizeof(sockstat);
      if (0 != getsockopt(fd_, SOL_SOCKET, SO_ERROR, &sockstat, &err)
          || sockstat != 0) {
        tryConnect();
        return;
      }
      assert(rp_);
      state_ = State::CONNECTED;
      memcpy(&remoteAddr_, rp_->ai_addr, rp_->ai_addrlen);
      socklen_t size = rp_->ai_addrlen;
      assert(0 == getsockname(fd_, (struct sockaddr*)&localAddr_, &size));
      if (dlgt_) dlgt_->onConnected(this);
      return;
    }
    // Connected
    if (state_ == State::CONNECTED || state_ == State::FLUSHING) {
      (void)directWrite();
    }
  }
}
