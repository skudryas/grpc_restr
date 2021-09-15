#include <vector>
#include <string>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <assert.h>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include "Match.h"
//#include "Cache.h"
#include <iostream>
#include <sys/queue.h>
#include <algorithm>
#include <memory>

#include "Chain.hpp"

#include "robin-map/include/tsl/robin_map.h"
#include "robin-map/include/tsl/robin_set.h"
//#define REPL_PROFILE

#ifdef REPL_PROFILE
#include <gperftools/profiler.h>
#endif

#define INIT_ELAPSED \
  struct timespec __ts; \
  clock_gettime(CLOCK_MONOTONIC, &__ts); \
  double __end = 0.0, __prev = 0.0, __start = (double)__ts.tv_sec + ((double)__ts.tv_nsec) / 1000000000.; \
  __prev = __start;

#define PRINT_ELAPSED(__txt) do { \
  clock_gettime(CLOCK_MONOTONIC, &__ts); \
  __end = (double)__ts.tv_sec + ((double)__ts.tv_nsec) / 1000000000.; \
  std::cout << "[elapsed] " __txt " : "  << (__end - __prev) << std::endl; \
  __prev = __end; \
} while (0);

#define FINISH_ELAPSED(__txt) do { \
  clock_gettime(CLOCK_MONOTONIC, &__ts); \
  __end = (double)__ts.tv_sec + ((double)__ts.tv_nsec) / 1000000000.; \
  std::cout << "[elapsed] TOTAL " __txt " : "  << (__end - __start) << std::endl; \
} while (0);

