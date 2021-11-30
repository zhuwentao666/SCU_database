/**
 * b_plus_tree.cpp
 */
#include <iostream>
#include <string>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "index/b_plus_tree.h"
#include "page/header_page.h"

namespace scudb {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(const std::string &name,
                          BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator,
                          page_id_t root_page_id)
        : index_name_(name), root_page_id_(root_page_id),
          buffer_pool_manager_(buffer_pool_manager), comparator_(comparator) {}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::IsEmpty() const { 
  return root_page_id_ == INVALID_PAGE_ID;
 }

template <typename KeyType, typename ValueType, typename KeyComparator>
thread_local int BPlusTree<KeyType, ValueType, KeyComparator>::rootLockedCnt = 0;
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::GetValue(const KeyType &key,
                              std::vector<ValueType> &result,
                              Transaction *transaction) {
  //step 1. 找到叶子节点
  B_PLUS_TREE_LEAF_PAGE_TYPE *tar = FindLeafPage(key,false,OpType::READ,transaction);
  if (tar == nullptr)
    return false;
  //step 2. 叶节点有Lookup去匹配
  result.resize(1);
  auto ret = tar->Lookup(key,result[0],comparator_);
  //step 3. 记得释放
  FreePagesInTransaction(false,transaction,tar->GetPageId());
  return ret;
}


/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value,
                            Transaction *transaction) {
  LockRootPageId(true);//用根结点时需要加锁
  if (IsEmpty()==true) {
    StartNewTree(key,value);
    TryUnlockRootPageId(true);
    return true;
  }
  TryUnlockRootPageId(true);
  return InsertIntoLeaf(key,value,transaction);//返回是否成功的布尔值
}
/*
 * Insert constant key & value pair into an empty tree
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then update b+
 * tree's root page id and insert entry directly into leaf page.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::StartNewTree(const KeyType &key, const ValueType &value) {
  page_id_t newPageId;
  //申请一个新Page
  Page *rootPage = buffer_pool_manager_->NewPage(newPageId);
  if (rootPage == nullptr) throw "out of memory";
  //由提示可知，目前这个root是叶子节点类型
  B_PLUS_TREE_LEAF_PAGE_TYPE *root = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE *>(rootPage->GetData());
  root->Init(newPageId,INVALID_PAGE_ID);//(page_id,parent_id)
  root_page_id_ = newPageId;
  UpdateRootPageId(true);//更新根节点ID
  root->Insert(key,value,comparator_);
  buffer_pool_manager_->UnpinPage(newPageId,true);
}

/*
 * Insert constant key & value pair into leaf page
 * User needs to first find the right leaf page as insertion target, then look
 * through leaf page to see whether insert key exist or not. If exist, return
 * immdiately, otherwise insert entry. Remember to deal with split if necessary.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::InsertIntoLeaf(const KeyType &key, const ValueType &value,
                                    Transaction *transaction) {
  //找到对应的叶节点
  B_PLUS_TREE_LEAF_PAGE_TYPE *leafPage = FindLeafPage(key,false,OpType::INSERT,transaction);
  ValueType result;
  if (leafPage->Lookup(key,result,comparator_)) {//如果该键本身存在
    FreePagesInTransaction(true,transaction);
    return false;
  }
  //执行插入pair
  leafPage->Insert(key,value,comparator_);
  if (leafPage->GetSize() > leafPage->GetMaxSize()) {//如果超限了需要分裂，分裂函数之后实现
    B_PLUS_TREE_LEAF_PAGE_TYPE *newLeafPage = Split(leafPage,transaction);
    //分离后，需要将右边第一个值放入父节点，叶节点没有常占位
    InsertIntoParent(leafPage,newLeafPage->KeyAt(0),newLeafPage,transaction);
  }
  //释放
  FreePagesInTransaction(true,transaction);
  return true;

}

/*
 * Split input page and return newly created page.
 * Using template N to represent either internal page or leaf page.
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then move half
 * of key & value pairs from input page to newly created page
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N> N *BPLUSTREE_TYPE::Split(N *node,Transaction *transaction) { 
  //申请新page
  page_id_t newPageId;
  Page* const newPage = buffer_pool_manager_->NewPage(newPageId);
  if (newPage == nullptr) throw "out of memory";
  newPage->WLatch();
  transaction->AddIntoPageSet(newPage);
  //执行分裂和移动
  N *new_Node = reinterpret_cast<N *>(newPage->GetData());//生成的类型由N决定
  new_Node->Init(newPageId, node->GetParentPageId());
  node->MoveHalfTo(new_Node, buffer_pool_manager_);
  return new_Node;
 }

/*
 * Insert key & value pair into internal page after split
 * @param   old_node      input page from split() method
 * @param   key
 * @param   new_node      returned page from split() method
 * User needs to first find the parent page of old_node, parent node must be
 * adjusted to take info of new_node into account. Remember to deal with split
 * recursively if necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(BPlusTreePage *old_node,
                                      const KeyType &key,
                                      BPlusTreePage *new_node,
                                      Transaction *transaction) {
  //根节点的分裂需要生成新的page
  if (old_node->IsRootPage()) {
    Page* const newPage = buffer_pool_manager_->NewPage(root_page_id_);
    assert(newPage != nullptr);
    assert(newPage->GetPinCount() == 1);
    B_PLUS_TREE_INTERNAL_PAGE *newRoot = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(newPage->GetData());
    newRoot->Init(root_page_id_);
    //初始化这个根节点，使得它的常占位指向原page，第一个key的pointer指向新page
    newRoot->PopulateNewRoot(old_node->GetPageId(),key,new_node->GetPageId());
    old_node->SetParentPageId(root_page_id_);
    new_node->SetParentPageId(root_page_id_);
    UpdateRootPageId();
    buffer_pool_manager_->UnpinPage(newRoot->GetPageId(),true);
    return;
  }
  page_id_t parentId = old_node->GetParentPageId();
  auto *page = FetchPage(parentId);
  assert(page != nullptr);
  B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page);
  new_node->SetParentPageId(parentId);
  //显然，新的key应该立刻插入在旧节点在其parent的key之后
  parent->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
  if (parent->GetSize() > parent->GetMaxSize()) {
    //如果内节点溢出也要进行分裂
    B_PLUS_TREE_INTERNAL_PAGE *newInternalPage = Split(parent,transaction);//new page need unpin
    //常占位代表下界，故应该更新其父page
    InsertIntoParent(parent,newInternalPage->KeyAt(0),newInternalPage,transaction);
  }
  buffer_pool_manager_->UnpinPage(parentId,true);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immdiately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  if (IsEmpty()) return;
  B_PLUS_TREE_LEAF_PAGE_TYPE *target = FindLeafPage(key,false,OpType::DELETE,transaction);
  int curSize = target->RemoveAndDeleteRecord(key,comparator_);
  if (curSize < target->GetMinSize()) CoalesceOrRedistribute(target,transaction);
  FreePagesInTransaction(true,transaction);
}

/*
 * User needs to first find the sibling of input page. If sibling's size + input
 * page's size > page's max size, then redistribute. Otherwise, merge.
 * Using template N to represent either internal page or leaf page.
 * @return: true means target leaf page should be deleted, false means no
 * deletion happens
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
bool BPLUSTREE_TYPE::CoalesceOrRedistribute(N *node, Transaction *transaction) {
  //根节点是允许小于最小值的，但是当这个根节点有且仅有一个孩子，我们可以删去这个根节点并把它的孩子作为根节点。
  if (node->IsRootPage()) {
    bool delOldRoot = AdjustRoot(node);
    if (delOldRoot) {transaction->AddIntoDeletedPageSet(node->GetPageId());}
    return delOldRoot;
  }
  //node2是它的邻页
  N *node2;
  bool isRightSib = FindLeftSibling(node,node2,transaction);
  BPlusTreePage *parent = FetchPage(node->GetParentPageId());
  B_PLUS_TREE_INTERNAL_PAGE *parentPage = static_cast<B_PLUS_TREE_INTERNAL_PAGE *>(parent);
  //如果它们可以被放入同一个节点,则合并
  if (node->GetSize() + node2->GetSize() <= node->GetMaxSize()) {
    if (isRightSib) {std::swap(node,node2);} //默认希望node2是左邻页
    int removeIndex = parentPage->ValueIndex(node->GetPageId());
    Coalesce(node2,node,parentPage,removeIndex,transaction);
    buffer_pool_manager_->UnpinPage(parentPage->GetPageId(), true);
    return true;
  }
  //从node2取key，放在fisrt或者last位，node2在哪边
  int nodeInParentIndex = parentPage->ValueIndex(node->GetPageId());
  Redistribute(node2,node,nodeInParentIndex);//unpin node,node2
  buffer_pool_manager_->UnpinPage(parentPage->GetPageId(), false);
  return false;
}

INDEX_TEMPLATE_ARGUMENTS
template <typename N>
bool BPLUSTREE_TYPE::FindLeftSibling(N *node, N * &sibling, Transaction *transaction) {
  auto page = FetchPage(node->GetParentPageId());
  B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page);
  int index = parent->ValueIndex(node->GetPageId());
  int siblingIndex = index - 1;
  if (index == 0) siblingIndex = index + 1;
  sibling = reinterpret_cast<N *>(CrabingProtocalFetchPage(parent->ValueAt(siblingIndex),OpType::DELETE,-1,transaction));
  buffer_pool_manager_->UnpinPage(parent->GetPageId(), false);
  return index == 0;
}

/*
 * Move all the key & value pairs from one page to its sibling page, and notify
 * buffer pool manager to delete this page. Parent page must be adjusted to
 * take info of deletion into account. Remember to deal with coalesce or
 * redistribute recursively if necessary.
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 * @param   parent             parent page of input "node"
 * @return  true means parent node should be deleted, false means no deletion
 * happend
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
bool BPLUSTREE_TYPE::Coalesce(
    N *&neighbor_node, N *&node,
    BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *&parent,
    int index, Transaction *transaction) {
  //能进行合并的必要条件是两个节点的容量和不超过最大容量
  assert(node->GetSize() + neighbor_node->GetSize() <= node->GetMaxSize());
  //这个辅助函数已经在节点内实现了
  node->MoveAllTo(neighbor_node,index,buffer_pool_manager_);
  transaction->AddIntoDeletedPageSet(node->GetPageId());
  parent->Remove(index);
  if (parent->GetSize() <= parent->GetMinSize()) {
    //将该节点内容全部移出后，其父节点会删除指向它的键，但这可能造成父节点过小
    return CoalesceOrRedistribute(parent,transaction);
  }
  return false;
}

/*
 * Redistribute key & value pairs from one page to its sibling page. If index ==
 * 0, move sibling page's first key & value pair into end of input "node",
 * otherwise move sibling page's last key & value pair into head of input
 * "node".
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
void BPLUSTREE_TYPE::Redistribute(N *neighbor_node, N *node, int index) {
  if (index == 0) {
    //此时，邻居节点在原节点的右边
    neighbor_node->MoveFirstToEndOf(node,buffer_pool_manager_);
  } else 
  {
    neighbor_node->MoveLastToFrontOf(node, index, buffer_pool_manager_);
  }
}
/*
 * Update root page if necessary
 * NOTE: size of root page can be less than min size and this method is only
 * called within coalesceOrRedistribute() method
 * case 1: when you delete the last element in root page, but root page still
 * has one last child
 * case 2: when you delete the last element in whole b+ tree
 * @return : true means root page should be deleted, false means no deletion
 * happend
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::AdjustRoot(BPlusTreePage *old_root_node) {
  if (old_root_node->IsLeafPage()) 
  {//如果它根本没有孩子结点
    root_page_id_ = INVALID_PAGE_ID;
    UpdateRootPageId();
    return true;
  }
  if (old_root_node->GetSize() == 1) {// 如果它仍有一个孩子结点,需要把它的孩子节点作为根节点并删去它
    B_PLUS_TREE_INTERNAL_PAGE *root = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(old_root_node);
    const page_id_t newRootId = root->RemoveAndReturnOnlyChild();
    root_page_id_ = newRootId;
    UpdateRootPageId();
    Page *page = buffer_pool_manager_->FetchPage(newRootId);
    assert(page != nullptr);
    B_PLUS_TREE_INTERNAL_PAGE *newRoot=reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page->GetData());
    //这个结点此时没有父节点了
    newRoot->SetParentPageId(INVALID_PAGE_ID);
    buffer_pool_manager_->UnpinPage(newRootId, true);
    return true;
  }
  return false;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leaftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPLUSTREE_TYPE::Begin() {
  KeyType temp;
  auto start_leaf = FindLeafPage(temp, true);//找到最左边的叶子节点
  TryUnlockRootPageId(false);
  return INDEXITERATOR_TYPE(start_leaf, 0, buffer_pool_manager_);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPLUSTREE_TYPE::Begin(const KeyType &key) {
  auto start_leaf = FindLeafPage(key);
  TryUnlockRootPageId(false);
  if (start_leaf == nullptr) return INDEXITERATOR_TYPE(start_leaf, 0, buffer_pool_manager_);
  int index = start_leaf->KeyIndex(key,comparator_);
  return INDEXITERATOR_TYPE(start_leaf, index, buffer_pool_manager_);
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Find leaf page containing particular key, if leftMost flag == true, find
 * the left most leaf page
 */
