#include "Repl.h"

namespace Repl {
thread_local size_t g_repl_tid = 0;
}

#ifdef REPL_TEST
void test()
{
}

int main()
{
  test();
}

#endif // REPL_TEST
