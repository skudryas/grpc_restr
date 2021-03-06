#pragma once

#include "Repl.h"
#include "Queue.hpp"

namespace Repl {

template <typename ReqCons>
class GrpcRepl: public Repl
{
  public:
    GrpcRepl(size_t num = 1): Repl(num), bbq_(16), asyncConsumed_(0), asyncForwarded_(0) {}
                         /*     totalCounter_(0), totalCounterAsync_(0) {}*/
    void loop()
    {
      while (true) {
        Elem e(bbq_.pop_front());
/*        if (e.idx != totalCounter_) {
          std::cout << "Failed check at e.idx = " << e.idx << " counter = " << totalCounter_ << " type = " << e.type << std::endl;
        }
        assert(e.idx == totalCounter_);
        ++totalCounter_;*/
        switch (e.type) {
          case Elem::CONS:
            assert(e.key.length() > 0);
            Chain::Buffer buf;
            buf.buf = (uint8_t*)e.data.data(); buf.size = e.data.size();
            consume(e.key, buf);
            break;
          case Elem::SUB:
            subscribeBatch(e.cons, e.sub.keys());
            break;
          case Elem::USUB:
            unsubscribeBatch(e.cons, e.sub.keys());
            break;
          case Elem::ADD:
            addConsumer(e.cons);
            break;
          case Elem::RM:
            removeConsumer(e.cons);
            break;
          default:
            assert(false);
        }
      }
    }
    void subscribeBatchAsync(Consumer *cons, const ReqCons &req)
    {
//      Elem e(totalCounterAsync_++);
      Elem e;
      e.type = Elem::SUB;
      e.sub.CopyFrom(req);
      e.cons = cons;
      bbq_.push_back(std::move(e));
    }
    void unsubscribeBatchAsync(Consumer *cons, const ReqCons &req)
    {
//      Elem e(totalCounterAsync_++);
      Elem e;
      e.type = Elem::USUB;
      e.sub.CopyFrom(req);
      e.cons = cons;
      bbq_.push_back(std::move(e));
    }
    void consumeAsync(const std::string &key, const Chain::Buffer &data)
    {
      DLOG(DEBUG) << "consAsync " << key;
      ++asyncConsumed_;
      Elem e;
//      Elem e(totalCounterAsync_++);
      e.type = Elem::CONS;
      e.key = std::move(key); e.data = std::string((char*)data.buf, data.size);
      bbq_.push_back(std::move(e));
    }
    void addConsumerAsync(Consumer *cons)
    {
      Elem e;
//      Elem e(totalCounterAsync_++);
      e.type = Elem::ADD;
      e.cons = cons;
      bbq_.push_back(std::move(e));
    }
    void removeConsumerAsync(Consumer *cons)
    {
      Elem e;
//      Elem e(totalCounterAsync_++);
      e.type = Elem::RM;
      e.cons = cons;
      bbq_.push_back(std::move(e));
    }
#ifdef REPL_PROFILE
    size_t asyncConsumed() const { return asyncConsumed_; }
    size_t asyncForwarded() const { return asyncForwarded_; }
#endif
    void incrementAsyncForwarded()
    {
#ifdef REPL_PROFILE
      ++asyncForwarded_;
#endif
    }
    virtual void startProfiling() override
    {
#ifdef REPL_PROFILE
      Repl::startProfiling();
#endif
    }
    virtual void stopProfiling() override
    {
#ifdef REPL_PROFILE
      Repl::stopProfiling();
      LOG(ALERT) << "AsyncConsumed in: " << asyncConsumed();
      LOG(ALERT) << "Consumed in: " << consumed();
      LOG(ALERT) << "Forwarded out " << forwarded();
      LOG(ALERT) << "AsyncForwarded out " << asyncForwarded();
#endif
    }
  private:
    struct Elem
    {
      enum Type {
        CONS = 0,
        SUB = 1,
        USUB = 2,
        ADD = 3,
        RM = 4
      };
//      Elem(size_t idx_ = 0): idx(idx_) {}
      Type type;
      ReqCons sub;
      std::string key;
      std::string data;
      Consumer *cons;
//      size_t idx;
    };
    BoundedBlockingQueue<Elem> bbq_;
    std::atomic<size_t> asyncConsumed_;
    std::atomic<size_t> asyncForwarded_;
/*    size_t totalCounter_;
    size_t totalCounterAsync_;*/
};

} // namespace Repl