INDEX_TEMPLATE_ARGUMENTS
B_PLUS_TREE_LEAF_PAGE_TYPE *BPLUSTREE_TYPE::FindLeafPage(const KeyType &key,
                                                         bool leftMost,OpType op,
                                                         Transaction *transaction) {
  //具体思路就是从根节点开始，不断找包含这个key的page，直到找到叶page
  bool exclusive = (op != OpType::READ);
  LockRootPageId(exclusive);
  if (IsEmpty()) {
    TryUnlockRootPageId(exclusive);
    return nullptr;
  }
  auto pointer = CrabingProtocalFetchPage(root_page_id_,op,-1,transaction);
  page_id_t next;
  for (page_id_t cur = root_page_id_; !pointer->IsLeafPage(); pointer = CrabingProtocalFetchPage(next,op,cur,transaction),cur = next) {
    B_PLUS_TREE_INTERNAL_PAGE *internalPage = static_cast<B_PLUS_TREE_INTERNAL_PAGE *>(pointer);
    if (leftMost) 
    {
      next = internalPage->ValueAt(0);
    }else  
    {
      next = internalPage->Lookup(key,comparator_);
    }
  }
  return static_cast<B_PLUS_TREE_LEAF_PAGE_TYPE *>(pointer);
}
INDEX_TEMPLATE_ARGUMENTS
BPlusTreePage *BPLUSTREE_TYPE::FetchPage(page_id_t page_id) {
  auto page = buffer_pool_manager_->FetchPage(page_id);
  return reinterpret_cast<BPlusTreePage *>(page->GetData());
}
INDEX_TEMPLATE_ARGUMENTS
BPlusTreePage *BPLUSTREE_TYPE::CrabingProtocalFetchPage(page_id_t page_id,OpType op,page_id_t previous, Transaction *transaction) {
  bool exclusive = op != OpType::READ;
  auto page = buffer_pool_manager_->FetchPage(page_id);
  Lock(exclusive,page);
  auto treePage = reinterpret_cast<BPlusTreePage *>(page->GetData());
  if (previous > 0 && (!exclusive || treePage->IsSafe(op))) {
    FreePagesInTransaction(exclusive,transaction,previous);
  }
  if (transaction != nullptr)
    transaction->AddIntoPageSet(page);
  return treePage;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::FreePagesInTransaction(bool exclusive, Transaction *transaction, page_id_t cur) {
  //需要进行释放
  TryUnlockRootPageId(exclusive);
  if (transaction == nullptr) {
    assert(!exclusive && cur >= 0);
    Unlock(false,cur);
    buffer_pool_manager_->UnpinPage(cur,false);
    return;
  }
  for (Page *page : *transaction->GetPageSet()) {
    int curPid = page->GetPageId();
    Unlock(exclusive,page);
    buffer_pool_manager_->UnpinPage(curPid,exclusive);
    if (transaction->GetDeletedPageSet()->find(curPid) != transaction->GetDeletedPageSet()->end()) {
      buffer_pool_manager_->DeletePage(curPid);
      transaction->GetDeletedPageSet()->erase(curPid);
    }
  }
  transaction->GetPageSet()->clear();
}

/*
 * Update/Insert root page id in header page(where page_id = 0, header_page is
 * defined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      defualt value is false. When set to true,
 * insert a record <index_name, root_page_id> into header page instead of
 * updating it.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  HeaderPage *head_page = static_cast<HeaderPage *>(buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record)
    head_page->InsertRecord(index_name_, root_page_id_);
  else
    head_page->UpdateRecord(index_name_, root_page_id_);
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

/*
 * This method is used for debug only
 * print out whole b+tree sturcture, rank by rank
 */
INDEX_TEMPLATE_ARGUMENTS
std::string BPLUSTREE_TYPE::ToString(bool verbose) {
}

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name,
                                    Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;

    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, transaction);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name,
                                    Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, transaction);
  }
}


template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;
} // namespace scudb
