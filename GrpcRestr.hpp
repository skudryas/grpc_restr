#pragma once

#include "GrpcDefault.hpp"
#include "Repl.h"

#include "rtkmb.pb.h"

class GrpcRestrProvider;

// Stream for rtkmb.proto: "/mbproto.MessageBroker/Produce"
class GrpcRestrStreamProd: public GrpcStream
{
  private:
    Repl::Repl &repl_;
    mbproto::ProduceRequest request_;
  public:
    GrpcRestrStreamProd(Repl::Repl &repl): repl_(repl) {}
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
    struct ConsumerWrapper: public Repl::Consumer
    {
      GrpcRestrStreamCons &cons_;
      ConsumerWrapper(GrpcRestrStreamCons &cons): cons_(cons),
            Repl::Consumer(SERV_THREAD_NUM + 1) {}
      virtual ~ConsumerWrapper()
      {
      }
      virtual void consume(const std::string &key, const std::string &data) override;
      mbproto::ConsumeResponse response_;
      std::string tmpstr_;
    };
    ConsumerWrapper cons_;
    Http2Stream *stream_;
    Repl::Repl &repl_;
    mbproto::ConsumeRequest request_;
    GrpcRestrProvider *prov_;
  public:
    GrpcRestrStreamCons(Http2Stream *stream, Repl::Repl &repl, GrpcRestrProvider *prov):
      stream_(stream), repl_(repl), cons_(*this), prov_(prov)
    {
      repl_.addConsumer(&cons_);
    }
    ~GrpcRestrStreamCons() override
    {
      repl_.removeConsumer(&cons_);
      stream_->setDlgt(nullptr);
    }
    // XXX replace to hash
    bool operator<(const GrpcRestrStreamCons &rhs) const
    {
      return stream_ < rhs.stream_;
    }
    Repl::Repl &repl() { return repl_; }
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
    Repl::Repl &repl_;
    Grpc404Stream stream404_;
    GrpcRestrStreamProd producer_;
    std::set<GrpcRestrStreamCons*> consumers_;
  public:
    GrpcRestrProvider(Repl::Repl &repl): repl_(repl), producer_(repl) {}
    void removeConsumer(GrpcRestrStreamCons *cons);
    virtual GrpcStream* newStream(Http2Stream* stream) override;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
};
