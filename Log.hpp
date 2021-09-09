#include <iostream>
#include <sys/time.h>

class nullog /*: public std::ostream*/ {
  public:
    template<typename T>
    nullog& operator<<(const T&) { return *this; }
};

//template<typename T>
//nullog& operator<<(nullog& n, const T&) { return n; }

enum LogLevel {
  NOLOG = 0,
  FATAL,
  ERROR,
  WARNING,
  ALERT,
  INFO,
  DEBUG
};

extern LogLevel g_loglevel;

class deflog /*: public std::ostream */ {
  private:
    LogLevel ll_;
  public:
    deflog(LogLevel ll, const char *func, int line): ll_(ll) {
      if (g_loglevel >= ll_) {
        struct timeval ntv;
        gettimeofday(&ntv, NULL);
        struct tm now_tm;
        localtime_r(&ntv.tv_sec, &now_tm);
        char timebuf[256];
        int timelen = strftime(timebuf, 256, "%b %e %T", &now_tm);
        snprintf(&timebuf[timelen], 32, ".%04d", ((int)ntv.tv_usec) / 100);
        std::cout << timebuf << " " << func << ":" << line << " ";
      }
    }
    template<typename T>
    deflog& operator<<(const T& t) {
      if (g_loglevel >= ll_)
        std::cout << t; return *this;
    }
    ~deflog() {
      if (g_loglevel >= ll_)
        std::cout << std::endl;
    }
};

void set_log_level(LogLevel);
void set_log_level(int argc, char **argv);

#ifndef QUIETLOG
#define LOG(__LL) deflog(__LL, __func__, __LINE__)
#else
#define LOG(__LL) nullog()
#endif

#if defined(DEBUGLOG) && !defined(QUIETLOG)
#define DLOG(__LL) deflog(__LL, __func__, __LINE__)
#else
#define DLOG(__LL) nullog()
#endif

