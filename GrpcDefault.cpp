#include "GrpcDefault.hpp"
#include <sstream>

#include "default.pb.h"

GrpcStream* GrpcDefaultProvider::newStream(Http2Stream* stream)
{
  DLOG(INFO) << "newStream headers:";
  for (auto &i : stream->headers()) {
    DLOG(INFO) << i.first << " -> " << i.second;
  }
  auto path = stream->headers().find(":path");
  if (path == stream->headers().end() || path->second != "/example.Example/Rpc") {
    s404_.onCreated(stream);
    return &s404_;
  } else {
    se_.onCreated(stream);
    return &se_;
  }
}

void GrpcDefaultProvider::onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code)
{
  DLOG(INFO) << "got accept error: " << (int)error;
}

/// default

void GrpcDefaultStream::onCreated(Http2Stream* stream)
{
  /* NO-OP */
}

void GrpcDefaultStream::onError(Http2Stream *stream, const Http2StreamError& error)
{
  DLOG(INFO) << "got error: " << (int) error;
}

void GrpcDefaultStream::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  std::stringstream ss;
  for (size_t i = 0; i < buf.size; ++i) {
    ss << std::hex << (int)buf.buf[i] << " ";
  }
  DLOG(INFO) << "onRead size = " << buf.size << " -> " << ss.str();
}

void GrpcDefaultStream::onTrailer(Http2Stream *stream)
{
  DLOG(INFO) << "onTrailer";
}

void GrpcDefaultStream::onClosed(Http2Stream *stream)
{
}

// 404

void Grpc404Stream::onError(Http2Stream *stream, const Http2StreamError& error)
{
  DLOG(INFO) << "got error: " << (int) error;
}

void Grpc404Stream::onCreated(Http2Stream* stream)
{
  bool ch = checkHeaders(stream);
  bool wh = writeHeaders(stream, 404, true);
  std::cout << "Write 404: ch = " << (ch ? "YES" : "NO") << " wh = " << (wh ? "YES" : "NO") << std::endl;
}

void Grpc404Stream::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  /* NO-OP */
}

void Grpc404Stream::onTrailer(Http2Stream *stream)
{
  /* NO-OP */
  DLOG(INFO) << "404: onTrailer";
}

void Grpc404Stream::onClosed(Http2Stream *stream)
{
  /* NO-OP */
}

// example

void GrpcExampleStream::onCreated(Http2Stream* stream)
{
}

void GrpcExampleStream::onError(Http2Stream *stream, const Http2StreamError& error)
{
  DLOG(INFO) << "got error: " << (int) error;
}

/// default

void GrpcExampleStream::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  if (buf.size == 0) {
    DLOG(DEBUG) << "Got empty DATA message";
  }
  
  while (true) {
    Chain::Buffer pb = readData(stream, buf);
    if (!pb.size)
      break;
    example::Request request;
    std::string sv((char*)pb.buf, pb.size);
    request.ParseFromString(sv);
  
    DLOG(INFO) << "GOT MESSAGE " << request.requesttext();  
  }
  bool ch = checkHeaders(stream);
  bool wh = writeHeaders(stream, 200, false);
  std::cout << "Write 200: ch = " << (ch ? "YES" : "NO") << " wh = " << (wh ? "YES" : "NO") << std::endl;
  example::Response response;
  response.set_responsetext("HELLO, WORLD!!!");
  std::string tmp;
  response.SerializeToString(&tmp);
  Chain::Buffer outbuf;
  outbuf.buf = (uint8_t*)tmp.data(); outbuf.size = tmp.size();
  writeData(stream, outbuf, true);

//  bool wt = writeTrailers(stream);
}

void GrpcExampleStream::onTrailer(Http2Stream *stream)
{
  DLOG(INFO) << "onTrailer";
}

void GrpcExampleStream::onClosed(Http2Stream *stream)
{
}
