#pragma once

#include "TcpConn.hpp"
#include <map>
#include <unordered_map>
#include <memory>
#include <nghttp2/nghttp2.h>

class Http2ConnDlgt;
class Http2StreamDlgt;

enum class Http2ConnError {
  CONNECTION = 0,
  INVALID,
};

enum class Http2StreamError {
  CONNECTION = 0,
  BAD_HEADERS,
};

class Http2Conn;

class Http2Stream
{
  public:
    Http2Stream(Http2Conn &conn, int streamId): conn_(conn), streamId_(streamId),
       dlgt_(nullptr), state_(State::IDLE) {}
    ~Http2Stream();
    uint32_t streamId() const { return streamId_; }
    Http2Conn &conn() { return conn_; }
    std::map<std::string, std::string>& headers() { return headers_; }
    std::vector<std::string>& cookies() { return cookies_; }
    enum class State {
      IDLE,
      RESERVED_LOCAL,
      RESERVED_REMOTE,
      OPEN,
      HALF_CLOSED_LOCAL,
      HALF_CLOSED_REMOTE,
      CLOSED,
    };
    State& state() { return state_; }
    Chain& headersChain() { return headersChain_; }
    void setDlgt(Http2StreamDlgt *dlgt) { dlgt_ = dlgt; }
    Http2StreamDlgt *dlgt() { return dlgt_; }

  private:
    State state_;
    Http2Conn &conn_; // weak
    Http2StreamDlgt *dlgt_; // weak
    uint32_t streamId_;
    std::map<std::string, std::string> headers_;
    std::vector<std::string> cookies_;
    Chain headersChain_;
};


class Http2Conn : public TcpConnDlgt {
  public:
    // ctr & dtr
    Http2Conn(Loop *loop, Http2ConnDlgt *dlgt, int fd, struct sockaddr *remote,
           struct sockaddr *local, int flags = 0);
    Http2Conn(Loop *loop, Http2ConnDlgt *dlgt, int flags = 0):
          conn_(std::unique_ptr<TcpConn>(new TcpConn(loop, this, flags))),
          dlgt_(dlgt), state_(State::WAIT_CONNECT), pingSent_(false) {
      init();
    }
    virtual ~Http2Conn();

    // API
    void connect(const char *host, const char *port) { /* TODO */}
    void closeSoon(); // use this
    void closeSoon(int code, const char *meg); // or this
    bool sendHeaders(Http2Stream *stream, const std::map<std::string, std::string>& headers, bool finished);
    bool sendData(Http2Stream *stream, Chain::Buffer& buf, bool finished);

    // Inline API
    enum class State;
    State& state() {
      return state_;
    }
    Chain& rawInput() {
      return conn_->input();
    }
    Chain& rawOutput() {
      return conn_->output();
    }
    Chain& input() {
      return input_; // XXX unused
    }
    Http2ConnDlgt *dlgt() { return dlgt_; }
    TcpConn* conn() { return conn_.get(); }

    // Enums
    enum class State {
      WAIT_CONNECT = 1,
      CONNECTING,
      CONNECTED,
      CLOSE_WAIT,
      CLOSED,
      ERROR,
    };

    enum class FrameType : int {
      DATA = 0x0,
      HEADERS = 0x1,
      PRIORITY = 0x2,
      RST_STREAM = 0x3,
      SETTINGS = 0x4,
      PUSH_PROMISE = 0x5,
      PING = 0x6,
      GOAWAY = 0x7,
      WINDOW_UPDATE = 0x8,
      CONTINUATION = 0x9,
    };

    enum class ErrorCode: int {
      NO_ERROR = 0x0, // The associated condition is not a result of an error. For example, a GOAWAY might include this code to indicate graceful shutdown of a connection.
      PROTOCOL_ERROR = 0x1, // The endpoint detected an unspecific protocol error. This error is for use when a more specific error code is not available.
      INTERNAL_ERROR = 0x2, // The endpoint encountered an unexpected internal error.
      FLOW_CONTROL_ERROR = 0x3, // The endpoint detected that its peer violated the flow-control protocol.
      SETTINGS_TIMEOUT = 0x4, // The endpoint sent a SETTINGS frame but did not receive a response in a timely manner. See Section 6.5.3 ("Settings Synchronization").
      STREAM_CLOSED = 0x5, // The endpoint received a frame after a stream was half-closed.
      FRAME_SIZE_ERROR = 0x6, // The endpoint received a frame with an invalid size.
      REFUSED_STREAM = 0x7, // The endpoint refused the stream prior to performing any application processing (see Section 8.1.4 for details).
      CANCEL = 0x8, // Used by the endpoint to indicate that the stream is no longer needed.
      COMPRESSION_ERROR = 0x9, // The endpoint is unable to maintain the header compression context for the connection.
      CONNECT_ERROR = 0xa, // The connection established in response to a CONNECT request (Section 8.3) was reset or abnormally closed.
      ENHANCE_YOUR_CALM = 0xb, // The endpoint detected that its peer is exhibiting a behavior that might be generating excessive load.
      INADEQUATE_SECURITY = 0xc, // The underlying transport has properties that do not meet minimum security requirements (see Section 9.2).
      HTTP_1_1_REQUIRED = 0xd, // The endpoint requires that HTTP/1.1 be used instead of HTTP/2.
    };

    // TcpConn delegate...
    virtual void onRead(TcpConn *conn, size_t) override;
    virtual void onWrite(TcpConn *conn, Chain::Buffer&& buf) override;
    virtual void onConnected(TcpConn *conn) override;
    // Can be safely deleted only on these invocations
    virtual void onClosed(TcpConn *conn) override;
    virtual void onError(TcpConn *conn, TcpConnDlgt::Error error, int code) override;

  private:
    void init();
    ssize_t parseFrame();
    void handleError(const Http2ConnError& e);
    void handleClose();
    bool parseHeaders(Http2Stream& stream, Chain::Buffer& buf);

    nghttp2_hd_inflater *nghdinflater_; // decoding headers
    nghttp2_hd_deflater *nghddeflater_; // encoding headers
    Http2ConnDlgt *dlgt_; // weak
    Http2ConnError lastError_;
    Chain input_; // XXX unused
    State state_;
    bool pingSent_;
    std::unique_ptr<TcpConn> conn_;
    std::unordered_map<uint32_t, Http2Stream> streams_;
    uint8_t pingBody_[8];
    using StreamIter = std::unordered_map<uint32_t, Http2Stream>::iterator;
};

class Http2ConnDlgt
{
  public:
    // XXX ErrorChain!
    virtual void onError(Http2Conn *conn, const Http2ConnError& error) = 0;
    virtual void onStream(Http2Conn *conn, Http2Stream *stream) = 0;
    virtual void onConnected(Http2Conn *conn) = 0;
    virtual void onClosed(Http2Conn *conn) = 0;
};

class Http2StreamDlgt
{
  public:
    // XXX ErrorChain!
    virtual ~Http2StreamDlgt() {}
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) = 0;
    virtual void onRead(Http2Stream *, Chain::Buffer&) = 0;
    virtual void onTrailer(Http2Stream *) = 0;
    virtual void onClosed(Http2Stream *) = 0;
};

