#include <iostream>
#include <list>
#include <unordered_map>

template <typename T, typename KeyT = int>
class cache_t {
private:
    size_t sz_ = 0;
    size_t alin_cap_ = 0;
    size_t alout_cap_ = 0;

    std::list<std::pair<KeyT, T>> alin_;
    std::list<std::pair<KeyT, T>> am_;
    std::list<KeyT> alout_;

    using ItemIt = typename std::list<std::pair<KeyT, T>>::iterator;
    using KeyIt = typename std::list<KeyT>::iterator;

    std::unordered_map<KeyT, ItemIt> alin_hash_;
    std::unordered_map<KeyT, ItemIt> am_hash_;
    std::unordered_map<KeyT, KeyIt> alout_hash_;

public:
    cache_t(size_t sz, size_t alin_cap, size_t alout_cap)
        : sz_(sz), alin_cap_(alin_cap), alout_cap_(alout_cap)
    {
        if (sz_ == 0 || alin_cap_ == 0 || alin_cap_ > sz_) {
            throw std::invalid_argument("Bad cache sizes");
        }
    }

    bool full() const { return alin_.size() + am_.size() >= sz_; }

    template <typename F>
    bool lookup_update(KeyT key, F slow_get_page);

    void evict_from_alin();
    void evict_from_am();
    void add_ghost(KeyT key);
};


template <typename T, typename KeyT>
void cache_t<T, KeyT>::evict_from_alin() {
    if (alin_.empty()) return;

    KeyT key = alin_.front().first;
    alin_hash_.erase(key);
    alin_.pop_front();

    add_ghost(key);
}


template <typename T, typename KeyT>
void cache_t<T, KeyT>::evict_from_am() {
    if (am_.empty()) return;

    KeyT key = am_.back().first;
    am_hash_.erase(key);
    am_.pop_back();
}


template <typename T, typename KeyT>
void cache_t<T, KeyT>::add_ghost(KeyT key) {
    if (alout_cap_ == 0) return;

    if (alout_.size() >= alout_cap_) {
        KeyT old = alout_.front();
        alout_hash_.erase(old);
        alout_.pop_front();
    }

    alout_.push_back(key);
    alout_hash_[key] = std::prev(alout_.end());
}


template <typename T, typename KeyT>
template <typename F>
bool cache_t<T, KeyT>::lookup_update(KeyT key, F slow_get_page) {
    auto am_it = am_hash_.find(key);
    if (am_it != am_hash_.end()) {
        am_.splice(am_.begin(), am_, am_it->second);
        return true;
    }

    auto alin_it = alin_hash_.find(key);
    if (alin_it != alin_hash_.end()) {
        am_.splice(am_.begin(), alin_, alin_it->second);
        am_hash_[key] = am_.begin();
        alin_hash_.erase(alin_it);
        return true;
    }

    auto ghost_it = alout_hash_.find(key);
    T page = slow_get_page(key);

    if (ghost_it != alout_hash_.end()) {
        alout_.erase(ghost_it->second);
        alout_hash_.erase(ghost_it);

        if (full()) evict_from_am();

        am_.push_front({key, page});
        am_hash_[key] = am_.begin();
        return false;
    }

    if (alin_.size() >= alin_cap_) {
        evict_from_alin();
    } else if (full()) {
        evict_from_am();
    }

    alin_.push_back({key, page});
    alin_hash_[key] = std::prev(alin_.end());
    return false;
}


template <typename KeyT>
KeyT slow_get_page(KeyT key) {
    return key;
}


int main() {
    size_t m;
    std::cin >> m;

    cache_t<int> c(m, m / 2 + 1, m);
    int input[16];

    for (int i = 0; i < 16; i++) {
        std::cin >> input[i];
        bool hit = c.lookup_update(input[i], slow_get_page<int>);
        std::cout << (hit ? "hit " : "miss ") << input[i] << "\n";
    }
}
