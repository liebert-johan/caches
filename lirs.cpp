#include <list>
#include <unordered_map>
#include <iostream>
#include <stdexcept>

enum BlockStatus { LIR, HIR_RESIDENT, HIR_NON_RESIDENT };

template<typename Key, typename T>
struct Block {
    Key key;
    T block_data;
    BlockStatus block_status;
};

template<typename Key, typename T> 
class Lirs_cache {
private:
    size_t main_size_limit_;
    size_t temp_size_limit_;

    using CacheList = std::list<Block<Key, T>>;
    using ListIterator = typename CacheList::iterator;
    using TempList = std::list<Key>;
    using TempIterator = typename TempList::iterator;
    
    std::unordered_map<Key, ListIterator> main_hash;
    CacheList main_cache;
    
    std::unordered_map<Key, TempIterator> temp_hash;
    TempList temp_cache;

public:
    Lirs_cache(size_t main_size, size_t temp_size) 
        : main_size_limit_(main_size), temp_size_limit_(temp_size) {
        if ((main_size <= 0) || (temp_size <= 0)) {
            throw std::invalid_argument("cache sizes must be positive"); 
        }         
    }
    
    bool full_main() const { return main_hash.size() >= main_size_limit_; }
    bool full_temp() const { return temp_hash.size() >= temp_size_limit_; }
    
    bool in_main_cache(const Key& key) const { return main_hash.find(key) != main_hash.end(); }
    bool in_temp_cache(const Key& key) const { return temp_hash.find(key) != temp_hash.end(); }
    
    void clean_main() {
        while (!main_cache.empty()) {
            auto it = std::prev(main_cache.end());
            if (it->block_status == HIR_NON_RESIDENT) {
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

    bool delete_from_main(const Key& key) {
        auto it = main_hash.find(key);
        if (it == main_hash.end()) {
            return false;
        }
        main_cache.erase(it->second);
        main_hash.erase(it);
        return true;
    }       

    bool up_main(const Key& key) {
        auto main_it = main_hash.find(key);
        if (main_it == main_hash.end()) {
            return false;
        }
        main_cache.splice(main_cache.begin(), main_cache, main_it->second);
        main_it->second->block_status = LIR;
        return true;
    }
 
    template<typename F>
    T get_file(const Key& key, F fetch_func) {
        if (in_main_cache(key)) {
            ListIterator main_it = main_hash[key];
            
            if (main_it->block_status == HIR_NON_RESIDENT) {
                main_it->block_data = fetch_func(key);
            }
            
            pull_up_main(main_it->key);
            return main_it->block_data;
        }
        
        Block<Key, T> new_block = request_for_server(key, fetch_func);
        push_back_temp(new_block);
        push_front_main(new_block);
        return main_hash[key]->block_data;
    }

private:
    template<typename F>
    Block<Key, T> request_for_server(const Key& key, F fetch_func) {
        return Block<Key, T>{key, fetch_func(key), HIR_RESIDENT};
    }

    bool demote_main_elem() {
        if (main_cache.empty()) return false;
        auto it = main_cache.end();
        while (it != main_cache.begin()) {
            --it;
            if (it->block_status == LIR) {
                it->block_status = HIR_RESIDENT;
                return push_back_temp(*it); 
            }
        }
        return false;
    }

    bool pull_up_main(const Key& key) { 
        if (!in_main_cache(key)) { 
            return false;
        }
        auto main_map_it = main_hash.find(key);
        ListIterator main_it = main_map_it->second;
        BlockStatus prev_status = main_it->block_status;
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
        }
        return true;
    } 

    bool push_back_temp(const Block<Key, T>& block) {
        if (full_temp()) {
            Key oldest_key = temp_cache.front();
              
            auto main_it = main_hash.find(oldest_key);
            if (main_it != main_hash.end()) {
                main_it->second->block_status = HIR_NON_RESIDENT;
                clean_main();
            }
            temp_hash.erase(oldest_key);
            temp_cache.pop_front();
        }
  
        temp_cache.push_back(block.key);
        temp_hash[block.key] = std::prev(temp_cache.end());
        return true;
    }

    void push_front_main(const Block<Key, T>& block) {
        if (full_main()) {
            const Key& oldest_key = main_cache.back().key;
            auto main_it = main_hash.find(oldest_key);
            if (main_it != main_hash.end()) { 
                main_it->second->block_status = HIR_NON_RESIDENT;
                clean_main();
            } 
        }       
        main_cache.push_front(block);
        main_hash[block.key] = main_cache.begin();
    }
};
