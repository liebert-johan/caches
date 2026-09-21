#include <list>
#include <unordered_map>

enum BlockStatus { LIR, HIR_RESIDENT, HIR_NON_RESIDENT };

template<typename Key, typename T>
struct Block {
private:
    Key key;
    T block_data;
public:
    BlockStatus block_status;
};

template<typename Key, typename T> 
class Lirs_cache {
private:
    size_t main_size_limit_;
    size_t temp_size_limit_;

    using CacheList = std::list<Block<Key, T>>;
    using ListIterator = typename CacheList::iterator;

    std::unordered_map<Key, ListIterator> main_hash;
    CacheList main_cache;
    
    std::unordered_map<Key, ListIterator> temp_hash;
    CacheList temp_cache;

public:
    Lirs_cache(size_t main_size, size_t temp_size) 
        : main_size_limit_(main_size), temp_size_limit_(temp_size) {}

    bool full_lir() const { return main_hash.size() >= main_size_limit_; }
    bool full_hir() const { return temp_hash.size() >= temp_size_limit_; }
    
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
	    
    bool up_main(const Key& key) {
      auto main_it = main_hash.find(key);
      if (main_it == main_hash.end()) {
          return false;
      }
        
      main_cache.splice(main_cache.begin(), main_cache, main_it->second);
      main_it->second->block_status = LIR;
      return true;
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
            demote_main_elem();
            clean_main();
        } 
        else if (prev_status == HIR_NON_RESIDENT) { 
            demote_main_elem();
        }  
        return true;
    }
 
    template<typename F>
    T get_file(const Key& key, F fetch_func) {
        if (in_main_cache(key)) {
            ListIterator main_it = main_hash[key];
            if (main_it->block_status == HIR_NON_RESIDENT) {
                main_it->block_data = fetch_func(key);
                delete_from_tempo(key);
                pull_up_main(key);
            } else {
                pull_up_main(key);
            }
            return main_it->block_data;
        }
        Block<Key, T> new_block = request_for_server(key, fetch_func);
        push_temp(new_block);
        push_front_main(new_block);
        return main_hash[key]->block_data;
    }

private:
  private:
    template<typename F>
    Block<Key, T> request_for_server(const Key& key, F fetch_func) {
        return Block<Key, T>{key, fetch_func(key), HIR_RESIDENT};
    }
    bool push_temp(const Block<Key, T>& block) {
        if (full_hir() && !temp_cache.empty()) {
            const Key& oldest_key = temp_cache.front().key;
            
            auto main_it = main_hash.find(oldest_key);
            if (main_it != main_hash.end()) {
                main_it->second->block_status = HIR_NON_RESIDENT;
                main_it->second->block_data = T();
            }
            temp_hash.erase(oldest_key);
            temp_cache.pop_front();
        }
        
        temp_cache.push_back(block);
        temp_cache.back().block_status = HIR_RESIDENT;
        temp_hash[block.key] = std::prev(temp_cache.end());
        return true;
    }
    bool demote_main_elem() {
        if (main_cache.empty()) return false;
        auto it = main_cache.end();
        while (it != main_cache.begin()) {
            --it;
            if (it->block_status == LIR) {
                Block<Key, T> block_to_demote = *it;
                
                main_hash.erase(it->key);
                main_cache.erase(it);
                
                return push_temp(block_to_demote);
            }
        }
        return false;
    }
    void push_front_main(const Block<Key, T>& block) {
        main_cache.push_front(block);
        main_hash[block.key] = main_cache.begin();
    }

};