namespace Repl {

extern thread_local size_t g_repl_tid;

using counter_t = size_t;

#define g_repl_tid1 g_repl_tid // 0

//#define REPL_DEBUG
//#define REPL_VDEBUG
//#define REPL_NO_UNINDEX
#define HASH_TABLE_TYPE_ROBIN
//#define HASH_TABLE_TYPE_STL
#if defined(HASH_TABLE_TYPE_STL)
  #define UMAP std::unordered_map
  #define USET std::unordered_set
#elif defined(HASH_TABLE_TYPE_ROBIN)
  #define UMAP tsl::robin_map
  #define USET tsl::robin_set
#else
  #error "HASH_TABLE_TYPE is unknown type"##HASH_TABLE_TYPE
#endif

#define SERV_THREAD_NUM 4 // 0
#define USE_MULTI_ACCEPT
#define USE_REPL_LOCK
//#define USE_CONCURRENT_QUEUE 1

class Counter
{
    std::vector<counter_t> counters_;
  public:
    Counter(size_t num)
    {
      counters_.resize(num);
    }
    counter_t inc() { return ++counters_[g_repl_tid1]; }
    void reset() {
      counters_[g_repl_tid1] = 0;
    }
    bool operator==(const Counter& rhs) {
      return counters_[g_repl_tid1] == rhs.counters_[g_repl_tid1];
    }
    counter_t& operator=(const Counter& rhs) {
      return counters_[g_repl_tid1] = rhs.counters_[g_repl_tid1];
    }
};

struct Consumer;

/*struct Key {
  std::string val;
  std::unordered_set<Pattern*> patterns;
  counter_t kcnt = 0;
  Key(const std::string &val_): val(val_) {}
};*/

struct Pattern
{
  std::string val;
  std::vector<Consumer*> consumers;
  Counter cnt;
  Pattern(const std::string &val_, size_t num = 1): val(val_), cnt(num)
  {
  } 
};

struct Consumer /*: public std::enable_shared_from_this<Consumer> */
{
  Consumer(size_t num): cnt(num)
  {
  }
  virtual ~Consumer()
  {
  }
  std::vector<Pattern*> patterns;
  Counter cnt;
  virtual void consume(const std::string &key, const Chain::Buffer &data)
  {
#ifdef REPL_DEBUG
    assert(0);
#endif
    // TODO
  }
};

class Repl
{
  private:
#ifdef REPL_PROFILE
    std::atomic<size_t> consumed_;
    std::atomic<size_t> forwarded_;
#endif
    std::shared_mutex rwmtx_;
    UMAP<std::string, Pattern*> patterns_;
    USET<Consumer*> consumers_;
    UMAP<std::string_view, std::vector<Pattern*>> index_;
    USET<Pattern*> anyindex_;
    Counter cnt_;
    size_t numthreads_;
    void checkCnt() {
      if (cnt_.inc() != 0)
        return;

      for (auto &i : consumers_) {
        i->cnt.reset();
      }
      for (auto &i : patterns_) {
        i.second->cnt.reset();
      }
      cnt_.inc();
    }
    void indexPattern(/*const*/ Pattern *pat)
    {
      // Добавляем шаблон в индекс
      std::string_view out[MAX_TOKEN_COUNT];
      auto tokens_count = Match::Tokens(pat->val, out);
      // 1. Бежим по списку токенов
      for (size_t i = 0; i < tokens_count; ++i) {
        // 2. Ищем токен в индексе
        auto tok = index_.find(out[i]);
        if (tok == index_.end()) {
          // 3. Если его нет - добавляем
          //std::unordered_set<Pattern*> patterns_in_token;
          //patterns_in_token.max_load_factor(16);
          auto empres = index_.emplace(std::move(out[i]), std::move(std::vector<Pattern*>()));
#ifdef REPL_DEBUG
          assert(empres.second);
#endif
          tok = empres.first;
        }
        // 4. Добавляем в индекс шаблон. Раньше его не должно было быть!
#ifdef REPL_DEBUG
        assert(std::find(tok->second.begin(), tok->second.end(), pat) == tok->second.end());
#endif
#ifdef HASH_TABLE_TYPE_ROBIN
        tok.value().emplace_back(pat);
#else
        tok->second.emplace_back(pat);
#endif
        // 5. Добавлять индекс в шаблон не надо. Нафиг он там не нужен.
      }
      // 6. Надо проиндексироват шаблоны только из * и #
      if (tokens_count == 0) {
#ifdef REPL_DEBUG
        assert(anyindex_.emplace(pat).second);
#else
        anyindex_.emplace(pat);
#endif
      }
    }
    void unindexPattern(/*const*/ Pattern *pat)
    {
      // Удаляем шаблон из индекса
      std::string_view out[MAX_TOKEN_COUNT];
      auto tokens_count = Match::Tokens(pat->val, out);
      // 1. Бежим по списку токенов
      for (size_t i = 0; i < tokens_count; ++i) {
        // 2. Ищем токен в индексе. Его не может не быть!
        auto tok = index_.find(out[i]);
#ifdef REPL_DEBUG
        assert(tok != index_.end());
#endif
        // 3. Удаляем из индекса ссылку на шаблон. Ее не может не быть!
#ifdef HASH_TABLE_TYPE_ROBIN
        auto pit = std::find(tok.value().begin(), tok.value().end(), pat);
#else
        auto pit = std::find(tok->second.begin(), tok->second.end(), pat);
#endif
#ifdef REPL_DEBUG
        assert(pit != tok->second.end());
#endif
#ifdef HASH_TABLE_TYPE_ROBIN
        std::swap(*pit, *(tok.value().end() - 1));
        tok.value().erase(tok.value().end() - 1);
#else
        std::swap(*pit, *(tok->second.end() - 1));
        tok->second.erase(tok->second.end() - 1);
#endif
        // 4. Подчищаем индекс
        if (tok->second.empty()) {
          index_.erase(tok);
        }
      }
      // 5. Надо удалить из индекса шаблоны только из * и #
      if (tokens_count == 0) {
#ifdef REPL_DEBUG
        assert(anyindex_.erase(pat) == 1);
#else
        anyindex_.erase(pat);
#endif
      }
    }
    std::vector<Pattern*>* reindexToken(const std::string& key)
    {
      // Для данного ключа находим самый короткий список возможных шаблонов
      //std::cout << "reindex size = " << index_.size() << std::endl;
      std::string_view out[MAX_TOKEN_COUNT];
      auto tokens_count = Match::Tokens(key, out);
      // 1. Бежим по списку токенов
      std::vector<Pattern*> *best = nullptr;
      size_t best_size = 0;
      for (size_t i = 0; i < tokens_count; ++i) {
        // 2. Ищем токен в индексе. Его может не быть!
        auto tok = index_.find(out[i]);
        if (tok == index_.end()) {
          //std::cout << "not found while index size = " << index_.size() << std::endl;
          continue;
        } else {
          //std::cout << "FOUND " << out[i] << std::endl;
        }
        // 3. Нам нужен токен, у которого меньше всего паттернов, мы хотим матчить поменьше
        if (best_size == 0 || best_size > tok->second.size()) {
#ifdef HASH_TABLE_TYPE_ROBIN
          best = &tok.value();
#else
          best = &tok->second;
#endif
          best_size = best->size();
          // 4. Идеально!
          if (best_size == 1)
            break;
        }
      }
      return best;
    }
  public:
#ifdef REPL_PROFILE
    size_t consumed() const { return consumed_; }
    size_t forwarded() const { return forwarded_; }
#endif
    virtual void startProfiling()
    {
#ifdef REPL_PROFILE
      ProfilerStart("/tmp/grpc_restr.prof");
#endif
    }
    virtual void stopProfiling()
    {
#ifdef REPL_PROFILE
      ProfilerStop();
#endif
    }

