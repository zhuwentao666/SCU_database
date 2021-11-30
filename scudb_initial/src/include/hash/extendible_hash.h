/*
 * extendible_hash.h : implementation of in-memory hash table using extendible
 * hashing
 *
 * Functionality: The buffer pool manager must maintain a page table to be able
 * to quickly map a PageId to its corresponding memory location; or alternately
 * report that the PageId does not match any currently-buffered page.
 */

#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hash/hash_table.h"

namespace scudb {          

template <typename K, typename V>
class ExtendibleHash : public HashTable<K, V> {
  //自定义桶
  struct Bucket
  {
    Bucket() = default;//隐式构造生成
    explicit Bucket(size_t i, int d) : id(i), depth(d) {}//显示构造指定
    std::map<K, V> items;          // 桶里有键值对
    size_t id = 0;                 // 桶应有ID
    int depth = 0;                 // 桶应有自己的深度
  };
public:
  // constructor
  ExtendibleHash(size_t size);
  // helper function to generate hash addressing
  size_t HashKey(const K &key);
  // helper function to get global & local depth
  int GetGlobalDepth() const;
  int GetLocalDepth(int bucket_id) const;
  int GetNumBuckets() const;
  // lookup and modifier
  bool Find(const K &key, V &value) override;
  bool Remove(const K &key) override;
  void Insert(const K &key, const V &value) override;

private:
  // add your own member variables here
  mutable std::mutex mutex_;      // 在C ++ 11中始终将std :: mutex声明为mutable,加锁操作是要修改加锁的对象,而这个对象可能为const.

  std::vector<std::shared_ptr<Bucket>> bucket_;    // 桶数组,基于上面的Bucket

  std::shared_ptr<Bucket> split(std::shared_ptr<Bucket> &); // 分裂新桶

  int depth;              // 全局的桶的深度

  const size_t bucket_size_;    // 每个桶能容纳的元素个数

  int bucket_count_;   // 桶数

  size_t pair_count_;     // 哈希表中键值对的个数



};
} // namespace scudb
