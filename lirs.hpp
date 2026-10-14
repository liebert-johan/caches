#ifndef LIRS_CACHE_HPP
#define LIRS_CACHE_HPP
#include "cache.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <utility>

enum class BlockStatus { 
    LIR, 
    HIR_RESIDENT, 
    HIR_NON_RESIDENT 
};

template<typename Key, typename T>
struct Block {
    Key key;
    std::unique_ptr<T> block_data;
    BlockStatus block_status;
};

template<typename Key, typename T>
class Lirs_cache: public ICache<Key, T> {
private:

    size_t lir_size_limit_;
    size_t hir_size_limit_;
    size_t history_size_limit_;
    size_t current_lir_count_ = 0;
    float distribution = 0.95f;
    int64_t cache_hit_count_ = 0;
    int64_t cache_miss_count_ = 0;
    using CacheList = std::list<Block<Key, T>>;
    using ListIterator = typename CacheList::iterator;
    using TempList = std::list<Key>;
    using TempIterator = typename TempList::iterator;

    std::unordered_map<Key, ListIterator> main_hash;
    CacheList main_cache;

    std::unordered_map<Key, TempIterator> temp_hash;
    TempList temp_cache;
    
public:
    Lirs_cache(size_t capacity, class ICache<Key, T>* next_cache_layer_init) {
	if (distribution >= 1 || distribution <= 0) {
	    throw std::invalid_argument("distribution between lir and hir cache must be > 0 and < 1");
	}	
	lir_size_limit_ = static_cast<size_t>(capacity * distribution);
        if (lir_size_limit_ == 0 && capacity > 0) {
           lir_size_limit_ = 1;
         }
        hir_size_limit_ = (capacity > lir_size_limit_) ? (capacity - lir_size_limit_) : 1;
	if ((hir_size_limit_ == 0) || (lir_size_limit_ == 0)) {
            throw std::invalid_argument("cache sizes must be positive");
        }
        history_size_limit_ = 2 * lir_size_limit_ + hir_size_limit_;
	this->set(next_cache_layer_init);
    }
    void set_next_layer(ICache<Key, T>* next) override { this->next_cache_layer = next; }
    
    bool full_lir() const { return current_lir_count_ >= lir_size_limit_; }
    bool full_temp() const { return temp_hash.size() >= hir_size_limit_; }
    
    int64_t get_hits() const override {return cache_hit_count_;}
    size_t capacity() override {return lir_size_limit_ + hir_size_limit_;}
    int64_t get_misses() const override {return cache_miss_count_;} 
   
   
    bool in_main_cache(const Key& key) const { return main_hash.find(key) != main_hash.end(); }
    
    bool in_temp_cache(const Key& key) const { return temp_hash.find(key) != temp_hash.end(); }
    void clean_main() {
        while (!main_cache.empty()) {
            auto it = std::prev(main_cache.end());
            if (it->block_status != BlockStatus::LIR) {
                main_hash.erase(it->key);
                main_cache.pop_back();
            } else {
                break;
            }
        }
    }

    bool delete_from_tempo(const Key& key) {
        auto it = temp_hash.find(key);
        if (it == temp_hash.end()) {
            return false;
        }
        temp_cache.erase(it->second);
        temp_hash.erase(it);
        return true;
    }

    bool up_main(const Key& key) {
        auto main_it = main_hash.find(key);
        if (main_it == main_hash.end()) {
            return false;
        }
        main_cache.splice(main_cache.begin(), main_cache, main_it->second);

        if (main_it->second->block_status != BlockStatus::LIR) {
            main_it->second->block_status = BlockStatus::LIR;
            current_lir_count_++;
        }
        return true;
    }

    T lookup_update(const Key& key, const std::function<T(const Key&, ICache<Key, T>*)>& fetch_func) override {
        if (in_main_cache(key)) {
            ListIterator main_it = main_hash[key];
            if (main_it->block_status == BlockStatus::HIR_NON_RESIDENT) {
                main_it->block_data = std::make_unique<T>(fetch_func(key, ICache<Key, T>::next_cache_layer));
                cache_miss_count_++;
            } else {
                cache_hit_count_++;
            }
            pull_up_main(key);
            return *(main_hash[key]->block_data);
        }

        // Miss
        cache_miss_count_++;
        Block<Key, T> new_block = request(key, fetch_func);
        push_front_main(std::move(new_block));
        push_back_temp(key);

        if (!full_lir()) {
            main_hash[key]->block_status = BlockStatus::LIR;
            current_lir_count_++;
            delete_from_tempo(key);
        }

        return *(main_hash[key]->block_data);
    }
private:
    void enforce_history_limit() {
        if (main_hash.size() <= history_size_limit_) return;
        
	auto it = main_cache.rbegin();
        while (it != main_cache.rend()) {
            if (it->block_status == BlockStatus::HIR_NON_RESIDENT) {
                main_hash.erase(it->key);
                main_cache.erase(std::next(it).base());
                return;
            }
            ++it;
        }
    }

    template<typename F>
    Block<Key, T> request(const Key& key, F fetch_func) {
        return Block<Key, T>{key, std::make_unique<T>(fetch_func(key, ICache<Key, T>::next_cache_layer)), BlockStatus::HIR_RESIDENT};
    }

    bool demote_main_elem() {
        if (main_cache.empty()) return false;
        auto it = std::prev(main_cache.end());
        if (it->block_status == BlockStatus::LIR) {
            it->block_status = BlockStatus::HIR_RESIDENT;
            current_lir_count_--;
            push_back_temp(it->key);
            return true;
        }
        return false;
    }

    bool pull_up_main(const Key& key) {
        if (!in_main_cache(key)) return false;

        BlockStatus prev_status = main_hash[key]->block_status;
        up_main(key);

        if (prev_status == BlockStatus::LIR) {
            clean_main();
        } else if (prev_status == BlockStatus::HIR_RESIDENT) {
            delete_from_tempo(key);
            demote_main_elem();
            clean_main();
        } else if (prev_status == BlockStatus::HIR_NON_RESIDENT) {
            demote_main_elem();
            clean_main();
        }
        return true;
    }

    bool push_back_temp(const Key& key) {
        if (full_temp()) {
            Key oldest_key = temp_cache.front();

            auto main_it = main_hash.find(oldest_key);
            if (main_it != main_hash.end()) {
                main_it->second->block_status = BlockStatus::HIR_NON_RESIDENT;
                main_it->second->block_data.reset();
                clean_main();
                enforce_history_limit();
            }
            temp_hash.erase(oldest_key);
            temp_cache.pop_front();
        }

        temp_cache.push_back(key);
        temp_hash[key] = std::prev(temp_cache.end());
        return true;
    }

    void push_front_main(Block<Key, T> block) {
        main_cache.push_front(std::move(block));
        main_hash[main_cache.front().key] = main_cache.begin();
    }
};

#endif // LIRS_CACHE_HPP
