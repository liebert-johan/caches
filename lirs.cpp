#include <list>
#include <unordered_map>
#include <stdexcept>
#include <memory> 

enum BlockStatus { LIR, HIR_RESIDENT, HIR_NON_RESIDENT };
template<typename Key, typename T>
struct Block {
    Key key;
    std::unique_ptr<T> block_data;
    BlockStatus block_status;
};
template<typename Key, typename T> 
class Lirs_cache {
private:
    size_t lir_size_limit_;
    size_t hir_size_limit_;
    size_t history_size_limit_;
    size_t current_lir_count_ = 0;

    using CacheList = std::list<Block<Key, T>>;
    using ListIterator = typename CacheList::iterator;
    using TempList = std::list<Key>;
    using TempIterator = typename TempList::iterator;
    
    std::unordered_map<Key, ListIterator> main_hash;
    CacheList main_cache;
    
    std::unordered_map<Key, TempIterator> temp_hash;
    TempList temp_cache;

public:
    Lirs_cache(size_t lir_size, size_t hir_size) 
        : lir_size_limit_(lir_size), hir_size_limit_(hir_size) {
        if ((lir_size <= 0) || (hir_size <= 0)) {
            throw std::invalid_argument("cache sizes must be positive"); 
        }
        history_size_limit_ = 2 * lir_size + hir_size; 
    }
    bool full_lir() const { return current_lir_count_ >= lir_size_limit_; }
    bool full_temp() const { return temp_hash.size() >= hir_size_limit_; }
    bool in_main_cache(const Key& key) const { return main_hash.find(key) != main_hash.end(); }
    bool in_temp_cache(const Key& key) const { return temp_hash.find(key) != temp_hash.end(); }
    
    void clean_main() {
        while (!main_cache.empty()) {
            auto it = std::prev(main_cache.end());
            if (it->block_status != LIR) { 
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
        
        if (main_it->second->block_status != LIR) {
            main_it->second->block_status = LIR;
            current_lir_count_++;
        }
        return true;
    }
    template<typename F>
    T get_file(const Key& key, F fetch_func) {
        if (in_main_cache(key)) {
            ListIterator main_it = main_hash[key];
            if (main_it->block_status == HIR_NON_RESIDENT) {
                main_it->block_data = std::make_unique<T>(fetch_func(key));
            }
            pull_up_main(key);
            return *(main_hash[key]->block_data);
        }
        Block<Key, T> new_block = request_for_server(key, fetch_func);
        push_front_main(std::move(new_block)); 
        push_back_temp(key); 
        if (!full_lir()) {
            main_hash[key]->block_status = LIR;
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
            if (it->block_status == HIR_NON_RESIDENT) {
                main_hash.erase(it->key);
                main_cache.erase(std::next(it).base());
                return;
            }
            ++it;
        }
    }
    template<typename F>
    Block<Key, T> request_for_server(const Key& key, F fetch_func) {
        return Block<Key, T>{key, std::make_unique<T>(fetch_func(key)), HIR_RESIDENT};
    }
    bool demote_main_elem() {
        if (main_cache.empty()) return false;
        auto it = std::prev(main_cache.end()); 
        if (it->block_status == LIR) {
            it->block_status = HIR_RESIDENT;
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
        if (prev_status == LIR) { 
            clean_main();
        }
        else if (prev_status == HIR_RESIDENT) {
            delete_from_tempo(key); 
            demote_main_elem();
            clean_main();
        }
        else if (prev_status == HIR_NON_RESIDENT) {
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
                main_it->second->block_status = HIR_NON_RESIDENT;
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

