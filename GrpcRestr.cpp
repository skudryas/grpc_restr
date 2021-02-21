#include "GrpcRestr.hpp"
#include <sstream>

GrpcStream* GrpcRestrProvider::newStream(Http2Stream* stream)
{
  DLOG(ALERT) << "newStream headers:";
  for (auto &i : stream->headers()) {
    DLOG(INFO) << i.first << " -> " << i.second;
  }
  auto path = stream->headers().find(":path");
  if (path == stream->headers().end()) {
    stream404_.onCreated(stream);
    return &stream404_;
  } else if (path->second == "/mbproto.MessageBroker/Produce") {
    producer_.onCreated(stream);
    return &producer_;
  } else if (path->second == "/mbproto.MessageBroker/Consume") {
    GrpcRestrStreamCons *cons = new GrpcRestrStreamCons(stream, repl_, this);
    auto ret = consumers_.emplace(cons);
    assert(ret.second);
    cons->onCreated(stream);
    return cons;
  } else {
    stream404_.onCreated(stream);
    return &stream404_;
  }
}

void GrpcRestrProvider::onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code)
{
  DLOG(INFO) << "got accept error: " << (int)error;
}

void GrpcRestrProvider::removeConsumer(GrpcRestrStreamCons *cons)
{
  consumers_.erase(cons);
  delete cons;
}

/// producer

void GrpcRestrStreamProd::onCreated(Http2Stream *stream)
{
  bool ch = checkHeaders(stream);
  bool wh = writeHeaders(stream, 200, false);
}

void GrpcRestrStreamProd::onError(Http2Stream *stream, const Http2StreamError& error)
{
  DLOG(INFO) << "got error: " << (int) error;
}

void GrpcRestrStreamProd::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  if (buf.size == 0)
    return;
  while (true) {
    Chain::Buffer pb = readData(stream, buf);
    if (!pb.size)
      break;
  
    std::string sv((char*)pb.buf, pb.size);
    if (!request_.ParseFromString(sv))
      return;
  
    DLOG(INFO) << "got PRODUCE:" << request_.key();
    repl_.consume(request_.key(), request_.payload());
  }
}

void GrpcRestrStreamProd::onTrailer(Http2Stream *stream)
{
  DLOG(INFO) << "onTrailer";
}

void GrpcRestrStreamProd::onClosed(Http2Stream *stream)
{
}

/// consumer

void GrpcRestrStreamCons::onCreated(Http2Stream *stream)
{
  bool ch = checkHeaders(stream);
  bool wh = writeHeaders(stream, 200, false);
}

void GrpcRestrStreamCons::onError(Http2Stream *stream, const Http2StreamError& error)
{
  prov_->removeConsumer(this);
  DLOG(INFO) << "got error: " << (int) error;
}

void GrpcRestrStreamCons::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  if (buf.size == 0)
    return;

  while (true) {
    Chain::Buffer pb = readData(stream, buf);
    if (!pb.size)
      break;

    std::string sv((char*)pb.buf, pb.size);
    if (!request_.ParseFromString(sv))
      return;
  
    switch (request_.action()) {
      case mbproto::ConsumeRequest::SUBSCRIBE:
        DLOG(INFO) << "got SUBSCRIBE:";
        repl_.subscribeBatch(&cons_, request_.keys());
        break;
      case mbproto::ConsumeRequest::UNSUBSCRIBE:
        DLOG(INFO) << "got UNSUBSCRIBE";
        repl_.unsubscribeBatch(&cons_, request_.keys());
        break;
      default:
        /* NO-OP */
        break;
    }
  }
}

void GrpcRestrStreamCons::onTrailer(Http2Stream *stream)
{
  /* NO-OP */
  DLOG(INFO) << "Cons: onTrailer";
}

void GrpcRestrStreamCons::onClosed(Http2Stream *stream)
{
  prov_->removeConsumer(this);
}

void GrpcRestrStreamCons::ConsumerWrapper::consume(const std::string &key, const std::string &data)
{
  DLOG(INFO) << "CONSUME!!!!!!";
  response_.set_key(key);
  response_.set_payload(data);
  tmpstr_.clear();
  response_.SerializeToString(&tmpstr_);
  Chain::Buffer outbuf;
  outbuf.buf = (uint8_t*)tmpstr_.data(); outbuf.size = tmpstr_.size();
  cons_.writeData(cons_.stream(), outbuf, false);
}

