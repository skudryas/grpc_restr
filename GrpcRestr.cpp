#include "GrpcRestr.hpp"
#include <sstream>

GrpcStream* GrpcRestrProvider::newStream(Http2Stream* stream)
{
  DLOG(DEBUG) << "newStreamheaders:";
  for (auto &i : stream->headers()) {
    DLOG(DEBUG) << i.first << " -> " << i.second;
  }
  auto path = stream->headers().find(":path");
  if (path == stream->headers().end()) {
    stream404_.onCreated(stream);
    return &stream404_;
  } else if (path->second == "/mbproto.MessageBroker/Produce") {
    GrpcRestrStreamProd *prod = new GrpcRestrStreamProd(stream, repl_, this);
    auto ret = producers_.emplace(prod);
    assert(ret.second);
    prod->onCreated(stream);
    return prod;
  } else if (path->second == "/mbproto.MessageBroker/Consume") {
#ifdef GRPC_RESTR_PROFILE
    if (!profiling_ && consumers_.empty()) {
      ProfilerStart("/tmp/grpc_restr.prof");
      profiling_ = true;
    }
#endif
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
#ifdef GRPC_RESTR_PROFILE
  if (profiling_ && consumers_.empty()) {
    ProfilerStop();
    profiling_ = false;
  }
#endif
}

void GrpcRestrProvider::removeProducer(GrpcRestrStreamProd *prod)
{
  producers_.erase(prod);
  delete prod;
}

/// producer

void GrpcRestrStreamProd::onCreated(Http2Stream *stream)
{
  LOG(INFO) << "Created producer " << this;
  bool ch = checkHeaders(stream);
  bool wh = writeHeaders(stream, 200, false);
}

void GrpcRestrStreamProd::onError(Http2Stream *stream, const Http2StreamError& error)
{
  DLOG(INFO) << "got error: " << (int) error;
  prov_->removeProducer(this);
}

void GrpcRestrStreamProd::onRead(Http2Stream *stream, Chain::Buffer& buf)
{
  if (buf.size == 0)
    return;
  DLOG(INFO) << "Readed buffer " << buf.size;
  while (true) {
    Chain::Buffer pb = readData(stream, buf);
    if (!pb.size) {
      DLOG(INFO) << "onRead() in producer stream: can't parse, buf.size=" << buf.size <<
        " lpm size=" << lpm_.size() << " lpm= " << lpm_size_;
      break;
    }
#if 1
    Chain::StreamBuf sb(lpm_);
    std::istream is(&sb);
    if (!request_.ParseFromIstream(&is)) {
      lpm_.drain(pb.size); // !!!
      lpm_size_ = 0;
      return;
    } else {
      lpm_size_ = 0;
      DLOG(INFO) << "onRead() in producer stream: parsed successfully, ByteSize()="
        << request_.ByteSizeLong() << " pb.size=" << pb.size << " lpm size=" << lpm_.size() << " lpm= " << lpm_size_;
    }
#else
    std::string sv((char*)pb.buf, pb.size);
    lpm_.drain(pb.size);
    lpm_size_ = 0;

    if (!request_.ParseFromString(sv)) {
      return;
    } else {
      lpm_.drain(pb.size);
      lpm_size_ = 0;
      DLOG(INFO) << "onRead() in producer stream: parsed successfully, ByteSize()="
        << request_.ByteSizeLong() << " pb.size=" << pb.size << " lpm size=" << lpm_.size() << " lpm= " << lpm_size_;
    }
#endif
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
  prov_->removeProducer(this);
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
    lpm_.drain(pb.size);
    lpm_size_ = 0;

    if (!request_.ParseFromString(sv))
      return;
  
    switch (request_.action()) {
      case mbproto::ConsumeRequest::SUBSCRIBE:
        DLOG(INFO) << "got SUBSCRIBE:";
        for (auto &i : request_.keys()) {
          DLOG(DEBUG) << i;
        }
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
  DLOG(DEBUG) << "Cons: closed";
}

void GrpcRestrStreamCons::ConsumerWrapper::consume(const std::string &key, const std::string &data)
{
#ifdef THREADED_POLLING
  std::unique_lock lock(wmtx_);
#endif
  DLOG(DEBUG) << "CONSUME!!!!!!";
  response_.set_key(key);
  response_.set_payload(data);
  tmpstr_.clear();
  response_.SerializeToString(&tmpstr_);
  Chain::Buffer outbuf;
  outbuf.buf = (uint8_t*)tmpstr_.data(); outbuf.size = tmpstr_.size();
  cons_.writeData(cons_.stream(), outbuf, false);
}

