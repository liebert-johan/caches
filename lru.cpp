
#include <iostream>
#include <list>
#include <unordered_map>

template <typename T, typename KeyT = int>
class cache_t {
private:
    size_t sz_;
    std::list<std::pair<KeyT, T>> cache_;

    using ListIt = typename std::list<std::pair<KeyT, T>>::iterator;
    std::unordered_map<KeyT, ListIt> hash_;

public:
    cache_t(size_t sz) : sz_(sz) {}

    bool full() const;

    

    template <typename F>
    bool lookup_update(KeyT key, F slow_get_page);

    T front() const { return cache_.front().second; }
};

template <typename T, typename KeyT>
template <typename F>
bool cache_t<T, KeyT>::lookup_update(KeyT key, F slow_get_page) {

    auto hit = hash_.find(key);

    if (hit != hash_.end()) {
        auto eltit = hit->second;
        cache_.splice(cache_.begin(), cache_, eltit);
        return true;
    }

    T page = slow_get_page(key);
    if (full()) {
        hash_.erase(cache_.back().first);
        cache_.pop_back();
    }
    cache_.emplace_front(key, page);
    hash_.emplace(key, cache_.begin());
    return false;
}

template <typename U, typename K>
bool cache_t<U, K>::full() const {
    return cache_.size() >= sz_;
}

template <typename KeyT, typename F>
F slow_get_page(KeyT key) {
    return key;
}

int main() {
    size_t m;
    std::cin >> m;
    cache_t<int> c{m};
    int input[16];

    for (int i = 0; i < 16; i++) {
        std::cin >> input[i];
        bool hit = c.lookup_update(input[i], slow_get_page<int, int>);
        std::cout << (hit ? "hit " : "miss ") << c.front() << "\n";
    }
}
