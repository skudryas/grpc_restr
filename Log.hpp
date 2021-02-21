#include <iostream>

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
    deflog(LogLevel ll): ll_(ll) {}
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

#define LOG(__LL) deflog(__LL)

#ifdef DEBUGLOG
#define DLOG(__LL) deflog(__LL)
#else
#define DLOG(__LL) nullog()
#endif

