#pragma once

#include "TcpAccept.hpp"
#include "Http2Conn.hpp"

class GrpcStream: public Http2StreamDlgt {
  public:
    virtual void onCreated(Http2Stream* stream) = 0;
    virtual void onError(Http2Stream *stream, const Http2StreamError& error) override = 0;
    virtual void onRead(Http2Stream *, Chain::Buffer&) override = 0;
    virtual void onTrailer(Http2Stream *) override = 0;
    virtual void onClosed(Http2Stream *) override = 0;
    GrpcStream(): lpm_size_(0) {}
    virtual ~GrpcStream() override;
    bool checkHeaders(Http2Stream *stream);
    bool writeHeaders(Http2Stream *stream, int code, bool doclose);
    bool writeTrailers(Http2Stream *stream);
    void writeData(Http2Stream *stream, Chain::Buffer &buf, bool doclose);
    Chain::Buffer readData(Http2Stream *stream, Chain::Buffer &buf);
    const std::string& serviceName() const { return service_name_; }
    const std::string& messageType() const { return message_type_; }
  protected:
    // GrpcStream extracted
    std::string service_name_;
    std::string message_type_;
    // Length-prefixed message, could be splitted by several DATA frames,
    // so we need to reconstruct GRPC message using this buffer
    Chain lpm_;
    size_t lpm_size_;
};

class GrpcStreamProvider {
  public:
    virtual GrpcStream* newStream(Http2Stream* stream) = 0;
    virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) = 0;
};

class GrpcServ: public Http2ConnDlgt {
  public:
    GrpcServ(GrpcStreamProvider *provider): accept_(*this), provider_(provider) {}
    // Http2ConnDlgt
    virtual void onError(Http2Conn *conn, const Http2ConnError& error) override;
    virtual void onStream(Http2Conn *conn, Http2Stream *stream) override;
    virtual void onConnected(Http2Conn *conn) override;
    virtual void onClosed(Http2Conn *conn) override;
    // GrpcAccept
    TcpAcceptDlgt *acceptDlgt() {
      return &accept_;
    } 
    // GrpcAccept API
    void gotConn(std::unique_ptr<Http2Conn>&& conn);
    void gotError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code);
  private:
    class GrpcAccept: public TcpAcceptDlgt {
      public:
        GrpcAccept(GrpcServ& serv) : serv_(serv) {}
        virtual void onAccept(TcpAccept *acc, int fd,
            struct sockaddr *localAddr, struct sockaddr *remoteAddr) override;
        virtual void onError(TcpAccept *acc, TcpAcceptDlgt::Error error, int code) override;
      private:
        GrpcServ& serv_;
    };
    GrpcAccept accept_;
    GrpcStreamProvider *provider_; // weak
    std::unordered_map<Http2Conn*, std::unique_ptr<Http2Conn>> conns_;
};

