/**
 * b_plus_tree_leaf_page.cpp
 */

#include <sstream>
#include <include/page/b_plus_tree_internal_page.h>
#include "common/exception.h"
#include "common/rid.h"
#include "page/b_plus_tree_leaf_page.h"

namespace scudb {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetPageId(page_id);
  SetSize(0);
  SetParentPageId(parent_id);
  SetNextPageId(INVALID_PAGE_ID);
  SetMaxSize((PAGE_SIZE - sizeof(BPlusTreeLeafPage))/sizeof(MappingType) - 1); //与中间节点一样，但是这里减一是为了预留分裂空间，之前是为了预留常占位
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
page_id_t B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const {
  return next_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) {
  next_page_id_ = next_page_id;
}

/**
 * Helper method to find the first index i so that array[i].first >= key
 * NOTE: This method is only used when generating index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(
    const KeyType &key, const KeyComparator &comparator) const {
  int st = 0, ed = GetSize() - 1;
  while (st <= ed) { //与内节点的查找方式完全一样，但这里是找到第一个大于等于key的index，所以在原来的基础上加一
    int mid = (ed - st) / 2 + st;
    if (comparator(array[mid].first,key) >= 0) ed = mid - 1;
    else st = mid + 1;
  }
  return ed + 1;
}

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
KeyType B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const {
  // replace with your own code
  KeyType key;
  return array[index].first;
}

/*
 * Helper method to find and return the key & value pair associated with input
 * "index"(a.k.a array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
const MappingType &B_PLUS_TREE_LEAF_PAGE_TYPE::GetItem(int index) {
  // replace with your own code
  return array[index];
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert key & value pair into leaf page ordered by key
 * @return  page size after insertion
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key,
                                       const ValueType &value,
                                       const KeyComparator &comparator) {
  int index = KeyIndex(key,comparator); //第一个大于等于key的index
  IncreaseSize(1);
  int curSize = GetSize();
  //往后移动
  for (int i = curSize - 1; i > index; i--) {
    array[i].first = array[i - 1].first;
    array[i].second = array[i - 1].second;
  }
  array[index].first = key;
  array[index].second = value;
  return curSize;
}

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(
    BPlusTreeLeafPage *recipient,
    __attribute__((unused)) BufferPoolManager *buffer_pool_manager) {
  int total = GetMaxSize() + 1;

  int copyIdx = (total)/2;
  for (int i = copyIdx; i < total; i++) {
    recipient->array[i - copyIdx].first = array[i].first;
    recipient->array[i - copyIdx].second = array[i].second;
  }
  //叶节点就没有孩子节点了，无需更新孩子节点的信息，但是要重设指针，注意，它分裂出来的page应该位于它的右边
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(recipient->GetPageId());

  SetSize(copyIdx);
  recipient->SetSize(total - copyIdx);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyHalfFrom(MappingType *items, int size) {}

/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * For the given key, check to see whether it exists in the leaf page. If it
 * does, then store its corresponding value in input "value" and return true.
 * If the key does not exist, then return false
 */
INDEX_TEMPLATE_ARGUMENTS
bool B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType &value,
                                        const KeyComparator &comparator) const {
  int index = KeyIndex(key,comparator);//第一个大于等于key的index，如果它的key不等于输入key那么输入key不存在
  if (index < GetSize() && comparator(array[index].first, key) == 0) {
    value = array[index].second;
    return true;
  }
  return false;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * First look through leaf page to see whether delete key exist or not. If
 * exist, perform deletion, otherwise return immdiately.
 * NOTE: store key&value pair continuously after deletion
 * @return   page size after deletion
 */
//*****************************************************************************************************************
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(
    const KeyType &key, const KeyComparator &comparator) {
  //这里本来能使用Lookup，但是上面实现的Lookup虽然能判断是否存在，但是只能得到value，而我们需要的是key，而叶节点没有实现valueat()函数
  int index = KeyIndex(key,comparator);
  if (index >= GetSize() || comparator(key,KeyAt(index)) != 0) {
    return GetSize();
  }
  //存在则删除
  memmove(array + index, array + index + 1,static_cast<size_t>((GetSize() - index - 1)*sizeof(MappingType)));
  IncreaseSize(-1);
  return GetSize();
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page, then
 * update next page id
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient,
                                           int, BufferPoolManager *) {
  //这里就不像内节点那样需要考虑常占指针了
  int startIdx = recipient->GetSize();
  for (int i = 0; i < GetSize(); i++) {
    recipient->array[startIdx + i].first = array[i].first;
    recipient->array[startIdx + i].second = array[i].second;
  }
  //更新指针,注意更新逻辑
  //只有大下界的page才能全移动到另一个小下界page的右端，说明recipient是左page
  recipient->SetNextPageId(GetNextPageId());

  recipient->IncreaseSize(GetSize());
  SetSize(0);
  
  }
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyAllFrom(MappingType *items, int size) {

}

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(
    BPlusTreeLeafPage *recipient,
    BufferPoolManager *buffer_pool_manager) {
      //由题意，它只能往左移动
      MappingType pair = GetItem(0);
      IncreaseSize(-1);
      //与之前类似
      memmove(array, array + 1, static_cast<size_t>(GetSize()*sizeof(MappingType)));
      recipient->CopyLastFrom(pair);
      //它没有孩子节点(或者说它的孩子节点是真正的值)，只有父节点，更新它的父节点时完全可把它当作内节点,与内节点的更新方式完全相同
      Page *page = buffer_pool_manager->FetchPage(GetParentPageId());
      B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page->GetData());
      parent->SetKeyAt(parent->ValueIndex(GetPageId()), array[0].first);//该叶子节点的首key即是下界，应该与其父节点的key一致
      buffer_pool_manager->UnpinPage(GetParentPageId(), true);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyLastFrom(const MappingType &item) {
  array[GetSize()] = item;
  IncreaseSize(1);
}
/*
 * Remove the last key & value pair from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(
    BPlusTreeLeafPage *recipient, int parentIndex,
    BufferPoolManager *buffer_pool_manager) {
      //由题意，它只能往右移动，逻辑与前面一致
      //这里的parentIndex是指recipient的parent
  MappingType pair = GetItem(GetSize() - 1);
  IncreaseSize(-1);
  recipient->CopyFirstFrom(pair, parentIndex, buffer_pool_manager);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyFirstFrom(
    const MappingType &item, int parentIndex,
    BufferPoolManager *buffer_pool_manager) {
  memmove(array + 1, array, GetSize()*sizeof(MappingType));
  IncreaseSize(1);
  array[0] = item;

  Page *page = buffer_pool_manager->FetchPage(GetParentPageId());
  B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page->GetData());
  parent->SetKeyAt(parentIndex, array[0].first);
  buffer_pool_manager->UnpinPage(GetParentPageId(), true);
    }

/*****************************************************************************
 * DEBUG
 *****************************************************************************/
INDEX_TEMPLATE_ARGUMENTS
std::string B_PLUS_TREE_LEAF_PAGE_TYPE::ToString(bool verbose) const {
  if (GetSize() == 0) {
    return "";
  }
  std::ostringstream stream;
  if (verbose) {
    stream << "[pageId: " << GetPageId() << " parentId: " << GetParentPageId()
           << "]<" << GetSize() << "> ";
  }
  int entry = 0;
  int end = GetSize();
  bool first = true;

  while (entry < end) {
    if (first) {
      first = false;
    } else {
      stream << " ";
    }
    stream << std::dec << array[entry].first;
    if (verbose) {
      stream << "(" << array[entry].second << ")";
    }
    ++entry;
  }
  return stream.str();
}

template class BPlusTreeLeafPage<GenericKey<4>, RID,
                                       GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID,
                                       GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID,
                                       GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID,
                                       GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID,
                                       GenericComparator<64>>;
} // namespace scudb
