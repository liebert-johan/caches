#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

template <typename T, typename KeyT = int>
class cache_t {
private:
    size_t c_ = 0;
    size_t p_ = 0;

    // Начало списка — MRU, конец — LRU.
    std::list<std::pair<KeyT, T>> t1_;
    std::list<std::pair<KeyT, T>> t2_;
    std::list<KeyT> b1_;
    std::list<KeyT> b2_;

    using ItemIt = typename std::list<std::pair<KeyT, T>>::iterator;
    using KeyIt = typename std::list<KeyT>::iterator;

    std::unordered_map<KeyT, ItemIt> t1_hash_;
    std::unordered_map<KeyT, ItemIt> t2_hash_;
    std::unordered_map<KeyT, KeyIt> b1_hash_;
    std::unordered_map<KeyT, KeyIt> b2_hash_;

    // T1 -> B1: значение удаляется, ключ остаётся в истории.
    void evict_from_t1() {
        KeyT key = t1_.back().first;

        b1_.push_front(key);
        b1_hash_[key] = b1_.begin();

        t1_hash_.erase(key);
        t1_.pop_back();
    }

    // T2 -> B2.
    void evict_from_t2() {
        KeyT key = t2_.back().first;

        b2_.push_front(key);
        b2_hash_[key] = b2_.begin();

        t2_hash_.erase(key);
        t2_.pop_back();
    }

    // Особый случай ARC: удалить из T1, НЕ добавляя ключ в B1.
    void remove_t1_lru() {
        KeyT key = t1_.back().first;
        t1_hash_.erase(key);
        t1_.pop_back();
    }

    void remove_b1_lru() {
        KeyT key = b1_.back();
        b1_hash_.erase(key);
        b1_.pop_back();
    }

    void remove_b2_lru() {
        KeyT key = b2_.back();
        b2_hash_.erase(key);
        b2_.pop_back();
    }

    void remove_from_b1(const KeyT& key) {
        auto it = b1_hash_.find(key);
        b1_.erase(it->second);
        b1_hash_.erase(it);
    }

    void remove_from_b2(const KeyT& key) {
        auto it = b2_hash_.find(key);
        b2_.erase(it->second);
        b2_hash_.erase(it);
    }

    void add_to_t1(const KeyT& key, T page) {
        t1_.push_front({key, std::move(page)});
        t1_hash_[key] = t1_.begin();
    }

    void add_to_t2(const KeyT& key, T page) {
        t2_.push_front({key, std::move(page)});
        t2_hash_[key] = t2_.begin();
    }

    // Освободить место для загружаемого элемента.
    // key_was_in_b2 нужен при равенстве |T1| == p.
    void replace(bool key_was_in_b2) {
        if (!t1_.empty() &&
            (t1_.size() > p_ ||
             (key_was_in_b2 && t1_.size() == p_))) {
            evict_from_t1();
        } else {
            evict_from_t2();
        }
    }

public:
    explicit cache_t(size_t c) : c_(c) {
        if (c_ == 0) {
            throw std::invalid_argument("Bad cache size");
        }
    }

    template <typename F>
    bool lookup_update(KeyT key, F slow_get_page) {
        // 1. Попадание в T1: переносим элемент в начало T2.
        auto t1_it = t1_hash_.find(key);
        if (t1_it != t1_hash_.end()) {
            t2_.splice(t2_.begin(), t1_, t1_it->second);

            t2_hash_[key] = t2_.begin();
            t1_hash_.erase(t1_it);

            return true;
        }

        // 2. Попадание в T2: обновляем LRU-позицию.
        auto t2_it = t2_hash_.find(key);
        if (t2_it != t2_hash_.end()) {
            t2_.splice(t2_.begin(), t2_, t2_it->second);
            return true;
        }

        // В B1/B2 лежат только ключи, поэтому при любом из
        // оставшихся случаев значение нужно загрузить заново.
        T page = slow_get_page(key);

        // 3. Попадание в B1: увеличиваем p, кладём элемент в T2.
        if (b1_hash_.find(key) != b1_hash_.end()) {
            size_t delta = std::max<size_t>(
                1, b2_.size() / b1_.size()
            );

            p_ += std::min(delta, c_ - p_);

            // Удаляем ключ из B1 до replace().
            remove_from_b1(key);
            replace(false);

            add_to_t2(key, std::move(page));
            return false;
        }

        // 4. Попадание в B2: уменьшаем p, кладём элемент в T2.
        if (b2_hash_.find(key) != b2_hash_.end()) {
            size_t delta = std::max<size_t>(
                1, b1_.size() / b2_.size()
            );

            p_ -= std::min(delta, p_);

            // Передаём true: replace() должен знать, что
            // запрошенный ключ находился именно в B2.
            remove_from_b2(key);
            replace(true);

            add_to_t2(key, std::move(page));
            return false;
        }

        // 5. Полный промах: ключа нет ни в одном списке.
        size_t l1_size = t1_.size() + b1_.size();
        size_t all_size =
            l1_size + t2_.size() + b2_.size();

        if (l1_size == c_) {
            if (t1_.size() < c_) {
                remove_b1_lru();
                replace(false);
            } else {
                remove_t1_lru();
            }
        } else if (all_size >= c_) {
            if (all_size == 2 * c_) {
                remove_b2_lru();
            }

            replace(false);
        }

        add_to_t1(key, std::move(page));
        return false;
    }

    void print_state() const {
        std::cout << "p=" << p_
                  << " | T1=" << t1_.size()
                  << " T2=" << t2_.size()
                  << " B1=" << b1_.size()
                  << " B2=" << b2_.size()
                  << '\n';
    }
};

template <typename KeyT>
KeyT slow_get_page(KeyT key) {
    return key;
}

int main() {
    size_t m;
    std::cin >> m;

    cache_t<int> c(m);
    int input[16];

    for (int i = 0; i < 16; ++i) {
        std::cin >> input[i];

        bool hit = c.lookup_update(
            input[i],
            slow_get_page<int>
        );

        std::cout << (hit ? "hit " : "miss ")
                  << input[i] << '\n';

        c.print_state();
    }
}
