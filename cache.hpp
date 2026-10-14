#pragma once
#include <cstddef>
#include <functional>
#include <stdint.h>
template<typename Key, typename T>
class ICache {
public:
    class ICache<Key, T>* next_cache_layer;
    virtual ~ICache() = default;
    virtual T lookup_update(const Key& key, std::function<T(const Key&, ICache<Key, T>* next_cache_layer)> fetch_func) = 0;
    virtual size_t capacity() const = 0;
    virtual int64_t get_hits() const = 0;
    virtual int64_t get_misses() const = 0;
    virtual void set_next_layer(ICache<Key, T>* next) = 0;
};
