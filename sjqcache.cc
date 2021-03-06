#include "sjqcache.h"
#include <string>
#include <utility>

namespace sjq {
    cache::cache(int way, int set, rep_policy p,
                 int mshr_num, int mshr_maxmerge,
                 std::string name) : num_way(way),
                                     num_set(set),
                                     policy(p),
                                     tag_array(set, t_set(way)),
                                     m_mshr(mshr_num, mshr_maxmerge),
                                     name(std::move(name)) {
    }

    cache::cache(int num_associate, int total_size) : cache(num_associate, (total_size >> 6) / num_associate,
                                                            rep_policy::lru, 196, 16, "default cache") {
    }

    cache::access_ret cache::try_access(unsigned long long addr, access_type) const {
        auto blockAddr = addr >> 6;

        auto set = blockAddr % num_set;
        auto tag = blockAddr;
        auto &set_entry = tag_array[set];
        auto it = set_entry.begin();
        auto the_last_unreserved_entry = it;
        //bug report :
        //there is a bug here:we may evict an entry that are waiting for fill
        bool all_reserved = true;
        for (auto &entry : set_entry) {
            if (entry.get_status() != cache_entry::cache_entry_status::reserved) {
                all_reserved = false;
                the_last_unreserved_entry = it;
            }

            if (entry.get_status() == cache_entry::cache_entry_status::invalid) //ok just use it
            {
                //to the first place,and push to mshr
                if (m_mshr.try_access(addr) != mshr::mshr_ret::ok) {
                    //m_statistics[type].num_res_fail++;
                    return resfail;
                }

                return miss;
            }
            if (entry.get_tag() == tag) //find the entry
            {
                if (entry.get_status() == cache_entry::cache_entry_status::valid) {
                    return hit;
                } else //reserved
                {
                    if (m_mshr.try_access(addr) != mshr::mshr_ret::ok) {
                        return resfail;
                    }
                    return hit_res;
                }
            }
            it++;
        }
        if (all_reserved) {
            return resfail;
        }

        if (m_mshr.try_access(addr) != mshr::mshr_ret::ok) {
            return resfail;
        }
        //not delete the last unreserved, and insert the new one to the top;

        return miss;
    }

    cache::access_ret cache::access(unsigned long long addr, access_type type, int from_type) {

        auto blockAddr = addr >> 6;

        auto set = blockAddr % num_set;
        auto tag = blockAddr;
        if (policy == lru) {
            auto &set_entry = tag_array[set];
            auto it = set_entry.begin();
            auto the_last_unreserved_entry = it;
            //bug report :
            //there is a bug here:we may evict an entry that are waiting for fill
            bool all_reserved = true;

            for (auto &entry : set_entry) {
                if (entry.get_status() != cache_entry::cache_entry_status::reserved) {
                    all_reserved = false;
                    the_last_unreserved_entry = it;
                }

                if (entry.get_status() == cache_entry::cache_entry_status::invalid) //ok just use it
                {
                    //to the first place,and push to mshr
                    if (type == access_type::read) {
                        if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                            m_statistics[type].num_res_fail++;
                            return resfail;
                        }
                    }
                    entry.set_entry(blockAddr,
                                    type == access_type::read ? cache_entry::cache_entry_status::reserved
                                                              : cache_entry::cache_entry_status::valid, from_type);
                    if (type == access_type::write) entry.set_dirty();

                    if (it != set_entry.begin()) {
                        //move to begin;
                        auto temp = entry;
                        while (it != set_entry.begin()) {
                            *it = *std::prev(it);
                            it--;
                        }
                        *it = temp;
                    }
                    m_statistics[type].num_miss++;
                    if (type == access_type::read)
                        m_on_going_miss++;
                    m_last_evicted_entry = cache_entry();
                    return miss;
                }
                if (entry.get_tag() == tag) //find the entry
                {
                    if (entry.get_status() == cache_entry::cache_entry_status::valid) {
                        //to the first place;
                        if (type == access_type::write) entry.set_dirty();
                        if (it != set_entry.begin()) {
                            //move to begin;
                            auto temp = entry;
                            while (it != set_entry.begin()) {
                                *it = *std::prev(it);
                                it--;
                            }
                            *it = temp;
                        }
                        m_statistics[type].num_hit++;
                        return hit;
                    } else //reserved
                    {
                        ASSERT(entry.get_status() == cache_entry::cache_entry_status::reserved, "should reserve");
                        if (type == access_type::read)
                            if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                                m_statistics[type].num_res_fail++;
                                return resfail;
                            }
                        if (type == access_type::write) entry.set_dirty();

                        if (it != set_entry.begin()) {
                            //move to begin;
                            auto temp = entry;
                            while (it != set_entry.begin()) {

                                *it = *std::prev(it);
                                it--;
                            }
                            *it = temp;
                        }

                        //to the first place; and push to mshr
                        m_statistics[type].num_hit_reserved++;
                        return hit_res;
                    }
                }
                it++;
            }
            if (all_reserved) {
                m_statistics[type].num_res_fail++;

                return resfail;
            }
            //it's miss and need to evict
            if (type == access_type::read)
                if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                    m_statistics[type].num_res_fail++;

