#include "Http2Conn.hpp"
#include <iostream>
#include <algorithm>

#define ALLOW_ZERO_STREAM 1  // Some clients, like grpcurl, send zero stream...

std::string& str_tolower(std::string &s) {
    std::transform(s.begin(), s.end(), s.begin(), 
                   [](unsigned char c){ return std::tolower(c); }
                  );
    return s;
}

Http2Conn::Http2Conn(Loop *loop, Http2ConnDlgt *dlgt, int fd, struct sockaddr *remote,
           struct sockaddr *local, int flags) :
               conn_(std::unique_ptr<TcpConn>(new TcpConn(loop, this, fd, remote, local, flags))),
               dlgt_(dlgt), state_(State::CONNECTING), pingSent_(false)
{
  conn_->readSome();
  init();
}

Http2Conn::~Http2Conn() {
  DLOG(DEBUG) << "~Http2Conn";
  dlgt_ = nullptr;
  conn_->closeNow();
  nghttp2_hd_inflate_del(nghdinflater_);
  nghttp2_hd_deflate_del(nghddeflater_);
}

void Http2Conn::init() {
  assert(dlgt_ != nullptr);
  memset(pingBody_, *(char*)this, sizeof(pingBody_));
  (void)nghttp2_hd_inflate_new(&nghdinflater_);
  (void)nghttp2_hd_deflate_new(&nghddeflater_, 4096);
}

void Http2Conn::closeSoon(int code, const char *msg) {
  size_t len = (msg ? strlen(msg) : 0);
  Chain::Buffer out = rawOutput().readTo(len + 17);
  out.buf[0] = len >> 16; out.buf[1] = 0xff & (len >> 8); out.buf[2] = 0xff & len;
  out.buf[3] = (uint8_t)FrameType::GOAWAY; out.buf[4] = 0x0;
  out.buf[5] = out.buf[6] = out.buf[7] = out.buf[8] = 0;
  out.buf[9] = 0x7f; out.buf[10] = out.buf[11] = out.buf[12] = 0xff;
  out.buf[13] = code >> 24; out.buf[14] = (code >> 16) & 0xff;
  out.buf[15] = (code >> 8) & 0xff; out.buf[16] = code & 0xff;
  memcpy(&out.buf[17], msg, len);
  rawOutput().fill(len + 17);
  conn_->writeSome();
  handleClose(); // XXX Don't gracefully wait GOAWAY, just close
}

void Http2Conn::closeSoon() {
static const uint8_t close_conn_hardcode[17] = { 0x0, 0x0, 0x8, /* 24-bit length */
                                           (uint8_t)FrameType::GOAWAY, 0x0,
                                           0x0, 0x0, 0x0, 0x0, /* stream id */
                                           0x7f, 0xff, 0xff, 0xff, /* Max stream ID */
                                           0x0, 0x0, 0x0, 0x0 /* NO_ERROR */
                                              };
  Chain::Buffer ack_settings = rawOutput().readTo(17);
  memcpy(ack_settings.buf, close_conn_hardcode, 17);
  rawOutput().fill(17);
  conn_->writeSome();
  handleClose(); // XXX Don't gracefully wait GOAWAY, just close
}

