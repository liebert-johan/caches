#ifndef LRU_CACHE_HPP
#define LRU_CACHE_HPP

#include "cache.hpp"
#include <list>
#include <unordered_map>
#include <utility>
#include <functional>
#include <cstdint>
#include <stdexcept>

template <typename Key, typename T>
class LruCache : public ICache<Key, T> {
private:
    size_t sz_;
    int64_t hits_count_ = 0;
    int64_t misses_count_ = 0;

    std::list<std::pair<Key, T>> cache_;
    using ListIt = typename std::list<std::pair<Key, T>>::iterator;
    std::unordered_map<Key, ListIt> hash_;

public:
    explicit LruCache(size_t sz) : sz_(sz) {
        if (sz_ == 0) {
            throw std::invalid_argument("Cache capacity must be positive");
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
        return cache_.size() >= sz_;
    }

    T lookup_update(const Key& key, std::function<T(const Key&, ICache<Key, T>*)> fetch_func) override {
        auto hit = hash_.find(key);

        // 1. Попадание (Hit) -> перемещаем элемент в начало списка (MRU)
        if (hit != hash_.end()) {
            hits_count_++;
            auto eltit = hit->second;
            cache_.splice(cache_.begin(), cache_, eltit);
            return cache_.front().second;
        }

        // 2. Промах (Miss) -> запрашиваем страницу со следующего уровня или сервера
        misses_count_++;
        T page = fetch_func(key, this->next_cache_layer);

        // Если кэш полон, вытесняем наименее востребованный элемент из хвоста (LRU)
        if (full()) {
            hash_.erase(cache_.back().first);
            cache_.pop_back();
        }

        cache_.emplace_front(key, std::move(page));
        hash_[key] = cache_.begin();
        return cache_.front().second;
    }

    T front() const { 
        return cache_.front().second; 
    }
};

#endif // LRU_CACHE_HPP
