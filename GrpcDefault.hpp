#pragma once

#include "GrpcServ.hpp"

// Just write 404
// XXX template for any code. please!
class Grpc404Stream: public GrpcStream
{
  public:
    virtual void onCreated(Http2Stream* stream) override;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override;
    virtual void onTrailer(Http2Stream *) override;
    virtual void onClosed(Http2Stream *) override;
    virtual ~Grpc404Stream() override {};
};

// Just a sceletone
class GrpcDefaultStream: public GrpcStream
{
  public:
    virtual void onCreated(Http2Stream* stream) override;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override;
    virtual void onTrailer(Http2Stream *) override;
    virtual void onClosed(Http2Stream *) override;
    virtual ~GrpcDefaultStream() override {};
};

// Stream for example.proto: "/example.Example/Rpc"
class GrpcExampleStream: public GrpcStream
{
  public:
    virtual void onCreated(Http2Stream* stream) override;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override;
    virtual void onTrailer(Http2Stream *) override;
    virtual void onClosed(Http2Stream *) override;
    virtual ~GrpcExampleStream() override {};
};

class GrpcDefaultProvider: public GrpcStreamProvider
{
  private:
    GrpcExampleStream se_;
    Grpc404Stream s404_;
  public:
    virtual GrpcStream* newStream(Http2Stream* stream) override;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
    // void registerPath(const std::string &path); // TODO!
};
