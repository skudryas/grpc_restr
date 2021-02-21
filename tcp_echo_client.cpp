#include "TcpConn.hpp"
#include <iostream>
#include <unordered_map>
#include <random>
#include <memory>

class EchoMgr;

class EchoChecker: public TcpConnDlgt {
  public:
    EchoChecker(EchoMgr *mgr, const std::string &host, const std::string &port);
    virtual void onError(TcpConn *conn, Error error, int code) override;
    virtual void onRead(TcpConn *conn, size_t) override;
    virtual void onWrite(TcpConn *conn, Chain::Buffer&& buf) override;
    virtual void onConnected(TcpConn *conn) override;
    virtual void onClosed(TcpConn *conn) override;
    enum Status {
      OK = 0,
      NUM_MISMATCH = 1,
      BYTES_MISMATCH = 2,
      TIMEOUT = 3,
      DROPPED = 4,
      CHECKED = 5,
      MAX = 6
    };
    Status status() const { return status_; }
  private:
    void genTestData();
    std::string host_, port_;
    EchoMgr *mgr_;
    TcpConn conn_;
    Chain chain_;
    Status status_;
};

class EchoMgr {
  public:
    EchoMgr(Loop *l, const std::string &host, const std::string &port,
        int pool, int total) : loop_(l), pool_(pool), total_(total),
         done_(0), host_(host), port_(port) {
      memset(status_, 0, sizeof(status_));
      onCheckerDone(nullptr);
    }
    void onCheckerDone(EchoChecker *checker) {
      if (checker) {
        ++status_[checker->status()];
        checkers_.erase(checker);
        ++done_;
        DLOG(DEBUG) << done_ << " checker, size = " << checkers_.size();
      }
      while (checkers_.size() < pool_ &&
          checkers_.size() + done_ < total_) {
        std::unique_ptr<EchoChecker> checker = std::make_unique<EchoChecker>
          (this, host_, port_);
        checkers_.emplace(checker.get(), std::move(checker));
      }
      if (done_ == total_ && checkers_.empty()) {
        loop_->stop();
        for (int i = 0; i < sizeof(status_) / sizeof(status_[0]); ++i) {
          LOG(ALERT) << i << " -> " << status_[i];
        }
      }
    }
    Loop *loop() { return loop_; }
  private:
    int status_[EchoChecker::Status::MAX];
    Loop *loop_;
    int pool_, total_, done_;
    std::string host_, port_;
    std::unordered_map<EchoChecker*, std::unique_ptr<EchoChecker> > checkers_;
};

EchoChecker::EchoChecker(EchoMgr *mgr, const std::string &host,
    const std::string &port): conn_(mgr->loop(), this),
    mgr_(mgr), status_(Status::OK), host_(host), port_(port) {
  genTestData();
  conn_.connect(host.c_str(), port.c_str());
}

void EchoChecker::onError(TcpConn *conn, Error error, int code) {
  if (error == Error::NOTAVAIL) {
    LOG(ALERT) << "NOTAVAIL for conn " << conn;
    conn_.connect(host_.c_str(), port_.c_str());
  } else {
    LOG(ALERT) << "Echo actor error: " << error << " code: "
            << strerror(code);
  }
}

void EchoChecker::onRead(TcpConn *conn, size_t count) {
  DLOG(DEBUG) << "onRead " << count;
  while (count) {
    Chain::Buffer src = conn->input().writeFrom(count);
    Chain::Buffer dst = chain_.writeFrom(src.size);
    size_t min = std::min(src.size, dst.size);
    DLOG(DEBUG) << "min = " << min;
    if (min == 0) {
      break;
    }
    if (0 != memcmp(src.buf, dst.buf, min)) {
      status_ = Status::BYTES_MISMATCH;
      break;
    }
    conn->input().drain(min);
    chain_.drain(min);
    count -= min;
  }
  if (chain_.size() == 0) {
    if (conn->input().size() != 0) {
      status_ = Status::NUM_MISMATCH;
    } else {
      if (status_ == Status::OK) {
        status_ = Status::CHECKED;
      }
    }
  }
  if (status_ != Status::OK) {
    conn->closeSoon();
  } else {
    DLOG(DEBUG) << "status = " << status_ << "chain: " << chain_.size() <<
      " input: " << conn->input().size();
//    conn->writeSome();
  }
}

void EchoChecker::onWrite(TcpConn *conn, Chain::Buffer&& buf) {
  DLOG(DEBUG) << "onWrite: " << buf.size;
  // no op
}

void EchoChecker::onConnected(TcpConn *conn) {
  DLOG(DEBUG) << "onConnected";
  conn_.output() = chain_;
  DLOG(DEBUG) << "output size = " << conn_.output().size() <<
    " chain size = " << chain_.size();
  conn->writeSome();
  conn->readSome();
}

void EchoChecker::onClosed(TcpConn *conn) {
  DLOG(DEBUG) << "onClose";
  mgr_->onCheckerDone(this);
}

void EchoChecker::genTestData() {
  std::random_device gen;
//  std::default_random_engine gen;
  std::uniform_int_distribution<int> distr_count(1, 10);
  std::uniform_int_distribution<uint8_t> distr_content(0, 255);
  std::uniform_int_distribution<int> distr_size(1, 66666);
  int chunk_count = distr_count(gen); //@@!
  for (int i = 0; i < chunk_count; ++i) {
    int chain_size = distr_size(gen); //@@!
    uint8_t chain_content = distr_content(gen);
    chain_.emplaceBack(std::vector<uint8_t>(chain_size, chain_content));
  }
}

int main(int argc, char **argv)
{
  const char *host = (argc > 1 ? argv[1] : "127.0.0.1"),
             *port = (argc > 2 ? argv[2] : "8001");
  int conn_pool = (argc > 3 ? atoi(argv[3]) : 128),
      conn_total = (argc > 4 ? atoi(argv[4]) : 65536);
  if (conn_pool <= 0 || conn_total <= 0) {
    return 0;
  }
  Loop l;
  EchoMgr e(&l, host, port, conn_pool, conn_total);
  l.run();
}
