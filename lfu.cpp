#include <iostream>
#include <list>
#include <stdexcept>
#include <unordered_map>

template <typename T, typename KeyT = int>
class cache_t {
private:
    size_t sz_ = 0;
    int min_freq_ = 1;

    struct NodeInfo {
        int freq;
        typename std::list<std::pair<KeyT, T>>::iterator it;
    };

    std::unordered_map<KeyT, NodeInfo> hash_;
    std::unordered_map<int, std::list<std::pair<KeyT, T>>> freq_buckets_;

public:
    cache_t(size_t sz) : sz_(sz) {
        if (sz_ == 0) {
            throw std::invalid_argument("Cache size must be positive");
        }
    }

    bool full() const { return hash_.size() >= sz_; }

    template <typename F>
    bool lookup_update(KeyT key, F slow_get_page);

    void remove_from_bucket(KeyT key);
};


template <typename T, typename KeyT>
void cache_t<T, KeyT>::remove_from_bucket(KeyT key) {
    auto found = hash_.find(key);
    if (found == hash_.end()) {
        return;
    }

    NodeInfo& info = found->second;
    auto& bucket = freq_buckets_[info.freq];

    bucket.erase(info.it);

    if (bucket.empty()) {
        freq_buckets_.erase(info.freq);

        if (info.freq == min_freq_) {
            min_freq_++;
        }
    }
}


template <typename T, typename KeyT>
template <typename F>
bool cache_t<T, KeyT>::lookup_update(KeyT key, F slow_get_page) {
    auto hit = hash_.find(key);

    if (hit == hash_.end()) {
        if (full()) {
            auto& lst_min = freq_buckets_[min_freq_];
            KeyT evict_key = lst_min.back().first;

            remove_from_bucket(evict_key);
            hash_.erase(evict_key);
        }

        T page = slow_get_page(key);

        freq_buckets_[1].push_front({key, page});
        hash_[key] = {1, freq_buckets_[1].begin()};

        min_freq_ = 1;
        return false;
    }

    T page = hit->second.it->second;
    int new_freq = hit->second.freq + 1;

    remove_from_bucket(key);

    freq_buckets_[new_freq].push_front({key, page});
    hit->second.freq = new_freq;
    hit->second.it = freq_buckets_[new_freq].begin();

    return true;
}


template <typename KeyT>
KeyT slow_get_page(KeyT key) {
    return key;
}


int main() {
    size_t m;
    std::cin >> m;

    cache_t<int> c(m);
    int input[16];

    for (int i = 0; i < 16; i++) {
        std::cin >> input[i];
        bool hit = c.lookup_update(input[i], slow_get_page<int>);
        std::cout << (hit ? "hit " : "miss ") << input[i] << "\n";
    }
}
