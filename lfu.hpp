#ifndef LFU_CACHE_HPP
#define LFU_CACHE_HPP

#include "cache.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <functional>
#include <cstdint>

template <typename Key, typename T>
class LfuCache : public ICache<Key, T> {
private:
    size_t sz_ = 0;
    int min_freq_ = 1;
    
    int64_t hits_count_ = 0;
    int64_t misses_count_ = 0;

    struct NodeInfo {
        int freq;
        typename std::list<std::pair<Key, T>>::iterator it;
    };

    std::unordered_map<Key, NodeInfo> hash_;
    std::unordered_map<int, std::list<std::pair<Key, T>>> freq_buckets_;

public:
    explicit LfuCache(size_t sz) : sz_(sz) {
        if (sz_ == 0) {
            throw std::invalid_argument("Cache size must be positive");
        }
        this->next_cache_layer = nullptr;
    }

    void set_next_layer(ICache<Key, T>* next) override {
        this->next_cache_layer = next;
    }

    size_t capacity() const override {
        return sz_;
    }

    int64_t get_hits() const override {
        return hits_count_;
    }

    int64_t get_misses() const override {
        return misses_count_;
    }

    bool full() const { 
        return hash_.size() >= sz_; 
    }

    T lookup_update(const Key& key, std::function<T(const Key&, ICache<Key, T>*)> fetch_func) override {
        auto hit = hash_.find(key);

        // 1. Попадание (Hit) -> увеличиваем частоту
        if (hit != hash_.end()) {
            hits_count_++;
            NodeInfo& info = hit->second;
            int old_freq = info.freq;
            int new_freq = old_freq + 1;

            auto& old_bucket = freq_buckets_[old_freq];
            auto& new_bucket = freq_buckets_[new_freq];

            // Перемещаем узел списка из старого бакета в новый за O(1) без копирования T
            new_bucket.splice(new_bucket.begin(), old_bucket, info.it);
            
            info.freq = new_freq;
            info.it = new_bucket.begin();

            // Если старый бакет опустел, удаляем его
            if (old_bucket.empty()) {
                freq_buckets_.erase(old_freq);
                // Если мы удалили минимальный бакет, сдвигаем min_freq_ вверх
                if (min_freq_ == old_freq) {
                    min_freq_ = new_freq;
                }
            }

            return info.it->second;
        }

        // 2. Промах (Miss)
        misses_count_++;

        if (full()) {
            auto& lst_min = freq_buckets_[min_freq_];
            Key evict_key = lst_min.back().first;
            
            lst_min.pop_back();
            if (lst_min.empty()) {
                freq_buckets_.erase(min_freq_);
            }
            hash_.erase(evict_key);
        }

        // Запрашиваем новые данные
        T page = fetch_func(key, this->next_cache_layer);

        // Вставляем с частотой 1
        freq_buckets_[1].push_front({key, std::move(page)});
        hash_[key] = {1, freq_buckets_[1].begin()};
        min_freq_ = 1;

        return freq_buckets_[1].front().second;
    }
};

#endif // LFU_CACHE_HPP