bool Http2Conn::sendHeaders(Http2Stream *stream, const std::map<std::string, std::string>& headers, bool finish) {
  if (!stream
#ifndef ALLOW_ZERO_STREAM
      || !stream->streamId() 
#endif
      /* Can be not added yet, is't OK */
      /* stream != &streams_.find(stream->streamId())->second */ ) {
    return false;
  }
  if (stream->state() != Http2Stream::State::IDLE &&
      stream->state() != Http2Stream::State::RESERVED_LOCAL &&
      stream->state() != Http2Stream::State::RESERVED_REMOTE &&
      stream->state() != Http2Stream::State::OPEN &&
      stream->state() != Http2Stream::State::HALF_CLOSED_REMOTE) {
    return false;
  }
  nghttp2_nv *ngheaders = (nghttp2_nv*)alloca(sizeof(nghttp2_nv) * headers.size());
  size_t idx = 0;
  for (auto& i: headers) {
    ngheaders[idx].name = (uint8_t*)(i.first.data());
    ngheaders[idx].namelen = i.first.size();
    ngheaders[idx].value = (uint8_t*)(i.second.data());
    ngheaders[idx].valuelen = i.second.size();
    ++idx;
  }
  size_t buflen = nghttp2_hd_deflate_bound(nghddeflater_, ngheaders, idx);
  uint8_t *buf = (uint8_t*)alloca(buflen);
  int rv = nghttp2_hd_deflate_hd(nghddeflater_, buf, buflen, ngheaders, idx);
  if (rv < 0) {
    return false;
  }
  Chain::Buffer out = rawOutput().readTo(rv + 9);
  out.buf[0] = 0xff & (rv >> 16); out.buf[1] = 0xff & (rv >> 8); out.buf[2] = 0xff & rv;
  out.buf[3] = (uint8_t)FrameType::HEADERS; out.buf[4] = 0x4 | (finish ? 0x1 : 0x0);
  out.buf[5] = (stream->streamId() >> 24) & 0xff;
  out.buf[6] = (stream->streamId() >> 16) & 0xff;
  out.buf[7] = (stream->streamId() >> 8) & 0xff;
  out.buf[8] = stream->streamId() & 0xff;
  memcpy(&out.buf[9], buf, rv);
  rawOutput().fill(rv + 9);
  conn_->writeSome();
  if (finish)
    stream->state() = Http2Stream::State::HALF_CLOSED_LOCAL;
  return true;
}

bool Http2Conn::sendData(Http2Stream *stream, Chain::Buffer& buf, bool finish) {
  if (!stream || !stream->streamId() ||
      stream != &streams_.find(stream->streamId())->second)
    return false;
  if (stream->state() != Http2Stream::State::OPEN &&
      stream->state() != Http2Stream::State::HALF_CLOSED_REMOTE) {
    return false;
  }
  Chain::Buffer out = rawOutput().readTo(buf.size + 9);
  out.buf[0] = 0xff & (buf.size >> 16); out.buf[1] = 0xff & (buf.size >> 8); out.buf[2] = 0xff & buf.size;
  out.buf[3] = (uint8_t)FrameType::DATA; out.buf[4] = finish ? 0x1 : 0x0;
  out.buf[5] = (stream->streamId() >> 24) & 0xff;
  out.buf[6] = (stream->streamId() >> 16) & 0xff;
  out.buf[7] = (stream->streamId() >> 8) & 0xff;
  out.buf[8] = stream->streamId() & 0xff;
  memcpy(&out.buf[9], buf.buf, buf.size);
  rawOutput().fill(buf.size + 9);
  DLOG(INFO) << "SOMETHING WRITTEN!!!!!";
  conn_->writeSome();
  if (finish)
    stream->state() = Http2Stream::State::HALF_CLOSED_LOCAL;
  return true;

  return true;
}

void Http2Conn::onError(TcpConn *conn, TcpConnDlgt::Error error, int code)
{
  state_ = State::ERROR;
  lastError_ = Http2ConnError::CONNECTION;
  // XXX code unused.. Need ErrorChain!
  onClosed(conn);
}