    void printStat() const {
//    std::cout << "T: " << g_repl_tid1 << " patterns=" << patterns_.size() << " consumers=" << consumers_.size() << std::endl;
    }
    Repl(size_t num = 1):
#ifdef REPL_PROFILE
      consumed_(0), forwarded_(0),
#endif
      cnt_(num), numthreads_(num) {
      consumers_.max_load_factor(1);
      consumers_.reserve(512);
      patterns_.max_load_factor(1);
      patterns_.reserve(1024 * 1024 * 4);
      index_.max_load_factor(1);
      index_.reserve(1024 * 1024 * 8);
      anyindex_.reserve(64);
      // ТАК БЫСТРЕЕ!!!
    }
    void addConsumer(Consumer *cons)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
#ifdef REPL_DEBUG
      assert(!consumers_.count(cons));
#endif
#ifdef REPL_VDEBUG
      if (consumers_.size() == 0) {
        std::cout << "consumers = " << consumers_.size() << "/" << consumers_.bucket_count() << " patterns = " << patterns_.size() << "/" << patterns_.bucket_count() << " index = " << index_.size() << "/" << index_.bucket_count() << " anyindex = " << anyindex_.size() << "/" << anyindex_.bucket_count() << std::endl;
      }
#endif
#ifdef REPL_PROFILE
      if (consumers_.size() == 0) {
        startProfiling();
      }
#endif
      consumers_.insert(cons);
    }
    void removeConsumer(Consumer *cons)
    {
      //std::cout << "consumer removed" << std::endl;
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
#ifdef REPL_VDEBUG
      bool dump = false;
      if (consumers_.size() == 500) {
        dump = true;
        std::cout << "consumers = " << consumers_.size() << "/" << consumers_.bucket_count() << " patterns = " << patterns_.size() << "/" << patterns_.bucket_count() << " index = " << index_.size() << "/" << index_.bucket_count() << " anyindex = " << anyindex_.size() << "/" << anyindex_.bucket_count() << std::endl;
      }
#endif 
#ifdef REPL_PROFILE
      if (consumers_.size() == 500 || consumers_.size() == 100) {
        std::cout << "Reached consumer disconnect while consumer count is " << consumers_.size() << std::endl;
        stopProfiling();
      }
#endif
      // XXX START
      // 1. Идем по списку паттернов в консумере
      for (auto &i: cons->patterns) {
        // 2. Находим интересующего нас консумера в паттерне
        auto it = std::find(i->consumers.begin(), i->consumers.end(), cons);
        // 3. Его не может не быть!
#ifdef REPL_DEBUG
        assert(it != i->consumers.end());
#endif
        // 4. Удаляем консумера из паттерна
        std::swap(*it, *(i->consumers.end() - 1));
        i->consumers.erase(i->consumers.end() - 1);
        // 5. XXX если у паттерна больше нет консумеров - удаляем паттерн
#ifndef REPL_NO_UNINDEX
        if (i->consumers.empty()) {
          unindexPattern(i);
          patterns_.erase(i->val);
          delete i;
        }
#endif
      }
      // XXX STOP
      consumers_.erase(cons);
#ifdef REPL_VDEBUG
      if (dump) {
        std::cout << "consumers = " << consumers_.size() << "/" << consumers_.bucket_count() << " patterns = " << patterns_.size() << "/" << patterns_.bucket_count() << " index = " << index_.size() << "/" << index_.bucket_count() << " anyindex = " << anyindex_.size() << "/" << anyindex_.bucket_count() << std::endl;
      }
#endif 

    }

    void tryConsume(const std::string &key, const /*std::string*/Chain::Buffer &data, Pattern *pat)
    {
      if (Match::Result::Yes == Match::Match(key.c_str(), pat->val.c_str())) {
        for (auto &cons: pat->consumers) {
          /*if (cons->cnt == cnt_) {
            std::cout << "consume5" << std::endl;
            continue;
          }
          cons->cnt = cnt_;*/
          cons->consume(key, data);
#ifdef REPL_PROFILE
          forwarded_++;
#endif
        }
      }
    }

