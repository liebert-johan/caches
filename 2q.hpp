#ifndef TWO_Q_CACHE_HPP
#define TWO_Q_CACHE_HPP

#include "cache.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <functional>
#include <cstdint>

template <typename Key, typename T>
class TwoQCache : public ICache<Key, T> {
private:
    size_t sz_ = 0;
    size_t alin_cap_ = 0;
    size_t alout_cap_ = 0;

    int64_t hits_count_ = 0;
    int64_t misses_count_ = 0;

    std::list<std::pair<Key, T>> alin_;
    std::list<std::pair<Key, T>> am_;
    std::list<Key> alout_;

    using ItemIt = typename std::list<std::pair<Key, T>>::iterator;
    using KeyIt = typename std::list<Key>::iterator;

    std::unordered_map<Key, ItemIt> alin_hash_;
    std::unordered_map<Key, ItemIt> am_hash_;
    std::unordered_map<Key, KeyIt> alout_hash_;

public:
    // Конструктор с явным указанием емкостей очередей
    TwoQCache(size_t sz, size_t alin_cap, size_t alout_cap)
        : sz_(sz), alin_cap_(alin_cap), alout_cap_(alout_cap) {
        if (sz_ == 0 || alin_cap_ == 0 || alin_cap_ > sz_) {
            throw std::invalid_argument("Bad cache sizes");
        }
        this->next_cache_layer = nullptr;
    }

    // Упрощенный конструктор по общей емкости (дефолтные пропорции статьи 2Q: ~25% Kin, 50% Kout)
    explicit TwoQCache(size_t sz)
        : TwoQCache(sz, 
                    sz / 4 > 0 ? sz / 4 : 1, 
                    sz / 2 > 0 ? sz / 2 : 1) {}

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
        return alin_.size() + am_.size() >= sz_;
    }

    T lookup_update(const Key& key, std::function<T(const Key&, ICache<Key, T>*)> fetch_func) override {
        // 1. Попадание в очередь долговременных обращений (Am)
        auto am_it = am_hash_.find(key);
        if (am_it != am_hash_.end()) {
            hits_count_++;
            am_.splice(am_.begin(), am_, am_it->second);
            return am_.begin()->second;
        }

        // 2. Попадание во входную очередь FIFO (A1in) -> перевод в Am
        auto alin_it = alin_hash_.find(key);
        if (alin_it != alin_hash_.end()) {
            hits_count_++;
            am_.splice(am_.begin(), alin_, alin_it->second);
            am_hash_[key] = am_.begin();
            alin_hash_.erase(alin_it);
            return am_.begin()->second;
        }

        // 3. Промах данных (нужно запрашивать со следующего слоя или сервера)
        misses_count_++;
        T page = fetch_func(key, this->next_cache_layer);

        // 3а. Промах с попаданием в историю призраков (A1out) -> добавление сразу в Am
        auto ghost_it = alout_hash_.find(key);
        if (ghost_it != alout_hash_.end()) {
            alout_.erase(ghost_it->second);
            alout_hash_.erase(ghost_it);

            if (full()) {
                evict_from_am();
            }

            am_.push_front({key, std::move(page)});
            am_hash_[key] = am_.begin();
            return am_.begin()->second;
        }

        // 3б. Полный промах -> вставка в A1in
        if (alin_.size() >= alin_cap_) {
            evict_from_alin();
        } else if (full()) {
            evict_from_am();
        }

        alin_.push_back({key, std::move(page)});
        alin_hash_[key] = std::prev(alin_.end());
        return alin_.back().second;
    }

private:
    void evict_from_alin() {
        if (alin_.empty()) return;

        Key key = alin_.front().first;
        alin_hash_.erase(key);
        alin_.pop_front();

        add_ghost(key);
    }

    void evict_from_am() {
        if (am_.empty()) return;

        Key key = am_.back().first;
        am_hash_.erase(key);
        am_.pop_back();
    }

    void add_ghost(const Key& key) {
        if (alout_cap_ == 0) return;

        if (alout_.size() >= alout_cap_) {
            Key old = alout_.front();
            alout_hash_.erase(old);
            alout_.pop_front();
        }

        alout_.push_back(key);
        alout_hash_[key] = std::prev(alout_.end());
    }
};

#endif // TWO_Q_CACHE_HPP
