#include "TcpConn.hpp"
#include "TcpAccept.hpp"
#include <iostream>
#include <unordered_map>
#include <memory>

class EchoActor: public TcpConnDlgt {
  public:
    virtual void onError(TcpConn *conn, Error error, int code) override;
    virtual void onRead(TcpConn *conn, size_t) override;
    virtual void onWrite(TcpConn *conn, Chain::Buffer&& buf) override;
    virtual void onConnected(TcpConn *conn) override;
    virtual void onClosed(TcpConn *conn) override;
    void addConn(std::unique_ptr<TcpConn> &&conn) {
      conns_.emplace(conn.get(), std::move(conn));
    }
  private:
    std::unordered_map<TcpConn*, std::unique_ptr<TcpConn> > conns_;
};

void EchoActor::onError(TcpConn *conn, Error error, int code) {
  DLOG(DEBUG) << "Echo actor error: " << error << " code: "
            << strerror(code);
}

void EchoActor::onRead(TcpConn *conn, size_t count) {
  DLOG(DEBUG) << "onRead";
  while (count) {
    Chain::Buffer src = conn->input().writeFrom(count);
    Chain::Buffer dst = conn->output().readTo(src.size);
    if (src.size >= 4 && memcmp(src.buf, "stop", 4) == 0) {
      DLOG(DEBUG) << "STOP";
      conn->loop()->stop();
      return;
    }
    memcpy(dst.buf, src.buf, src.size);
    conn->output().fill(src.size);
    conn->input().drain(src.size);
    count -= src.size;
  }
  conn->writeSome();
}

void EchoActor::onWrite(TcpConn *conn, Chain::Buffer&& buf) {
  DLOG(DEBUG) << "onWrite: " << buf.size;
  // no op
}

void EchoActor::onConnected(TcpConn *conn) {
  DLOG(DEBUG) << "onConnected";
  conn->readSome();
}

void EchoActor::onClosed(TcpConn *conn) {
  DLOG(DEBUG) << "onClose";
  conns_.erase(conn);
}

class EchoServ: public TcpAcceptDlgt {
  public:
    EchoServ(EchoActor &a) : a_(a) {}
    virtual void onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr) override;
    virtual void onError(TcpAccept *acc, Error error, int code) override;
  private:
    EchoActor &a_;
};

void EchoServ::onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr) {
  a_.addConn(std::make_unique<TcpConn>(acc->loop(), &a_, fd, remoteAddr, localAddr));
}

void EchoServ::onError(TcpAccept *acc, Error error, int code) {
  DLOG(DEBUG) << "Echo server error: " << error << " code: "
            << strerror(code);
}

int main(int argc, char **argv)
{
  const char *host = (argc > 1 ? argv[1] : "0.0.0.0"),
             *port = (argc > 2 ? argv[2] : "8001");
  Loop l;
  EchoActor t;
  EchoServ e(t);
  TcpAccept a(&l, &e, host, port, 10, 0);  
  l.run();
}
