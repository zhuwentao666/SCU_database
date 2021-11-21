/**
 * lru_replacer.h
 *
 * Functionality: The buffer pool manager must maintain a LRU list to collect
 * all the pages that are unpinned and ready to be swapped. The simplest way to
 * implement LRU is a FIFO queue, but remember to dequeue or enqueue pages when
 * a page changes from unpinned to pinned, or vice-versa.
 */

#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>

#include "buffer/replacer.h"
#include "hash/extendible_hash.h"

namespace scudb {

template <typename T> class LRUReplacer : public Replacer<T> {
  //双向列表
      struct node {
        node() = default;
        explicit node(T d, node *p = nullptr) : data(d), pre(p) {}
        T data;
        node *pre = nullptr;
        node *next = nullptr;
    };
public:
  // do not change public interface
  LRUReplacer();

  ~LRUReplacer();

  void Insert(const T &value);

  bool Victim(T &value);

  bool Erase(const T &value);

  size_t Size();

private:
  // add your member variables here
        mutable std::mutex mutex_;

        size_t size_;

        std::unordered_map<T, node *> table_;//使用哈希表来进行快速查找

        node *head_;//链表头部

        node *tail_;//链表尾部
};

} // namespace scudb