void Http2Conn::onRead(TcpConn *conn, size_t)
{
  if (state_ == State::WAIT_CONNECT || state_ == State::CONNECTING) {
    // Check for preface
    static const char PREFACE_HTTP2[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    Chain& input = rawInput();
    if (input.size() >= sizeof(PREFACE_HTTP2) - 1) {
      Chain::Buffer buf = input.pullupFrom(sizeof(PREFACE_HTTP2) - 1);
      if (memcmp(buf.buf, PREFACE_HTTP2, sizeof(PREFACE_HTTP2) - 1) == 0) {
        DLOG(DEBUG) << "Got PRI";
        input.drain(sizeof(PREFACE_HTTP2) - 1);
      }
    }
  }

  while (parseFrame() > 0 && rawInput().size() > 0);
}

void Http2Conn::onWrite(TcpConn *conn, Chain::Buffer&& buf)
{
  // NO-OP
}

void Http2Conn::onConnected(TcpConn *conn)
{
  // NOTE: conn_ is not set at this time!
  // NO-OP
}

void Http2Conn::onClosed(TcpConn *conn)
{
  if (state_ == State::CLOSED) {
    if (dlgt_)
      dlgt_->onClosed(this);
  } else if (state_ == State::ERROR) { 
    if (dlgt_)
      dlgt_->onError(this, lastError_);
  } else {
    state_ = State::CLOSED;
    handleClose();
    if (dlgt_)
      dlgt_->onClosed(this);
  }
}

// -1 - error, 0 - need more, read size - otherwise
ssize_t Http2Conn::parseFrame()
{
  Chain& input = rawInput();
  DLOG(DEBUG) << "parseFrame (sz=" << input.size() << ")";
  if (input.size() < 9)
    return 0;
  Chain::Buffer buf = input.pullupFrom(9);
  size_t size = (buf.buf[0] << 16) + (buf.buf[1] << 8) + buf.buf[2];
  DLOG(DEBUG) << "parseFrame frame size = " << size << " buf[0][1][2] = " << (int)buf.buf[0] << " " << (int)buf.buf[1] << " " << (int)buf.buf[2] << " size = " << size;
  if (input.size() < 9 + size)
    return 0;
  buf = input.pullupFrom(9 + size);
  FrameType type = (FrameType)buf.buf[3];
  int flags = buf.buf[4];
  uint32_t stream_id = (buf.buf[5] << 24) + (buf.buf[6] << 16) + (buf.buf[7] << 8) + buf.buf[8];
  stream_id = stream_id & 0x7fffffff; // reserved bit MUST be ignored

  DLOG(DEBUG) << "Http2Conn::onRead: streamId = " << stream_id << ", type = " << (int)type << ", flags = " << flags << ", size = " << size;
#ifndef ALLOW_ZERO_STREAM
  if (stream_id == 0 && type != FrameType::SETTINGS && type != FrameType::PING && type != FrameType::GOAWAY) {
    handleError(Http2ConnError::INVALID);
    return -1;
  }
#endif
  input.drain(9);
  buf = input.pullupFrom(size);
  buf.size = size;
  auto streamIter = streams_.find(stream_id);

  if (streamIter == streams_.end() || streamIter->second.state() == Http2Stream::State::CLOSED) {
    if (type == FrameType::HEADERS || type == FrameType::PUSH_PROMISE) {
      streamIter = streams_.emplace(stream_id, Http2Stream(*this, stream_id)).first;
      streamIter->second.state() = Http2Stream::State::IDLE;
      DLOG(DEBUG) << "Emplaced stream with id == " << stream_id;
    } else if (stream_id != 0) {
      handleError(Http2ConnError::INVALID);
      return -1;
    }
  }

#ifndef ALLOW_ZERO_STREAM
#define HANDLE_UNEXPECTED_ZERO_STREAM \
    if (stream_id == 0) {            \
      handleError(Http2ConnError::INVALID);   \
      return -1;                     \
    }                                \
#else
#define HANDLE_UNEXPECTED_ZERO_STREAM ;
#endif

#define SHOULD_BE_CONNECTED_NON_ZERO \
  do {                               \
    if (state_ != State::CONNECTED) {\
      handleError(Http2ConnError::INVALID);   \
      return -1;                     \
    }                                \
  } while (0)                        \

#define SHOULD_BE_CONNECTED_ZERO     \
  do {                               \
    if (state_ != State::CONNECTED) {\
      handleError(Http2ConnError::INVALID);   \
      return -1;                     \
    }                                \
    HANDLE_UNEXPECTED_ZERO_STREAM;   \
  } while (0)                        \

#define SHOULD_BE_ZERO               \
  do {                               \
    if (stream_id != 0) {            \
      handleError(Http2ConnError::INVALID);   \
      return -1;                     \
    }                                \
  } while (0)                        \

#define SHOULD_BE_OPEN               \
  do {                               \
    if (stream.state() != Http2Stream::State::OPEN &&               \
        stream.state() != Http2Stream::State::HALF_CLOSED_LOCAL) {  \
      handleError(Http2ConnError::INVALID);                                  \
      return -1;                     \
    }                                \
  } while (0) 


#define GET_STREAM                                  \
  StreamIter __strIter = streams_.find(stream_id);  \
  if (streamIter == streams_.end()) {               \
    handleError(Http2ConnError::INVALID);                    \
    return -1;                                      \
  }                                                 \
  Http2Stream &stream = streamIter->second;

  switch (type) {
    case FrameType::DATA:
    {
      DLOG(DEBUG) << "got DATA frame";
      SHOULD_BE_CONNECTED_NON_ZERO;
      GET_STREAM;
      SHOULD_BE_OPEN;

      if (flags & 0x8) {
        ssize_t padding = buf.buf[0];
        buf = Chain::Buffer{&buf.buf[1], buf.size - 1 - padding};
      }

      DLOG(DEBUG) << "Calling onRead()";
      if (stream.dlgt()) {
        DLOG(DEBUG) << "Calling onRead() 2";
        stream.dlgt()->onRead(&stream, buf);
      }

      if (flags & 0x1) {
        if (stream.state() == Http2Stream::State::OPEN)
          stream.state() = Http2Stream::State::HALF_CLOSED_REMOTE;
        if (stream.state() == Http2Stream::State::HALF_CLOSED_LOCAL) {
          stream.state() = Http2Stream::State::CLOSED;
        }
        if (stream.dlgt())
          stream.dlgt()->onClosed(&stream);
      }
      break;
    }
    case FrameType::HEADERS:
    {
      SHOULD_BE_CONNECTED_NON_ZERO;
      GET_STREAM;
      if (stream.state() != Http2Stream::State::IDLE &&
          stream.state() != Http2Stream::State::RESERVED_LOCAL &&
          stream.state() != Http2Stream::State::OPEN &&
          stream.state() != Http2Stream::State::HALF_CLOSED_LOCAL) {
        DLOG(DEBUG) << "Invalid stream state: " << (int)stream.state();
        handleError(Http2ConnError::INVALID);
        return -1;
      }

      if (flags & 0x8) {
        ssize_t padding = buf.buf[0];
        buf = Chain::Buffer{&buf.buf[1], buf.size - 1 - padding};
      }
      if (flags & 0x20) {
        buf = Chain::Buffer{&buf.buf[5], buf.size - 5};
        // XXX Don't handle PRIORITY and DEPENDENCY
      }
      bool finished = flags & 0x4;
      if (finished) {
        DLOG(INFO) << "got HEADERS finished";
        // parse headers
        if (parseHeaders(stream, buf)) {
          if (stream.state() == Http2Stream::State::IDLE ||
              stream.state() == Http2Stream::State::RESERVED_LOCAL) {
            stream.state() = Http2Stream::State::OPEN;
          }
        } else {
          DLOG(INFO) << "got HEADERS - NOT parsed";
          return -1;
        }
      }

      if (flags & 0x1) {
        if (stream.state() == Http2Stream::State::OPEN ||
            stream.state() == Http2Stream::State::IDLE ||
            stream.state() == Http2Stream::State::RESERVED_LOCAL) {
          stream.state() = Http2Stream::State::HALF_CLOSED_REMOTE;
        }
        if (stream.state() == Http2Stream::State::HALF_CLOSED_LOCAL) {
          stream.state() = Http2Stream::State::CLOSED;
        }
        if (stream.dlgt())
          stream.dlgt()->onClosed(&stream);
      }

      if (!finished) {
        Chain::Buffer outbuf = stream.headersChain().readTo(buf.size);
        memcpy(outbuf.buf, buf.buf, buf.size);
        stream.headersChain().fill(buf.size);
      }
      break;
    }
    case FrameType::PRIORITY:
    {
      // XXX Don't handle
      break;
    }
    case FrameType::RST_STREAM:
    {
      SHOULD_BE_CONNECTED_NON_ZERO;
      GET_STREAM;

      if (size != 4) {
        handleError(Http2ConnError::INVALID);
        return -1;
      }
      if (stream.state() == Http2Stream::State::IDLE) {
        handleError(Http2ConnError::INVALID);
        return -1;
      }
      stream.state() = Http2Stream::State::CLOSED;
      if (stream.dlgt())
        stream.dlgt()->onClosed(&stream);
      break;
    }
    case FrameType::SETTINGS:
    {
      SHOULD_BE_ZERO;
      if (flags & 0x1) {
        state_ = State::CONNECTED;
        // XXX!!!
        //dlgt_->onConnected(this);
        break;
        // Just ACK received..
      }
      // Just ignoring and ACK-ing.. :)
static const uint8_t ack_settings_hardcode[9] = { 0x0, 0x0, 0x0, /* 24-bit length */
                                           (uint8_t)FrameType::SETTINGS,
                                           0x1, /* ACK Flag */
                                           0x0, 0x0, 0x0, 0x0 /* stream id */
                                         };
      Chain::Buffer ack_settings = rawOutput().readTo(9);
      memcpy(ack_settings.buf, ack_settings_hardcode, 9);
      rawOutput().fill(9);

#if 1
static const uint8_t settings_hardcode[] = {
  0x00, 0x00, 0x18, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
  0x1f, 0x40, 0x00, 0x00, 0x00, 0x05, 0x00, 0x40, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x00, 0x20, 0x00, 0xfe, 0x03, 0x00, 0x00, 0x00, 0x01 };

#else
static const uint8_t settings_hardcode[9] = { 0x0, 0x0, 0x0, /* 24-bit length */
                                           (uint8_t)FrameType::SETTINGS,
                                           0x0, /* ACK Flag */
                                           0x0, 0x0, 0x0, 0x0 /* stream id */
                                         };
#endif
      Chain::Buffer my_settings = rawOutput().readTo(
          sizeof(settings_hardcode));
      memcpy(my_settings.buf, settings_hardcode, sizeof(settings_hardcode));
      rawOutput().fill(sizeof(settings_hardcode));

#if 1
static const uint8_t window_update_max[13] = { 0x0, 0x0, 0x4, /* 24-bit length */
                                   (uint8_t)FrameType::WINDOW_UPDATE,
                                   0x0, /* Flags */
                                   0x0, 0x0, 0x0, 0x0, /* stream id */
                                   0x7f, 0x3f, 0x00, 0x01 };
      Chain::Buffer win_upd = rawOutput().readTo(13);
      memcpy(win_upd.buf, window_update_max, 13);
      rawOutput().fill(13);

#endif
      conn_->writeSome();
      break;
    }
    case FrameType::PUSH_PROMISE:
    {
      handleError(Http2ConnError::INVALID);
      return -1; // Just fuck off.. Don't support any push
    }
    case FrameType::PING:
    {
      SHOULD_BE_ZERO;
      if (flags & 0x1) {
        if (pingSent_ && (size != sizeof(pingBody_) || memcmp(buf.buf, pingBody_, sizeof(pingBody_)))) {
          handleError(Http2ConnError::INVALID);
          return -1;
        }
        pingSent_ = false;
        break;
        // Just ack received..
      } else {
static const uint8_t ack_pong_hardcode[9] = { 0x0, 0x0, 0x8, /* 24-bit length */
                                           (uint8_t)FrameType::PING,
                                           0x1, /* ACK Flag */
                                           0x0, 0x0, 0x0, 0x0 /* stream id */
                                         };
        Chain::Buffer ack_pong = rawOutput().readTo(9 + sizeof(pingBody_));
        memcpy(ack_pong.buf, ack_pong_hardcode, 9);
        memcpy(&ack_pong.buf[9], buf.buf, sizeof(pingBody_));
        rawOutput().fill(9 + sizeof(pingBody_));
        conn_->writeSome();
      }
      break;
    }
    case FrameType::GOAWAY:
    {
      SHOULD_BE_ZERO;
      handleClose();
      break;
    }
    case FrameType::WINDOW_UPDATE:
    {
      // Just fuck off...
      break;
    }
    case FrameType::CONTINUATION:
    {
      SHOULD_BE_CONNECTED_NON_ZERO;
      GET_STREAM;
      if (stream.state() != Http2Stream::State::IDLE &&
          stream.state() != Http2Stream::State::RESERVED_LOCAL &&
          stream.state() != Http2Stream::State::OPEN &&
          stream.state() != Http2Stream::State::HALF_CLOSED_LOCAL) {
        handleError(Http2ConnError::INVALID);
        return -1;
      }

      bool finished = flags & 0x4;

      Chain::Buffer outbuf = stream.headersChain().readTo(buf.size);
      memcpy(outbuf.buf, buf.buf, buf.size);
      stream.headersChain().fill(buf.size);
      if (finished) {
        Chain& headers = stream.headersChain();
        Chain::Buffer headers_buf = headers.pullupFrom(headers.size());
        if (parseHeaders(stream, headers_buf)) {
          if (stream.state() == Http2Stream::State::IDLE ||
              stream.state() == Http2Stream::State::RESERVED_LOCAL) {
            stream.state() = Http2Stream::State::OPEN;
          }
        } else {
          return -1;
        }
      }

      if (flags & 0x1) {
        if (stream.state() == Http2Stream::State::OPEN ||
            stream.state() == Http2Stream::State::IDLE ||
            stream.state() == Http2Stream::State::RESERVED_LOCAL) {
          stream.state() = Http2Stream::State::HALF_CLOSED_REMOTE;
        }
        if (stream.state() == Http2Stream::State::HALF_CLOSED_LOCAL) {
          stream.state() = Http2Stream::State::CLOSED;
        }
        if (stream.dlgt())
          stream.dlgt()->onClosed(&stream);
      }

      break;
    }
    default:
    {
      handleError(Http2ConnError::INVALID);
      return -1;
    }
  }
  input.drain(size);
  return size + 9;
}

bool Http2Conn::parseHeaders(Http2Stream &stream, Chain::Buffer& buf)
{
  nghttp2_nv nv_out;
  int inflate_flags = 0;
  while (buf.size > 0) {
    ssize_t parsed = nghttp2_hd_inflate_hd2(nghdinflater_,
        &nv_out, &inflate_flags,
        buf.buf, buf.size, 1);
    if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
      nghttp2_hd_inflate_end_headers(nghdinflater_);
      break;
    }

    if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
      assert(buf.size >= parsed);
      buf.buf += parsed;
      buf.size -= parsed;
      std::string name((char*)nv_out.name, nv_out.namelen);
      str_tolower(name);
      std::string value((char*)nv_out.value, nv_out.valuelen);
      if (name == "cookies") {
        stream.cookies().push_back(std::move(value));
      } else {
        stream.headers()[std::move(name)] = std::move(value);
      }
    }
    if (parsed == 0) {
      break;
    } else if (parsed < 0) {
      DLOG(DEBUG) << "inflate returned code " << parsed;
      handleError(Http2ConnError::INVALID);
      return false;
    }
  }
  if (stream.dlgt()) {
    // Trailer
    DLOG(INFO) << "calling onTrailer()";
    stream.dlgt()->onTrailer(&stream);
  } else {
    // Got stream
    DLOG(INFO) << "calling onStream()";
    dlgt_->onStream(this, &stream);
  }
  nghttp2_hd_inflate_end_headers(nghdinflater_);
  return true;
}

void Http2Conn::handleError(const Http2ConnError& e)
{
  // Connection error.. Should forward error to all streams first
  state_ = State::ERROR;
  for (auto& i: streams_) {
    if (i.second.dlgt())
      i.second.dlgt()->onError(&i.second, Http2StreamError::CONNECTION);
  }
  lastError_ = e;
  conn_->closeSoon();
}

void Http2Conn::handleClose()
{
  // Connection close.. Should forward close to all streams first
  if (state_ != State::CLOSED) {
    conn_->closeSoon();
  }
  state_ = State::CLOSED;
  for (auto& i: streams_) {
    i.second.state() = Http2Stream::State::CLOSED;
    if (i.second.dlgt())
      i.second.dlgt()->onClosed(&i.second);
  }
}

Http2Stream::~Http2Stream()
{
  if (dlgt_) {
    dlgt_->onClosed(this);
    dlgt_ = nullptr;
  }
}