                    return resfail;
                }
            //not delete the last unreserved, and insert the new one to the top;
            ASSERT((*the_last_unreserved_entry).get_status() != cache_entry::cache_entry_status::reserved,
                   "should not be reserved");
            if (the_last_unreserved_entry->get_status() == cache_entry::cache_entry_status::valid) {
                m_last_evicted_entry = *the_last_unreserved_entry;
            } else {
                m_last_evicted_entry = cache_entry();
            }
            //m_last_evicted_entry = the_last_unreserved_entry->get_tag() << 6;
            set_entry.erase(the_last_unreserved_entry);

            cache_entry entry;
            entry.set_entry(addr >> 6,
                            type == access_type::read ? cache_entry::cache_entry_status::reserved
                                                      : cache_entry::cache_entry_status::valid, from_type);
            if (type == access_type::write) entry.set_dirty();

            set_entry.insert(set_entry.begin(), entry);
            //m_last_evicted_entry = set_entry.back().get_tag() << 6; //return the full addr
            m_statistics[type].num_miss++;
            if (type == access_type::read)
                m_on_going_miss++;
            return miss;
        } else {
            ASSERT(false, "can't be here");
            auto &set_entry = tag_array[set];
            auto it = set_entry.begin();
            for (auto &entry : set_entry) {
                if (entry.get_status() == cache_entry::cache_entry_status::invalid) {
                    //to the first place,and push to mshr
                    if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                        return resfail;
                    }
                    entry.set_entry(blockAddr, cache_entry::cache_entry_status::reserved, 0);

                    return miss;
                }
                if (entry.get_tag() == tag) {
                    if (entry.get_status() == cache_entry::cache_entry_status::valid) {
                        return hit;
                    } else {
                        if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                            return resfail;
                        }

                        //to the first place; and push to mshr
                        return hit_res;
                    }
                }
                it++;
            }
            //not found valid and tag
            if (m_mshr.access(addr, 0) != mshr::mshr_ret::ok) {
                return resfail;
            }
            cache_entry entry;
            entry.set_entry(addr >> 6, cache_entry::cache_entry_status::reserved, 0);
            set_entry.pop_back();
            set_entry.insert(set_entry.begin(), entry);
            return miss;
        }
    }

    //start mshr
    mshr::mshr_ret mshr::try_access(unsigned long long addr) const {
        auto blockAddr = addr >> 6; //bug
        if (array.find(blockAddr) != array.end()) {
            if (array.at(blockAddr).size() >= max_merge) {
                return mshr_ret::merge_full;
            }
            //array[blockAddr].push_back(addr);
            return mshr_ret::ok;
        } else {
            if (array.size() >= num_entry) {
                return mshr_ret::entry_full;
            }
            //array.insert(std::make_pair(blockAddr, std::vector<unsigned long long>()));
            //array[blockAddr].push_back(addr);
            return mshr_ret::ok;
        }
    }

    mshr::mshr_ret mshr::access(unsigned long long addr, int ) {
        auto blockAddr = addr >> 6; //bug
        if (array.find(blockAddr) != array.end()) {
            if (array[blockAddr].size() >= max_merge) {
                return mshr_ret::merge_full;
            }
            array[blockAddr].push_back(addr);
            return mshr_ret::ok;
        } else {
            if (array.size() >= num_entry) {
                return mshr_ret::entry_full;
            }
            array.insert(std::make_pair(blockAddr, std::vector<unsigned long long>()));
            array[blockAddr].push_back(addr);
            return mshr_ret::ok;
        }
    }

    void cache_entry::set_dirty() {
        dirty = true;
    }
} // namespace sjq