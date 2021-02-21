#include "Log.hpp"
#include <unistd.h>
#include <strings.h>

LogLevel g_loglevel = ALERT;
void set_log_level(LogLevel ll) {
  g_loglevel = ll;
}

void set_log_level(int argc, char **argv) {
  int opt = 0;
  while ((opt = getopt(argc, argv, "v:")) != -1) {
    switch (opt) {
      case 'v':
        if (strcasecmp(optarg, "NOLOG") == 0) {
          g_loglevel = NOLOG;
        } else if (strcasecmp(optarg, "FATAL") == 0) {
          g_loglevel = FATAL;
        } else if (strcasecmp(optarg, "ERROR") == 0) {
          g_loglevel = ERROR;
        } else if (strcasecmp(optarg, "WARNING") == 0) {
          g_loglevel = WARNING;
        } else if (strcasecmp(optarg, "ALERT") == 0) {
          g_loglevel = ALERT;
        } else if (strcasecmp(optarg, "INFO") == 0) {
          g_loglevel = INFO;
        } else if (strcasecmp(optarg, "DEBUG") == 0) {
          g_loglevel = DEBUG;
        }
      default:
        break;
    }
  }
}
