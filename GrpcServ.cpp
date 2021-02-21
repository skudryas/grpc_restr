#include "GrpcServ.hpp"

static void variant5_encode(uint8_t *p, uint32_t val);
static uint32_t variant5_decode(uint8_t *p);

void GrpcServ::GrpcAccept::onAccept(TcpAccept *acc, int fd,
        struct sockaddr *localAddr, struct sockaddr *remoteAddr)
{
  auto conn = std::make_unique<Http2Conn>(acc->loop(), &serv_, fd, remoteAddr, localAddr);
  serv_.gotConn(std::move(conn));
}

void GrpcServ::GrpcAccept::onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) {
  DLOG(ERROR) << "Grpc server error: " << error << " code: "
            << strerror(code);
  serv_.gotError(acc, error, code);
}

void GrpcServ::onError(Http2Conn *conn, const Http2ConnError& error)
{
  assert(1 == conns_.erase(conn));
}

void GrpcServ::onStream(Http2Conn *conn, Http2Stream *stream)
{
  GrpcStream *grpc_stream = provider_->newStream(stream);
  if (grpc_stream)
    stream->setDlgt(grpc_stream);
}

void GrpcServ::onClosed(Http2Conn *conn)
{
  assert(1 == conns_.erase(conn));
}

void GrpcServ::gotConn(std::unique_ptr<Http2Conn>&& conn)
{
  assert(conns_.emplace(std::make_pair(conn.get(), std::move(conn))).second);
}

void GrpcServ::gotError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code)
{
  provider_->onError(acc, error, code);
}

bool GrpcStream::checkHeaders(Http2Stream *stream)
{
  // Service-Name → ?( {proto package name} "." ) {service name}
  // Message-Type → {fully qualified proto message name}

  auto& headers = stream->headers();
  auto content_type = headers.find("content-type");
  if (content_type == headers.end() ||
      (content_type->second != "application/grpc+proto" &&
       content_type->second != "application/grpc"))
    return false;
  auto service_name = headers.find("service-name");
  if (service_name != headers.end()) {
    service_name_ = service_name->second;
  }
  auto message_type = headers.find("message-type");
  if (message_type != headers.end()) {
    message_type_ = message_type->second;
  }
  return true;
}

bool GrpcStream::writeHeaders(Http2Stream *stream, int code, bool doclose)
{
  std::map<std::string, std::string> headers =
  {
    {":status", std::to_string(code)},
    {"content-type", "application/grpc+proto"},
    {"server", "grpcc"},
  };

  return stream->conn().sendHeaders(stream, headers, doclose);
}

bool GrpcStream::writeTrailers(Http2Stream *stream)
{
  std::map<std::string, std::string> headers;
  return stream->conn().sendHeaders(stream, headers, true);
}

// 1 byte flag - encoded or not
// 4 bytes - uint32 in big endian
#define LENGTH_PREFIX_LENGTH 5

static void variant5_encode(uint8_t *p, uint32_t val)
{
  memset(p, 0, LENGTH_PREFIX_LENGTH);
  int idx = LENGTH_PREFIX_LENGTH;
  while (val && idx) {
    p[idx - 1] = val & 0xff;
    val = val >> 8;
  }
}

static uint32_t variant5_decode(uint8_t *p) // unsafe!
{
  uint32_t ret = 0;
  for (int idx = 0; idx < LENGTH_PREFIX_LENGTH; ++idx) {
    ret = (ret << 8);
    ret |= p[idx];
  }
  return ret;
}

void GrpcStream::writeData(Http2Stream *stream, Chain::Buffer &buf, bool doclose) // write length-prepended data, length in variant encoding
{
  uint8_t *p = (uint8_t*)alloca(LENGTH_PREFIX_LENGTH + buf.size);
  memcpy(&p[LENGTH_PREFIX_LENGTH], buf.buf, buf.size);
  Chain::Buffer outbuf;
  variant5_encode(p, (uint32_t)buf.size);
  outbuf.buf = p; outbuf.size = buf.size + LENGTH_PREFIX_LENGTH;
  stream->conn().sendData(stream, outbuf, doclose);
}

Chain::Buffer GrpcStream::readData(Http2Stream *stream, Chain::Buffer &buf) // read length-prepended data, length in variant encoding
{
  Chain::Buffer ret{nullptr, 0};
  if (buf.size != 0) {
    Chain::Buffer lpm_tail = lpm_.readTo(buf.size);
    memcpy(lpm_tail.buf, buf.buf, buf.size);
    lpm_.fill(lpm_tail.buf);
  }
  buf = lpm_.pullupAll();

  if (lpm_size_ == 0) {
    if (lpm_.size() < LENGTH_PREFIX_LENGTH)
      return ret;
    lpm_size_ = (size_t)variant5_decode(buf.buf);
    buf.buf += LENGTH_PREFIX_LENGTH;
    buf.size -= LENGTH_PREFIX_LENGTH;
    lpm_.drain(LENGTH_PREFIX_LENGTH);
  }

  ret.buf = buf.buf + LENGTH_PREFIX_LENGTH;
  ret.size = std::min(buf.size - LENGTH_PREFIX_LENGTH, size);


  buf.buf = buf.buf + LENGTH_PREFIX_LENGTH + ret.size;
  buf.size = buf.size - LENGTH_PREFIX_LENGTH - ret.size;
  return ret;
}

GrpcStream::~GrpcStream()
{
}

