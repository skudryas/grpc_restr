#pragma once

#include <vector>
#include <list>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <streambuf>

#include <iostream>

extern const int g_pagesize;

class Chain {
  private:
    class Dh {
      private:
        std::vector<uint8_t> buf_;
        ssize_t begin_, end_;

      public:
        // read constructors
        Dh() : begin_(0), end_(0) {
          buf_.reserve(g_pagesize);
        }
        Dh(size_t size) : begin_(0), end_(0), buf_(size) {
        }

        // write constructors
        Dh(const std::vector<uint8_t> &buf) :
          buf_(buf), begin_(0), end_(buf_.size()) {}
        Dh(std::vector<uint8_t> &&buf) :
          buf_(std::move(buf)), begin_(0), end_(buf_.size()) {}
        Dh(const uint8_t *buf, size_t size) :
                begin_(0), end_(size) {
          buf_.resize(size);
          memcpy(buf_.data(), buf, size);
        }
        // accessors
        size_t size() const { // w
          return end_ - begin_;
        }
        size_t max_size() const {
          return buf_.size();
        }
        size_t space() const { // r
          return buf_.size() - end_;
        }
        uint8_t* data() { // w
          return buf_.data() + begin_;
        }
        uint8_t* tail() { // r
          return buf_.data() + end_;
        }

        bool drain(size_t &sz) { // w
          size_t cursize = std::min(size(), sz);
          begin_ += cursize;
          sz -= cursize;
          return (size() == 0);
        }
        void fill(size_t sz) { // r
          end_ += sz;
        }
        void pullup() {
          if (begin_ != 0) {
            for (size_t i = 0; i < size(); ++i) {
              buf_[i] = buf_[begin_ + i];
            }
            end_ -= begin_;
            begin_ = 0;
          }
        }
    };
    std::list<Dh> chain_;
    size_t size_, blockSize_;
  public:
    Chain(): size_(0), blockSize_(g_pagesize) {}
    size_t size() const { return size_; }
    size_t& blockSize() { return blockSize_; }
    std::list<Dh> &chain() { return chain_; }
    struct Buffer {
      uint8_t *buf;
      size_t size;
    };
    Buffer readTo(size_t recommended = 1) {
      if (chain_.size() == 0 || chain_.back().space() < recommended) {
          chain_.emplace_back(std::max(recommended, blockSize_));
      }
      return Buffer{ chain_.back().tail(), chain_.back().space() };
    }
    Buffer writeFrom(size_t recommended = 0) {
      if (recommended == 0)
        recommended = blockSize_;
      if (size_ > 0) {
        Dh &front = chain_.front();
        return Buffer{ front.data(), std::min(front.size(), recommended) };
      } else {
        return Buffer{ nullptr, 0 };
      }
    }
    // XXX writeFromIOV!
    Buffer pullupFrom(size_t atleast) {
      if (size_ > 0 && atleast <= size_) {
        if (chain_.front().max_size() < atleast) {
          // Сase 1. Create a big node
          size_t gotsize = 0;
          for (auto & i: chain_) {
            gotsize += i.size();
            if (gotsize >= atleast)
              break;
          }
          Dh dh(gotsize);
          for (auto i = chain_.begin(); gotsize != 0 && i != chain_.end(); ) {
            Buffer w{ dh.tail(), dh.space() };
            Buffer r{ i->data(), i->size() };
            memcpy(w.buf, r.buf, r.size);
            i->drain(r.size);
            i = chain_.erase(i);
            dh.fill(r.size);
            gotsize -= r.size;
          }
          chain_.emplace_front(dh);
        } else {
          // Сase 2. Move to front node
          while (chain_.front().size() < atleast) {
            chain_.front().pullup();
            Buffer w{ chain_.front().tail(), chain_.front().space() }; // XXX why back().space() ???
            auto nextchain = ++chain_.begin();
            assert(nextchain != chain_.end());
            Buffer r{ nextchain->data(), nextchain->size() };
            size_t to_copy = std::min(r.size, w.size);
            memcpy(w.buf, r.buf, to_copy);
            chain_.front().fill(to_copy);
            if (nextchain->drain(to_copy)) { // XXX why r.size ?
              chain_.erase(nextchain);
            }
          }
        }
        Dh &front = chain_.front();
        assert(front.data() != nullptr);
        return Buffer{ front.data(), front.size() };
      } else {
        return Buffer{ nullptr, 0 };
      }
    }

    Buffer pullupAll() {
      return pullupFrom(size_);
    }

    size_t drain(size_t size) {
      size_t prev_size = size;
      while (size && chain_.size()) {
        Dh &front = chain_.front();
        if (front.drain(size)) {
          chain_.pop_front();
        }
      }
      size_ = size_ - (prev_size - size);
      return size;
    }
    void fill(size_t size) {
      if (!size) {
        return;
      }
      Dh &back = chain_.back();
      back.fill(size);
      size_ += size;
    }
    void clear() {
      size_ = 0;
      chain_.clear();
    }
    void emplaceBack(std::vector<uint8_t> &&v) {
      size_ += v.size();
      chain_.emplace_back(std::move(v));
    }
    class StreamBuf: public std::streambuf {
      private:
        Chain &chain_;
      public:
        StreamBuf(Chain &chain): chain_(chain) {
        }
        virtual ~StreamBuf() {
          chain_.fill(epptr() - pptr());
          chain_.drain(gptr() - eback());
        }
      protected:
        // get
        virtual std::streambuf::int_type underflow() override {
          char *gptr1 = gptr(), *egptr1 = egptr(), *eback1 = eback();
          if (gptr() == egptr()) {
            Buffer buf = chain_.writeFrom();
            if (buf.buf == nullptr) {
              return std::streambuf::traits_type::eof();
            }
            if (buf.buf == (uint8_t*)eback()) {
              chain_.drain(gptr() - eback());
              buf = chain_.writeFrom();
            }
            if (buf.buf == nullptr) {
              return std::streambuf::traits_type::eof();
            }
            setg((char*)buf.buf, (char*)buf.buf, (char*)(buf.buf + buf.size));
          } else {
            assert(0); // Unexpected call...
          }
          return *gptr();
        }
        // put
        virtual std::streambuf::int_type overflow( std::streambuf::int_type ch = std::streambuf::traits_type::eof() ) override {
          Buffer buf = chain_.readTo();
          if (pptr() == (char*)(buf.buf + buf.size) || pptr() == nullptr) {
            if (pptr() != nullptr) {
              chain_.fill(buf.size);
              buf = chain_.readTo();
            }
            setp((char*)(buf.buf + 1), (char*)(buf.buf + buf.size));
            if (ch != std::streambuf::traits_type::eof()) {
              *buf.buf = ch;
            }
          } else {
            assert(0); // Unexpected call...
          }
          return ch;
        }
    };
};