    void consume(const std::string &key, const Chain::Buffer &data)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::shared_lock lock(rwmtx_);
#endif
#ifdef REPL_PROFILE
      ++consumed_;
#endif
#ifdef REPL_VDEBUG
      INIT_ELAPSED;
#endif
      // 1. Если каким-то чудом произошло переполнение каунтера - обнуляем его
      //checkCnt();

      auto *patterns = reindexToken(key);
      if (patterns) {
#ifdef REPL_VDEBUG
        if (consumers_.size() > 500) {
          std::cout << "patterns count: " << patterns->size() << std::endl;
        }
#endif
        for (Pattern *pat: *patterns) {
          //std::cout << "consume4" << std::endl;
          tryConsume(key, data, pat);
        }
      }
      for (auto &pat: anyindex_) {
        tryConsume(key, data, pat);
      }

#ifdef REPL_VDEBUG
      if (consumers_.size() > 500) {
        FINISH_ELAPSED("consume");
      }
#endif
    }
    template <typename Iterable>
    void subscribeBatch(Consumer *cons, const Iterable &pats)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
      // 1. Ищем паттерн
      for (auto &pat: pats) {
        subscribeUnlocked(cons, pat);
      }
    }

    void subscribe(Consumer *cons, const std::string &pat)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
      subscribeUnlocked(cons, pat);
    }

    template <typename Iterable>
    void unsubscribeBatch(Consumer *cons, const Iterable &pats)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
      for (auto &pat: pats) {
        // 1. Ищем паттерн
        unsubscribeUnlocked(cons, pat);
      }
    }

    void unsubscribe(Consumer *cons, const std::string &pat)
    {
#if SERV_THREAD_NUM > 1 && defined(USE_REPL_LOCK)
      std::unique_lock lock(rwmtx_);
#endif
      unsubscribeUnlocked(cons, pat);
    }

    void subscribeUnlocked(Consumer *cons, const std::string &pat)
    {
      if (!consumers_.count(cons))
        return;
      // 1. Ищем паттерн
      auto it = patterns_.find(pat);
      // 2. Если его нет - добавляем
      if (it == patterns_.end()) {
        it = patterns_.emplace(pat, new Pattern(pat, numthreads_)).first;
        indexPattern(it->second);
      }
      // 3. Добавляем консумера в найденный / созданный паттерн
      if (std::find(it->second->consumers.begin(), it->second->consumers.end(), cons)
            == it->second->consumers.end()) {
        it->second->consumers.emplace_back(cons);
        // 4. Если консумера в паттерне не было - добавляем в паттерны консумера
        // новый паттерн. Его там не должно было быть!
#ifdef REPL_DEBUG
        assert(std::find(cons->patterns.begin(), cons->patterns.end(), it->second)
            == cons->patterns.end());
#endif
        cons->patterns.emplace_back(it->second);
      } else {
        // 5. Если консумер был в паттерне - то и в консумере должен быть этот паттерн!
#ifdef REPL_DEBUG
        assert(std::find(cons->patterns.begin(), cons->patterns.end(), it->second)
            != cons->patterns.end());
#endif
      }
    }

    void unsubscribeUnlocked(Consumer *cons, const std::string &pat)
    {
      if (!consumers_.count(cons))
        return;
      //std::cout << "T: " << g_repl_tid1 << " UNSUBSCRIBE:" << pat << std::endl;
      // 1. Ищем паттерн
      auto it = patterns_.find(pat);
      // 2. Если такого паттерна совсем нет - дальше делать нечего.
      if (it == patterns_.end()) {
        return;
      }
      // 3. Ищем указанного консумера в паттерне
      auto cit = std::find(it->second->consumers.begin(), it->second->consumers.end(), cons);
      // 4. Если этот консумер и так не был подписан - уходим
      if (cit == it->second->consumers.end()) {
        return;
      }
      // 5. Удаляем паттерн из консумера
      auto pit = std::find(cons->patterns.begin(), cons->patterns.end(), it->second);
#ifdef REPL_DEBUG
      assert(pit != cons->patterns.end());
#endif
      std::swap(*pit, *(cons->patterns.end() - 1));
      cons->patterns.erase(cons->patterns.end() - 1);
      // 6. Удаляем консумера из паттерна
      std::swap(*cit, *(it->second->consumers.end() - 1));
      it->second->consumers.erase(it->second->consumers.end() - 1);
      // 7. Если у паттерна больше нет консумеров - удаляем паттерн
#ifndef REPL_NO_UNINDEX
      if (it->second->consumers.empty()) {
        unindexPattern(it->second);
        auto curpat = it->second;
        patterns_.erase(it);
        delete curpat;
        // XXX try to "delayed" removing
      }
#endif
    }
};
} // namespace Repl
