#pragma once

#include "GrpcDefault.hpp"
#include "Event.hpp"
#include "GrpcRepl.h"

#ifdef USE_CONCURRENT_QUEUE
#include "concurrentqueue.h"
#endif

#ifdef GRPC_RESTR_PROFILE
#include <gperftools/profiler.h>
#endif

#include "rtkmb.pb.h"

class GrpcRestrProvider;

// Stream for rtkmb.proto: "/mbproto.MessageBroker/Produce"
class GrpcRestrStreamProd: public GrpcStream
{
  private:
    Repl::GrpcRepl<mbproto::ConsumeRequest> &repl_;
    mbproto::ProduceRequest request_;
    GrpcRestrProvider *prov_;
    Http2Stream *stream_;
  public:
    GrpcRestrStreamProd(Http2Stream *stream, Repl::GrpcRepl<mbproto::ConsumeRequest> &repl, GrpcRestrProvider *prov):
      stream_(stream), repl_(repl), prov_(prov) {}
    ~GrpcRestrStreamProd()
    {
      stream_->setDlgt(nullptr);
    }
    virtual void onCreated(Http2Stream* stream) override;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override;
    virtual void onTrailer(Http2Stream *) override;
    virtual void onClosed(Http2Stream *) override;
};

// Stream for rtkmb.proto: "/mbproto.MessageBroker/Consume"
class GrpcRestrStreamCons: public GrpcStream
{
  private:
    struct EventConsumer: public EventDlgt
    {
      GrpcRestrStreamCons &cons_;
      std::string tmpstr_;
      Event event_;
      EventConsumer(GrpcRestrStreamCons &cons, Loop *loop): cons_(cons), event_(loop, this) {}
      virtual void onError(Event *event, Error error, int code) override;
      virtual void onEvent(Event *event) override;
      void pushData(std::string&& data);
      void disableEvent() { event_.disableEvent(); }
#ifdef USE_CONCURRENT_QUEUE
      moodycamel::ConcurrentQueue<std::string> cq_;
#else
      std::mutex mtx_;
      std::list<std::string> queue_;
#endif
    };
    struct ConsumerWrapper: public Repl::Consumer
    {
      GrpcRestrStreamCons &cons_;
      EventConsumer &event_;
      ConsumerWrapper(GrpcRestrStreamCons &cons, EventConsumer &event): cons_(cons), event_(event),
            Repl::Consumer(SERV_THREAD_NUM + 1) {}
      virtual ~ConsumerWrapper()
      {
      }
      virtual void consume(const std::string &key, const Chain::Buffer &data) override;
    };
    EventConsumer event_;
    ConsumerWrapper cons_;
    Http2Stream *stream_;
    Repl::GrpcRepl<mbproto::ConsumeRequest> &repl_;
    mbproto::ConsumeRequest request_;
    GrpcRestrProvider *prov_;
  public:
    GrpcRestrStreamCons(Http2Stream *stream, Repl::GrpcRepl<mbproto::ConsumeRequest> &repl, GrpcRestrProvider *prov):
      stream_(stream), repl_(repl), event_(*this, stream->conn().loop()), cons_(*this, event_), prov_(prov)
    {
#ifdef USE_MULTI_ACCEPT
      repl_.addConsumer(&cons_);
#else
      repl_.addConsumerEvent(&cons_);
#endif
    }
    ~GrpcRestrStreamCons() override
    {
      //event_.disableEvent();
      repl_.removeConsumer(&cons_); // SHOULD BE NON-ASYNC!
      stream_->setDlgt(nullptr);
    }
    // XXX replace to hash
    bool operator<(const GrpcRestrStreamCons &rhs) const
    {
      return stream_ < rhs.stream_;
    }
    Repl::GrpcRepl<mbproto::ConsumeRequest> &repl() { return repl_; }
    Http2Stream *stream() { return stream_; }
    virtual void onCreated(Http2Stream* stream) override;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override;
    virtual void onTrailer(Http2Stream *) override;
    virtual void onClosed(Http2Stream *) override;
};

class GrpcRestrProvider: public GrpcStreamProvider
{
  private:
    // Single producer handler for all streams but many consumer handlers for each stream
    Repl::GrpcRepl<mbproto::ConsumeRequest> &repl_;
    Grpc404Stream stream404_;
    std::set<GrpcRestrStreamProd*> producers_;
    std::set<GrpcRestrStreamCons*> consumers_;
  public:
    GrpcRestrProvider(Repl::GrpcRepl<mbproto::ConsumeRequest> &repl): repl_(repl) {}
    void removeConsumer(GrpcRestrStreamCons *cons);
    void removeProducer(GrpcRestrStreamProd *prod);
    virtual GrpcStream* newStream(Http2Stream* stream) override;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
};
