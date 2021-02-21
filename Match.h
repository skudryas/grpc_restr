#include <list>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace Match {

enum class Result {
  Yes,
  No,
  Bad_src,
  Bad_pat,
};

// A brutal match way
Result Match(const char *src, const char *pat);

struct TokenAnal {
};

#define MAX_TOKEN_COUNT 33
// probably no 

size_t Tokens(const std::string &src, std::string_view* out);

////
#if 0
typedef std::pair<bool, std::string> string_t; // false - строки нет (не пустая строка а именно отсутствующая строка)
typedef std::unordered_multimap<std::string, string_t> slice_t; // слой из токенов разных паттернов
typedef std::vector<slice_t> library_t; // список слоев
typedef std::vector<std::string> key_t;

key_t GetKey(const std::string &str);
void AddToLibrary(library_t &lib, key_t &&key);
struct MatchEngine {
};

template <typename ConsumerPtr>
struct Token {
  enum class Type {
    STAR,
    HASH,
    TEXT,
  };
  struct Ptr {
    struct Hash {
      size_t operator(const Ptr& p)
      {
        return std::hash(p.ptr->token);
      }
    };
    Token *ptr;
    Ptr(Token *p): ptr(p) { ptr->ref(); }
    ~Ptr() { if (ptr->unref() == 0) delete ptr; }
  };
  struct Chain {
    /*struct Ptr {
      Chain *ptr;
    };*/
    std::unorderd_set<ConsumerPtr> cons;
    std::vector<Token::Ptr> tokens;
  };
  Token(const std::string &t): token(t), refcnt(0) {}
  std::string token;
  Type type;
/*  size_t refcnt;
  size_t ref() { return ++refcnt; }
  size_t unref() { return --refcnt; }*/
  std::unordered_set<Chain::Ptr> chains; 
};
#endif // if 0

} // namespace Match

