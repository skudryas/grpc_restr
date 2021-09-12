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
    LOG(ALERT) << "AsyncConsumed in: " << repl_.asyncConsumed();
    LOG(ALERT) << "Consumed in: " << repl_.consumed();
    LOG(ALERT) << "Forwarded out " << repl_.forwarded();
    LOG(ALERT) << "AsyncForwarded out " << repl_.asyncForwarded();
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
  DLOG(DEBUG) << "Readed buffer " << buf.size;
  while (true) {
    Chain::Buffer pb = readData(stream, buf);
    if (!pb.size) {
      DLOG(DEBUG) << "onRead() in producer stream: can't parse, buf.size=" << buf.size <<
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
      DLOG(DEBUG) << "onRead() in producer stream: parsed successfully, ByteSize()="
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
    {
/*      INIT_ELAPSED;
      DLOG(ALERT) << std::fixed << __start << " got PRODUCE:" << request_.key();*/
    }
#ifdef USE_MULTI_ACCEPT
    repl_.consume(request_.key(), request_.payload());
#else
    repl_.consumeAsync(request_.key(), request_.payload());
#endif
  }
}

void GrpcRestrStreamProd::onTrailer(Http2Stream *stream)
{
  DLOG(DEBUG) << "onTrailer";
}

void GrpcRestrStreamProd::onClosed(Http2Stream *stream)
{
  DLOG(INFO) << "got close id: " << stream->streamId();
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
#ifdef USE_MULTI_ACCEPT
        repl_.subscribeBatch(&cons_, request_.keys());
#else
        repl_.subscribeBatchAsync(&cons_, std::move(request_));
#endif
        break;
      case mbproto::ConsumeRequest::UNSUBSCRIBE:
        DLOG(INFO) << "got UNSUBSCRIBE";
#ifdef USE_MULTI_ACCEPT
        repl_.unsubscribeBatch(&cons_, request_.keys());
#else
        repl_.unsubscribeBatchAsync(&cons_, std::move(request_));
#endif
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
  DLOG(INFO) << "Cons: closed";
  prov_->removeConsumer(this);
}

void GrpcRestrStreamCons::ConsumerWrapper::consume(const std::string &key, const std::string &data)
{
/*  {
    INIT_ELAPSED;
    DLOG(ALERT) << std::fixed <<  __start << " consume pushed to async, key = " << key;
  }*/
#ifdef USE_SYNC_REPL
  mbproto::ConsumeResponse response;
  response.set_key(key);
  response.set_payload(data);
  std::string tmpstr;
  response.SerializeToString(&tmpstr);
  cons_.repl().incrementAsyncForwarded();
  Chain::Buffer outbuf;
  outbuf.buf = (uint8_t*)tmpstr.data(); outbuf.size = tmpstr.size();
  {
    std::unique_lock lock(mtx_);
    TcpConn::State state = cons_.stream()->conn().conn()->state();
    if (state == TcpConn::State::CONNECTED)
      cons_.writeData(cons_.stream(), outbuf, false);
    else
      std::cout << "write data in state " << (int)state << std::endl;
  }
#else
  async_.pushData(key, data);
#endif
}

void GrpcRestrStreamCons::AsyncConsumer::onError(Async *async, Error error, int code)
{
  assert(false);
}

void GrpcRestrStreamCons::AsyncConsumer::onAsync(Async *async)
{
#ifdef USE_CONCURRENT_QUEUE
  std::string tmp;
  while (cq_.try_dequeue(tmp)) {
    Chain::Buffer outbuf;
    outbuf.buf = (uint8_t*)tmp.data(); outbuf.size = tmp.size();
    cons_.writeData(cons_.stream(), outbuf, false);
  }
#else
  std::list<std::string> l;
  {
    std::unique_lock lock(mtx_);
    l.splice(l.begin(), queue_);
  }
  for (auto &i : l) {
/*    {
      const char *p = i.data() + 5;
      std::string tmp;
      if (i.size() > 5 + 6) {
        tmp.append(p, 6);
      }
      INIT_ELAPSED;
      DLOG(ALERT) << std::fixed << __start << " on async, key = " << tmp;
    }*/
    cons_.repl().incrementAsyncForwarded();
    Chain::Buffer outbuf;
    outbuf.buf = (uint8_t*)i.data(); outbuf.size = i.size();
    cons_.writeData(cons_.stream(), outbuf, false);
  }
  l.clear();
#endif
}


void GrpcRestrStreamCons::AsyncConsumer::pushData(const std::string &key, const std::string &data)
{
  if (async_.disabled())
    return;
  mbproto::ConsumeResponse response;
  response.set_key(key);
  response.set_payload(data);
#ifdef USE_CONCURRENT_QUEUE
  tmpstr_.clear();
  response.SerializeToString(&tmpstr_);
  cq_.enqueue(std::move(tmpstr_));
#else
  {
    std::unique_lock lock(mtx_);
    tmpstr_.clear();
    response.SerializeToString(&tmpstr_);
    queue_.emplace_back(std::move(tmpstr_));
  }
#endif
  async_.setAsync();
}

