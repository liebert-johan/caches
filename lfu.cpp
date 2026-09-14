
#include <iostream>
#include <list>
#include <unordered_map>





template <typename T, typename KeyT = int>
class cache_t {
private:
    size_t sz_ = 0;
    int min_freq_ = 1;

    struct NodeInfo{
        int freq;
        typename std::list<std::pair<KeyT, T>>::iterator it;
    };
    std::unordered_map<KeyT, NodeInfo> hash_;


public:

    std::unordered_map<int, std::list<std::pair<KeyT, T>>> freq_buckets_;

    cache_t(size_t sz) : sz_(sz) {}

    bool full() const { return hash_.size() >= sz_; }

    template <typename F>
    bool lookup_update(KeyT key, F slow_get_page);

    void remove_from_bucket(KeyT key);
};


template <typename KeyT, typename F>
F slow_get_page(KeyT key) {
    return key;
}


template <typename T, typename KeyT>
template <typename F>
bool cache_t<T, KeyT>::lookup_update(KeyT key, F slow_get_page) {
    auto hit = hash_.find(key);
    auto& lst_min = freq_buckets_[min_freq_];

    if (hit == hash_.end()) {
        if (full()) {
            auto& min_key = lst_min.back().first;

            remove_from_bucket(min_key);
            hash_.erase(min_key);
        }
        T page = slow_get_page(key);

        freq_buckets_[1].push_front({key, page});
        hash_[key] = {1, freq_buckets_[1].begin()};

        min_freq_ = 1;
        return false;
    }
    T page = hit->second.it->second;
    remove_from_bucket(key);

    freq_buckets_[hit -> second.freq + 1].push_front({key, page});

    hit -> second.freq++;
    hit -> second.it = freq_buckets_[hit -> second.freq].begin();

    return true;
}

template <typename T, typename KeyT>
void cache_t<T, KeyT>::remove_from_bucket(KeyT key) {
    NodeInfo& info = hash_[key];
    auto& bucket = freq_buckets_[info.freq];

    bucket.erase(info.it);

    if (bucket.empty()) {
        freq_buckets_.erase(info.freq);
        if (info.freq == min_freq_) {
            min_freq_++;
        }
    }
}


int main() {
    size_t m;
    std::cin >> m;

    cache_t<int> c(m);
    int input[16];

    for (int i = 0; i < 16; i++) {
        std::cin >> input[i];
        bool hit = c.lookup_update(input[i], slow_get_page<int, int>);
        std::cout << (hit ? "hit " : "miss ") << input[i] << "\n";
    }
}
