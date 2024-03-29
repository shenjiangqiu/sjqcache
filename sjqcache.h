#ifndef SJQ_CACHE_H
#define SJQ_CACHE_H

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#ifndef NDEBUG
#define ASSERT(condition, message)                                             \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Assertion `" #condition "` failed in " << __FILE__         \
                << " line " << __LINE__ << ": " << message << std::endl;       \
      std::terminate();                                                        \
    }                                                                          \
  } while (false)
#else
#define ASSERT(condition, message)                                             \
  do {                                                                         \
  } while (false)
#endif

namespace sjq {
class cache_entry {
public:
  bool operator==(const cache_entry &rhs) const {
    return m_status == rhs.m_status && m_tag == rhs.m_tag &&
           m_type == rhs.m_type;
  }

  bool operator!=(const cache_entry &rhs) const { return !(rhs == *this); }

  enum class cache_entry_status { invalid, valid, reserved };

  cache_entry() : m_status(cache_entry_status::invalid), m_tag(-1) {}

  void set_entry(unsigned long long tag, cache_entry_status status,
                 int from_type) {
    m_tag = tag;
    m_status = status;
    m_type = from_type;
  }

  [[nodiscard]] cache_entry_status get_status() const { return m_status; }

  [[nodiscard]] unsigned long long get_tag() const { return m_tag; }

private:
  cache_entry_status m_status;
  unsigned long long m_tag;
  int m_type{};
  bool dirty = false;

public:
  [[nodiscard]] bool isDirty() const { return dirty; }

public:
  void set_dirty();

  [[nodiscard]] int getMType() const { return m_type; }
};

class mshr {
public:
  using t_merge = std::vector<unsigned long long>;
  using t_array = std::map<unsigned long long, t_merge>;

public:
  enum class mshr_ret { ok, entry_full, merge_full };
  enum class access_type { read, write };

  mshr(int entry = 0, int max_merge = 0)
      : num_entry(entry), max_merge(max_merge){

                          };

  mshr_ret access(unsigned long long addr, int data_from);

  mshr_ret try_access(unsigned long long addr) const;

  void fill(unsigned long long addr) { array.erase(addr >> 6); }

  bool full(unsigned long long addr) {
    if (array.find(addr >> 6) != array.end()) {
      return array[addr >> 6].size() >= max_merge; // bug <
    } else {
      return array.size() >= num_entry;
    }
  }

private:
  size_t num_entry;
  size_t max_merge;
  t_array array;

  friend class cache_debugger;
};

struct statistics {
  unsigned long long num_hit = 0;
  unsigned long long num_miss = 0;
  unsigned long long num_hit_reserved = 0;
  unsigned long long num_res_fail = 0;
};

template <typename OSTYPE>
OSTYPE &operator<<(OSTYPE &os, const statistics &stat) {
  os << "\nnum_hit: " << stat.num_hit << "\nnum_miss " << stat.num_miss
     << "\nnum_hit_reserved " << stat.num_hit_reserved << "\nnum_res_fail "
     << stat.num_res_fail;
  return os;
}

class cache;

class cache {
  using t_set = std::vector<cache_entry>;

  using t_array = std::vector<t_set>;

public:
  inline static unsigned long long get_blocker_addr(unsigned long long addr) {
    return addr >> 6;
  }

  enum rep_policy { lru, fifo };
  enum access_ret { hit, hit_res, miss, resfail };
  enum access_type { read, write };

  cache(int way, int set, rep_policy p, int mshr_num, int mshr_maxmerge,
        std::string name);

  cache(int num_associate, int total_size);

  cache() = delete;

  cache(const cache &) = default;

  cache(cache &&) = default;

  auto get_name() const { return name; }

  auto get_stats() const { return m_statistics; }

  std::pair<int, int> get_size() const {
    return std::make_pair(num_set, num_way);
  }

  virtual ~cache() {}

  cache::access_ret access(unsigned long long addr, access_type type,
                           int from_type);

  access_ret try_access(unsigned long long addr, access_type type) const;

  int get_on_going_misses() const { return m_on_going_miss; }

  bool is_clear() const {
    for (auto &set_entry : tag_array) {
      for (auto &entry : set_entry) {
        if (entry.get_status() == cache_entry::cache_entry_status::reserved) {
          return false;
        }
      }
    }
    return true;
  }

  void fill(unsigned long long addr) {
    // std::cout << "fill:" << addr << std::endl;
    auto blockAddr = addr >> 6;
    auto set = blockAddr % num_set;

    auto &set_entry = tag_array[set];
    auto tag = blockAddr;

    for (auto &entry : set_entry) {
      if (entry.get_tag() == tag) {
        ASSERT(entry.get_status() == cache_entry::cache_entry_status::reserved,
               "add=" << addr);
        entry.set_entry(tag, cache_entry::cache_entry_status::valid,
                        entry.getMType());
        m_mshr.fill(addr);
      }
    }
    m_on_going_miss--;
    if (m_on_going_miss < 0) {
      throw;
    }
    ASSERT(m_on_going_miss >= 0, "on going miss should be positive");
  }

  cache_entry get_last_evict() const { return m_last_evicted_entry; }

private:
  int m_on_going_miss = 0;
  cache_entry m_last_evicted_entry{};
  unsigned num_way;
  unsigned num_set;
  unsigned policy;

  t_array tag_array;
  mshr m_mshr;
  const std::string name;
  statistics m_statistics[2];

  friend class cache_debugger;
};

template <typename OSTYPE> OSTYPE &operator<<(OSTYPE &os, const cache &ca) {
  os << ca.get_stats()[0] << std::endl;
  os << ca.get_stats()[1] << std::endl;

  return os;
}

inline std::ostream &operator<<(std::ostream &os, const cache &ca) {
  os << ca.get_stats()[0] << std::endl;
  os << ca.get_stats()[1] << std::endl;

  return os;
}

inline std::ostream &operator<<(std::ostream &os, cache &ca) {
  os << ca.get_stats()[0] << std::endl;
  os << ca.get_stats()[1] << std::endl;

  return os;
}

class cache_debugger {
public:
  static cache_entry get_entry(cache &tcache, int set, int way) {
    // std::cout<<"get entry:"<<set<<","<<way<<std::endl;
    return tcache.tag_array[set][way];
  }

  static cache::t_array &get_array(cache &tcache) { return tcache.tag_array; }

  static mshr::t_array get_mshr_array(cache &tcache) {
    return tcache.m_mshr.array;
  }

  static mshr::t_array get_mshr_array(mshr &m_mshr) { return m_mshr.array; }
};
} // namespace sjq
#endif