/**
 * b_plus_tree_internal_page.cpp
 */
#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "page/b_plus_tree_internal_page.h"

namespace scudb {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, set page id, set parent id and set
 * max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(page_id_t page_id,page_id_t parent_id) 
{
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetSize(1);
  //根据头文件给出的格式与提示，第一个key+pointer只有指针而没有key,无法利用，需要减去，除此之外，还要减去Header占的空间
  SetMaxSize((PAGE_SIZE- sizeof(BPlusTreeInternalPage))/sizeof(MappingType) - 1);
}
/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
KeyType B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const {
  // replace with your own code
  assert(index >= 0 && index < GetSize());
  return array[index].first;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  assert(index >= 0 && index < GetSize());
  array[index].first = key;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value)
{
  assert(0 <= index && index < GetSize());
  array[index].second = value;
}

/*
 * Helper method to find and return array index(or offset), so that its value
 * equals to input "value"
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const {
  for (int i = 0; i < GetSize(); i++) {
    if (value == ValueAt(i)) return i;
  }
  return -1;
}

/*
 * Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
ValueType B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const { 
  assert(index >= 0 && index < GetSize());
  return array[index].second;//second是找值
}

/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * Find and return the child pointer(page_id) which points to the child page
 * that contains input "key"
 * Start the search from the second key(the first key should always be invalid)
 */
INDEX_TEMPLATE_ARGUMENTS
ValueType
B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key,
                                       const KeyComparator &comparator) const {
  assert(GetSize() > 1);
  int st = 1, ed = GetSize() - 1;
  while (st <= ed) { //找到数组中最后一个array[index].first<=key的地方
    int mid = (ed - st) / 2 + st;
    if (comparator(array[mid].first,key) <= 0) st = mid + 1;
    else ed = mid - 1;
  }
  return array[st - 1].second;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Populate new root page with old_value + new_key & new_value
 * When the insertion cause overflow from leaf page all the way upto the root
 * page, you should create a new root page and populate its elements.
 * NOTE: This method is only called within InsertIntoParent()(b_plus_tree.cpp)
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(
    const ValueType &old_value, const KeyType &new_key,
    const ValueType &new_value) {
  array[0].second = old_value;
  array[1].first = new_key;
  array[1].second = new_value;
  SetSize(2);
    }
/*
 * Insert new_key & new_value pair right after the pair with its value ==
 * old_value
 * @return:  new size after insertion
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(
    const ValueType &old_value, const KeyType &new_key,
    const ValueType &new_value) {
  int index = ValueIndex(old_value)+1;//找到它应该插入的Index
  int curSize = GetSize();
  assert(index>0);
  IncreaseSize(1);
  curSize+=1;
  //插入点后面的需要向后移动
  for (int i = curSize - 1; i > index; i--) {
    array[i].first = array[i - 1].first;
    array[i].second = array[i - 1].second;
  }
  array[index].first = new_key;
  array[index].second = new_value;
  return curSize;
}

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(
    BPlusTreeInternalPage *recipient,
    BufferPoolManager *buffer_pool_manager) {
      //由于该节点分裂后，其recipient的的首key会被移动到父节点，故本函数无需执行ParentId的变更，仅用于移动键值对。
    assert(recipient != nullptr);
    int total=GetSize();
    int midIdx = (total)/2;//对半取，但是永远注意第一个位置被常占用
  for (int i = midIdx; i < total; i++) {
    recipient->array[i - midIdx].first = array[i].first;
    recipient->array[i - midIdx].second = array[i].second;
    //由于该节点分裂了，分裂出去的id的孩子节点需要更新
    auto childRawPage = buffer_pool_manager->FetchPage(array[i].second);
    BPlusTreePage *childTreePage = reinterpret_cast<BPlusTreePage *>(childRawPage->GetData());
    childTreePage->SetParentPageId(recipient->GetPageId());
    buffer_pool_manager->UnpinPage(array[i].second,true);
  }
  //设定它们的大小
  SetSize(midIdx);
  recipient->SetSize(total - midIdx);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyHalfFrom(
    MappingType *items, int size, BufferPoolManager *buffer_pool_manager) {}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Remove the key & value pair in internal page according to input index(a.k.a
 * array offset)
 * NOTE: store key&value pair continuously after deletion
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  assert(index >= 0 && index < GetSize());
  for (int i = index + 1; i < GetSize(); i++) {
    //键值对本身仍然存在于内存中
    array[i - 1] = array[i];
  }
  IncreaseSize(-1);
std::cout<<GetSize()<<std::endl;
}

/*
 * Remove the only key & value pair in internal page and return the value
 * NOTE: only call this method within AdjustRoot()(in b_plus_tree.cpp)
 */
INDEX_TEMPLATE_ARGUMENTS
ValueType B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() {
  assert(GetSize() == 1);
  ValueType ret = ValueAt(0);
  IncreaseSize(-1);
  return ret;
}
/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(
    BPlusTreeInternalPage *recipient, int index_in_parent,
    BufferPoolManager *buffer_pool_manager) {
  int start = recipient->GetSize();
  page_id_t recipPageId = recipient->GetPageId();
  // 找到其父页
  Page *page = buffer_pool_manager->FetchPage(GetParentPageId());
  assert(page != nullptr);
  BPlusTreeInternalPage *parent = reinterpret_cast<BPlusTreeInternalPage *>(page->GetData());

  //该Page的父key是该Page及其子Page的key所取值范围的下界，故可令常占位的key为该父key
  //这意味着常占位也要进行移动，因为它代表了这个Page所取Key的下界，否则就会丢失信息.
  SetKeyAt(0, parent->KeyAt(index_in_parent));

  buffer_pool_manager->UnpinPage(parent->GetPageId(), false);
  //以下与之前的实现类似
  for (int i = 0; i < GetSize(); ++i) {
    recipient->array[start + i].first = array[i].first;
    recipient->array[start + i].second = array[i].second;
    
    auto childRawPage = buffer_pool_manager->FetchPage(array[i].second);
    BPlusTreePage *childTreePage = reinterpret_cast<BPlusTreePage *>(childRawPage->GetData());
    childTreePage->SetParentPageId(recipPageId);
    buffer_pool_manager->UnpinPage(array[i].second,true);
  }
  //update relavent key & value pair in its parent page.
  recipient->SetSize(start + GetSize());
  assert(recipient->GetSize() <= GetMaxSize());
  SetSize(0);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyAllFrom(
    MappingType *items, int size, BufferPoolManager *buffer_pool_manager) {
      
    }

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to tail of "recipient"
 * page, then update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(
    BPlusTreeInternalPage *recipient,
    BufferPoolManager *buffer_pool_manager) {
        MappingType pair{KeyAt(0), ValueAt(0)};
        IncreaseSize(-1);
        
        //执行拷贝，如果目标区域和源区域有重叠的话，memmove能够保证源串在被覆盖之前将重叠区域的字节拷贝到目标区域中
        //这是与memcpy的不同点
        memmove(array, array + 1, static_cast<size_t>(GetSize()*sizeof(MappingType)));
        recipient->CopyLastFrom(pair, buffer_pool_manager);
        // 更新孩子父节点Id
        page_id_t childPageId = pair.second;
        Page *page = buffer_pool_manager->FetchPage(childPageId);
        assert (page != nullptr);
        BPlusTreePage *child = reinterpret_cast<BPlusTreePage *>(page->GetData());
        //将孩子的父节点Id设为新的recever的Id
        child->SetParentPageId(recipient->GetPageId());
        assert(child->GetParentPageId() == recipient->GetPageId());
        buffer_pool_manager->UnpinPage(child->GetPageId(), true);
        //更新其父节点
        page = buffer_pool_manager->FetchPage(GetParentPageId());
        B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page->GetData());
        //现在其常占位变成了array[1]的值，代表该page的key的下界，其父节点的key需要更新，但是对应val不需要，因为它还是指向这个page
        parent->SetKeyAt(parent->ValueIndex(GetPageId()), array[0].first);
        buffer_pool_manager->UnpinPage(GetParentPageId(), true);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyLastFrom(
    const MappingType &pair, BufferPoolManager *buffer_pool_manager) {
        assert(GetSize() + 1 <= GetMaxSize());
        array[GetSize()] = pair;
        IncreaseSize(1);
    }

/*
 * Remove the last key & value pair from this page to head of "recipient"
 * page, then update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(
    BPlusTreeInternalPage *recipient, int parent_index,
    BufferPoolManager *buffer_pool_manager) {
  MappingType pair {KeyAt(GetSize() - 1),ValueAt(GetSize() - 1)};
  IncreaseSize(-1);

  recipient->CopyFirstFrom(pair, parent_index, buffer_pool_manager);
    }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyFirstFrom(
    const MappingType &pair, int parent_index,
    BufferPoolManager *buffer_pool_manager) {
  assert(GetSize() + 1 < GetMaxSize());
  memmove(array + 1, array, GetSize()*sizeof(MappingType));
  IncreaseSize(1);
  array[0] = pair;
  // 将孩子的父节点Id设为新的recever的Id
  page_id_t childPageId = pair.second;
  Page *page = buffer_pool_manager->FetchPage(childPageId);
  assert (page != nullptr);
  BPlusTreePage *child = reinterpret_cast<BPlusTreePage *>(page->GetData());
  child->SetParentPageId(GetPageId());
  assert(child->GetParentPageId() == GetPageId());
  buffer_pool_manager->UnpinPage(child->GetPageId(), true);
  //更新其父节点
  page = buffer_pool_manager->FetchPage(GetParentPageId());
  B_PLUS_TREE_INTERNAL_PAGE *parent = reinterpret_cast<B_PLUS_TREE_INTERNAL_PAGE *>(page->GetData());
  parent->SetKeyAt(parent_index, array[0].first);
  buffer_pool_manager->UnpinPage(GetParentPageId(), true);
    }

/*****************************************************************************
 * DEBUG
 *****************************************************************************/
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::QueueUpChildren(
    std::queue<BPlusTreePage *> *queue,
    BufferPoolManager *buffer_pool_manager) {
  for (int i = 0; i < GetSize(); i++) {
    auto *page = buffer_pool_manager->FetchPage(array[i].second);
    if (page == nullptr)
      throw Exception(EXCEPTION_TYPE_INDEX,
                      "all page are pinned while printing");
    BPlusTreePage *node =
        reinterpret_cast<BPlusTreePage *>(page->GetData());
    queue->push(node);
  }
}

INDEX_TEMPLATE_ARGUMENTS
std::string B_PLUS_TREE_INTERNAL_PAGE_TYPE::ToString(bool verbose) const {
  if (GetSize() == 0) {
    return "";
  }
  std::ostringstream os;
  if (verbose) {
    os << "[pageId: " << GetPageId() << " parentId: " << GetParentPageId()
       << "]<" << GetSize() << "> ";
  }

  int entry = verbose ? 0 : 1;
  int end = GetSize();
  bool first = true;
  while (entry < end) {
    if (first) {
      first = false;
    } else {
      os << " ";
    }
    os << std::dec << array[entry].first.ToString();
    if (verbose) {
      os << "(" << array[entry].second << ")";
    }
    ++entry;
  }
  return os.str();
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t,
                                           GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t,
                                           GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t,
                                           GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t,
                                           GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t,
                                           GenericComparator<64>>;
} // namespace scudb
