#pragma once

#include <cstddef>
#include <functional>
#include <stdint.h>
#include <memory>
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include "cache.hpp"

#ifndef LIRS_CACHE_HPP
#define LIRS_CACHE_HPP

enum class BlockStatus { 
    LIR, 
    HIR_RESIDENT, 
    HIR_NON_RESIDENT 
};

template<typename Key, typename T>
struct LirsNode {
    std::unique_ptr<T> data;
    BlockStatus status;
    
    bool is_in_S = false;
    typename std::list<Key>::iterator S_it;
    
    bool is_in_Q = false;
    typename std::list<Key>::iterator Q_it;
};

template<typename Key, typename T>
class Lirs_cache : public ICache<Key, T> {
private:
    size_t lir_capacity_;
    size_t hir_capacity_;
    size_t max_S_size_;
    size_t lir_count_;
    
    int64_t hits_;
    int64_t misses_;
    
    std::list<Key> stack_S_; 
    std::list<Key> queue_Q_; 
    std::unordered_map<Key, LirsNode<Key, T>> storage_;
    
    void prune_stack_S() {
        while (!stack_S_.empty()) {
            Key bottom_key = stack_S_.back();
            LirsNode<Key, T>& node = storage_[bottom_key];
            
            if (node.status != BlockStatus::LIR) {
                node.is_in_S = false;
                stack_S_.pop_back();
                
                if (node.status == BlockStatus::HIR_NON_RESIDENT) {
                    storage_.erase(bottom_key);
                }
            } else {
                break;
            }
        }
    }

    void evict_from_Q() {
        while (queue_Q_.size() >= hir_capacity_) {
            Key evict_key = queue_Q_.front();
            queue_Q_.pop_front();
            LirsNode<Key, T>& node = storage_[evict_key];
            
            node.data.reset(); 
            node.is_in_Q = false;
            
            if (node.is_in_S) {
                node.status = BlockStatus::HIR_NON_RESIDENT;
            } else {
                storage_.erase(evict_key);
            }
        }
    }

    void demote_bottom_LIR() {
        if (lir_count_ > lir_capacity_ && !stack_S_.empty()) {
            Key bottom_key = stack_S_.back();
            LirsNode<Key, T>& node = storage_[bottom_key];
            
            if (node.status == BlockStatus::LIR) {
                node.status = BlockStatus::HIR_RESIDENT;
                lir_count_--;
                
                queue_Q_.push_back(bottom_key);
                node.Q_it = std::prev(queue_Q_.end());
                node.is_in_Q = true;
            }
        }
    }

    void enforce_history_limit() {
        while (stack_S_.size() > max_S_size_) {
            bool erased = false;
            auto it = stack_S_.rbegin();
            while (it != stack_S_.rend()) {
                Key k = *it;
                if (storage_[k].status == BlockStatus::HIR_NON_RESIDENT) {
                    storage_.erase(k);
                    stack_S_.erase((++it).base());
                    erased = true;
                    break;
                }
                ++it;
            }
            if (!erased) break;
        }
    }

    void handle_hit_in_stack_S(const Key& key, LirsNode<Key, T>& node) {
        if (node.status == BlockStatus::HIR_RESIDENT) {
            node.status = BlockStatus::LIR;
            lir_count_++;
            
            queue_Q_.erase(node.Q_it);
            node.is_in_Q = false;
            
            stack_S_.erase(node.S_it);
            stack_S_.push_front(key);
            node.S_it = stack_S_.begin();

            demote_bottom_LIR();
            prune_stack_S();
        } else if (node.status == BlockStatus::LIR) {
            stack_S_.erase(node.S_it);
            stack_S_.push_front(key);
            node.S_it = stack_S_.begin();
            prune_stack_S();
        }
    }

    void handle_hit_in_queue_Q(const Key& key, LirsNode<Key, T>& node) {
        stack_S_.push_front(key);
        node.S_it = stack_S_.begin();
        node.is_in_S = true;

        queue_Q_.erase(node.Q_it);
        queue_Q_.push_back(key);
        node.Q_it = std::prev(queue_Q_.end());
    }

    void handle_miss_non_resident(const Key& key, LirsNode<Key, T>& node, std::unique_ptr<T> fetched_data) {
        node.data = std::move(fetched_data);
        node.status = BlockStatus::LIR;
        lir_count_++;
        
        stack_S_.erase(node.S_it);
        stack_S_.push_front(key);
        node.S_it = stack_S_.begin();
        node.is_in_S = true;

        evict_from_Q();
        demote_bottom_LIR();
        prune_stack_S();
    }

    void handle_pure_miss(const Key& key, std::unique_ptr<T> fetched_data) {
        LirsNode<Key, T> new_node;
        new_node.data = std::move(fetched_data);
        
        if (lir_count_ < lir_capacity_) {
            new_node.status = BlockStatus::LIR;
            lir_count_++;
            stack_S_.push_front(key);
            new_node.S_it = stack_S_.begin();
            new_node.is_in_S = true;
        } else {
            new_node.status = BlockStatus::HIR_RESIDENT;
            evict_from_Q();
            
            stack_S_.push_front(key);
            new_node.S_it = stack_S_.begin();
            new_node.is_in_S = true;
            
            queue_Q_.push_back(key);
            new_node.Q_it = std::prev(queue_Q_.end());
            new_node.is_in_Q = true;
        }
        storage_[key] = std::move(new_node);
    }

public:
    Lirs_cache(size_t capacity, ICache<Key, T>* next_layer = nullptr, float distribution = 0.95f) 
        : lir_count_(0), hits_(0), misses_(0) {
        if (distribution <= 0.0f || distribution >= 1.0f) {
            throw std::invalid_argument("Distribution must be > 0 and < 1");
        }    
        
        lir_capacity_ = static_cast<size_t>(capacity * distribution);
        if (lir_capacity_ == 0 && capacity > 0) lir_capacity_ = 1;
        hir_capacity_ = (capacity > lir_capacity_) ? (capacity - lir_capacity_) : 1;
        max_S_size_ = (lir_capacity_ + hir_capacity_) * 3; 
        
        this->set_next_layer(next_layer);
    }

    void set_next_layer(ICache<Key, T>* next) override {
        this->next_cache_layer = next;
    } 
    Lirs_cache(const Lirs_cache&) = delete;
    Lirs_cache& operator=(const Lirs_cache&) = delete;
    
    int64_t get_hits() const override { return hits_; }
    size_t capacity() const override { return lir_capacity_ + hir_capacity_; }
    int64_t get_misses() const override { return misses_; } 

    T lookup_update(const Key& key, const std::function<T(const Key&, ICache<Key, T>*)>& fetch_func) override {
        auto it = storage_.find(key);
        bool in_cache = (it != storage_.end());

        if (in_cache && it->second.data != nullptr) {
            hits_++;
            if (it->second.is_in_S) {
                handle_hit_in_stack_S(key, it->second);
            } else if (it->second.is_in_Q) {
                handle_hit_in_queue_Q(key, it->second);
            }
            return *(it->second.data);
        }

        misses_++;
        std::unique_ptr<T> fetched_data = std::make_unique<T>(fetch_func(key, this->get_next_layer()));

        if (in_cache && it->second.status == BlockStatus::HIR_NON_RESIDENT) {
            handle_miss_non_resident(key, it->second, std::move(fetched_data));
        } else {
            handle_pure_miss(key, std::move(fetched_data));
        }
        
        enforce_history_limit();
        return *(storage_[key].data);
    }
};

#endif
