#include "Match.h"
#include <iostream>
#include <assert.h>

namespace Match {

Result Match(const char *key, const char *pat)
{
  do {
    if (*pat == 0 && *key == 0)
      return Result::Yes;
    if (*pat == '*') {
      if (*key == 0)
        return Result::Yes;
      while (*key && *key != '.')
        ++key;
      ++pat;
      if (*pat && *pat != '.')
        return Result::Bad_pat;
      continue;
    }
    if (*pat == '#') {
      // 0
      const char *newpat = pat;
      ++newpat;
      if (*newpat == 0)
        return Result::Yes;
      if (*newpat == '.')
        ++newpat;
      else
        return Result::Bad_pat;
      Result res = Match(key, newpat);
      if (res != Result::No)
        return res;
      // 1
      if (*key == 0)
        return Result::No;
      while (*key && *key != '.')
        ++key;
      if (*key == '.')
        ++key;
      continue;
    }
    while (*pat && *pat != '.' && *key && *key != '.' && *pat == *key) {
      ++key; ++pat;
    }
    if (*pat == '.' && *key == '.') {
      ++pat; ++key;
      continue;
    }
    if (*pat == '.' && *(pat + 1) == '#' && *key == 0) {
      ++pat;
      continue;
    }
    if (*pat == 0 && *key == 0)
      return Result::Yes;
    return Result::No;
  } while (true);
}

size_t Tokens(const std::string &src, std::string_view *out)
{
//  std::vector<std::string> ret;
  size_t index = 0;
  const char *p = src.c_str();
  const char *start = p;
  do {
    if (*p == '.' || *p == 0) {
      bool push = !(p - start == 1 && (*start == '*' || *start == '#'));
      if (push) {
        for (size_t i = 0; i < index; ++i) {
          bool equals = true;
          const char *cur = start;
          const char *prev = out[i].data();
          for (; ; ++prev, ++cur) {
            if (*prev == *cur || (*prev == 0 && *cur == '.') || (*prev == '.' && *cur == 0)) {
            } else {
              equals = false;
              break;
            }
            if (*prev == '.')
              break;
          }
          if (equals) {
            push = false;
            break;
          }
        }
      }
      if (push)
        out[index++] = std::string_view(start, p - start);
      start = p;
      if (*p == 0)
        break;
      start = ++p;
    } else {
      ++p;
    }
  } while (true);
  return index;
}

} // namespace match

#ifdef MATCH_TEST

void match_test()
{
  assert(Match::Match("", "#") == Match::Result::Yes);
  assert(Match::Match(".", "#") == Match::Result::Yes);
  assert(Match::Match("..", "#") == Match::Result::Yes);
  assert(Match::Match("a", "#") == Match::Result::Yes);
  assert(Match::Match("a.", "#") == Match::Result::Yes);
  assert(Match::Match("a..", "#") == Match::Result::Yes);
  assert(Match::Match("aa.", "#") == Match::Result::Yes);
  assert(Match::Match(".aaa", "#") == Match::Result::Yes);
  assert(Match::Match(".aaa.", "#") == Match::Result::Yes);
  assert(Match::Match(".aaa..", "#") == Match::Result::Yes);
  assert(Match::Match("..aaa..", "#") == Match::Result::Yes);
  assert(Match::Match("", "*") == Match::Result::Yes);
  assert(Match::Match("1", "*") == Match::Result::Yes);
  assert(Match::Match("123", "*") == Match::Result::Yes);
  assert(Match::Match(".", "*") == Match::Result::No);
  assert(Match::Match("..", "*") == Match::Result::No);
  assert(Match::Match(".", ".*") == Match::Result::Yes);
  assert(Match::Match(".", "*.") == Match::Result::Yes);
  assert(Match::Match("..", "*.") == Match::Result::No);
  assert(Match::Match("..", "*.*") == Match::Result::No);
  assert(Match::Match("..", "*.*.*") == Match::Result::Yes);
  assert(Match::Match("..", "*.*.") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "a.a.") == Match::Result::No);
  assert(Match::Match("a.a.a", "a.*.") == Match::Result::No);
  assert(Match::Match("a.a.a", "a.*.*") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "a.*.a") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "a.*.b") == Match::Result::No);
  assert(Match::Match("a.a.a", "b.*.a") == Match::Result::No);
  assert(Match::Match("a.a.a", "*.a.a") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "*.b.a") == Match::Result::No);
  assert(Match::Match("a.a.a", "a.*.#") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "a.#.*") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "*.#.*") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "#.#.#") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "#.#") == Match::Result::Yes);
  assert(Match::Match("a.a.a", "#.*.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "#.*.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "#.a.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "#.d.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "#.d.*") == Match::Result::No);
  assert(Match::Match("a.b.c.d", "*.d.#") == Match::Result::No);
  assert(Match::Match("a.b.c.d", "a.#.d.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "a.*.d.#") == Match::Result::No);
  assert(Match::Match("a.b.c.d", "a.#.d.#") == Match::Result::Yes);
  assert(Match::Match("a.b.c.d", "a.#.d.*") == Match::Result::No);
}

void match_handmade()
{
  while (true) {
    std::string key, pat;
    std::cout << "Key:" << std::endl;
    std::cin >> key;
    std::cout << "Pattern:" << std::endl;
    std::cin >> pat;
    Match::Result res = Match::Match(key.c_str(), pat.c_str());
    switch (res) {
      case Match::Result::Yes:
        std::cout << "Yes" << std::endl;
        break;
      case Match::Result::No:
        std::cout << "No" << std::endl;
        break;
      case Match::Result::Bad_src:
        std::cout << "Bad key" << std::endl;
        break;
      case Match::Result::Bad_pat:
        std::cout << "Bad pat" << std::endl;
        break;
      default:
        std::cout << "Unknown error" << std::endl;
        break;
    }
  }
}

void tokens_handmade()
{
  while (true) {
    std::string key;
    std::cout << "Key:" << std::endl;
    std::cin >> key;
    std::string_view out[MAX_TOKEN_COUNT];
    auto ret = Match::Tokens(key, out);
    std::string result(std::to_string(ret) + ": ");
    for (size_t i = 0; i != ret ; ) {
      result += out[i];
      ++i;
      if (i == ret)
        break;
      else
        result += '.';
    }
    std::cout << result << std::endl;
  }
}


int main()
{
#if 1
  match_test();
#else
  tokens_handmade();
  match_handmade();
#endif
}

#endif // MATCH_TEST
