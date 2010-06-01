/***************************************************************************
   Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef BASICSETREPOSITORY_H
#define BASICSETREPOSITORY_H

#include <set>
#include <vector>
#include "../languageexport.h"
#include <language/duchain/repositories/itemrepository.h>

/**
 * This file provides a set system that can be used to efficiently manage sub-sets of a set of global objects.
 * Each global object must be mapped to a natural number.
 *
 * The efficiency comes from:
 * 1. The sets are represented by binary trees, where every single tree node can be shared across multiple sets.
 *    For that reason, intersecting sets will share large parts of their internal data structures, which leads to
 *    extremely low memory-usage compared to using for example std::set.
 *
 * The more common intersections between the sets exist, the more efficient the system is.
 * This will have the biggest advantage when items that were added contiguously are commonly
 * used within the same sets, and work fastest if the intersections between different sets are contiguously long.
 *
 * That makes it perfect for representing sets that are inherited across tree-like structures, like for example in C++:
 * - Macros defined in files(Macros are inherited from included files)
 * - Strings contained in files(Strings are inherited from included files)
 * - Set of all included files
 *
 * Measurements(see in kdevelop languages/cpp/cppduchain/tests/duchaintest) show that even in worst case(with totally random sets)
 * these set-repositories are 2 times faster than std::set, and 4 times faster than QSet.
 *
 * The main disadvantages are that a global repository needs to be managed, and needs to be secured from simultaneous write-access
 * during multi-threading. This is done internally if the doLocking flag is set while constructing.
 * */

class QString;

namespace Utils {

enum {
delayedDeletionByDefault = 0
};

class SetNode;
class BasicSetRepository;
class SetNodeDataRequest;

///Internal node representation, exported here for performance reason.
struct KDEVPLATFORMLANGUAGE_EXPORT SetNodeData {
  //Rule: start < end
  uint start, end; //This set-node bounds all indices starting at start until end, not including end.

  //Child nodes
  //Rule: left->start == start, right->end == end
  //Rule: (left != 0 && right != 0) || (left == 0 && right == 0)
  uint leftNode, rightNode;
  uint m_refCount;
  
  inline SetNodeData() : start(1), end(1), leftNode(0), rightNode(0), m_refCount(0) {
  }
  
  uint hash() const;
  
  inline short unsigned int itemSize() const {
    return sizeof(SetNodeData);
  }
  
  inline bool contiguous() const {
    return !leftNode;
  }
  
  inline bool hasSlaves() const {
    return (bool)leftNode;
  }
};

typedef KDevelop::ItemRepository<SetNodeData, SetNodeDataRequest, false, false, sizeof(SetNodeData)> SetDataRepositoryBase;

struct SetDataRepository;

class SetNodeDataRequest {
public:

  enum {
    AverageSize = sizeof(SetNodeData)
  };
  
  //This constructor creates a request that finds or creates a node that equals the given node
  //The m_hash must be up to date, and the node must be split correctly around its splitPosition
  inline SetNodeDataRequest(const SetNodeData* _data, SetDataRepository& _repository, BasicSetRepository* _setRepository);
  
  ~SetNodeDataRequest();

  typedef unsigned int HashType;
  
  //Should return the m_hash-value associated with this request(For example the m_hash of a string)
  inline HashType hash() const {
    return m_hash;
  }
  
  //Should return the size of an item created with createItem
  inline size_t itemSize() const {
      return sizeof(SetNodeData);
  }
  //Should create an item where the information of the requested item is permanently stored. The pointer
  //@param item equals an allocated range with the size of itemSize().
  void createItem(SetNodeData* item) const;
  
  static void destroy(SetNodeData* data, KDevelop::AbstractItemRepository& _repository);

  static bool persistent(const SetNodeData* item) {
      return (bool)item->m_refCount;
  }

  //Should return whether the here requested item equals the given item
  inline bool equals(const SetNodeData* item) const;
  
  SetNodeData data;
  
  uint m_hash;
  mutable SetDataRepository& repository;
  mutable BasicSetRepository* setRepository; //May be zero when no notifications are wanted
  mutable bool m_created;
};

struct KDEVPLATFORMLANGUAGE_EXPORT SetDataRepository : public SetDataRepositoryBase {
  SetDataRepository(BasicSetRepository* _setRepository, QString name, KDevelop::ItemRepositoryRegistry* registry) : SetDataRepositoryBase(name, registry), setRepository(_setRepository) {
  }
  
  BasicSetRepository* setRepository;
};

/**
 * This object is copyable. It represents a set, and allows iterating through the represented indices.
 * */
class KDEVPLATFORMLANGUAGE_EXPORT Set {
public:
  class Iterator;
  typedef unsigned int Index;
  
  Set();
  //Internal constructor
  Set(uint treeNode, BasicSetRepository* repository);
  ~Set();
  Set(const Set& rhs);
  Set& operator=(const Set& rhs);

  QString dumpDotGraph() const;

  //Returns an itrator that can be used to iterate over the contained indices
  Iterator iterator() const;

  //Returns this set converted to a standard set that contains all indices contained by this set.
  std::set<unsigned int> stdSet() const;

  ///Returns the count of items in the set
  unsigned int count() const;

  bool contains(Index index) const;
  
  ///@warning: The following operations can change the global repository, and thus need to be serialized
  ///          using mutexes in case of multi-threading.

  ///Set union
  Set operator +(const Set& rhs) const;
  Set& operator +=(const Set& rhs);
  
  ///Set intersection
  Set operator &(const Set& rhs) const;
  Set& operator &=(const Set& rhs);

  ///Set subtraction
  Set operator -(const Set& rhs) const;
  Set& operator -=(const Set& rhs);
  
  uint setIndex() const {
    return m_tree;
  }
  
    ///Increase the static reference-count of this set by one. The initial reference-count of newly created sets is zero.
    void staticRef();
    
    ///Decrease the static reference-count of this set by one. This set must have a reference-count >= 1.
    ///If this set reaches the reference-count zero, it will be deleted, and all sub-nodes that also reach the reference-count zero
    ///will be deleted as well. @warning Either protect ALL your sets by using reference-counting, or don't use it at all.
    void staticUnref();
  
  ///Returns a pointer to the repository this set belongs to. Returns zero when this set is not initialized yet.
  BasicSetRepository* repository() const;
private:
  void unrefNode(uint);
  friend class BasicSetRepository;
  
  uint m_tree;
  mutable BasicSetRepository* m_repository;
};

/**
 * This is a repository that can be used to efficiently manage generic sets
 * that are represented by interweaved binary trees.
 *
 * All strings are based on items that are contained in one master-repository,
 * starting at one.
 *
 * An index of zero is interpreted as invalid.
 * */

class KDEVPLATFORMLANGUAGE_EXPORT BasicSetRepository {
public:
  ///@param name The name must be unique, and is used for loading and storing the data
  ///@param registry Where the repository should be registered. If you give zero, it won't be registered, and thus won't be saved to disk.
  BasicSetRepository(QString name, KDevelop::ItemRepositoryRegistry* registry  = &KDevelop::globalItemRepositoryRegistry(), bool delayedDeletion = delayedDeletionByDefault);
  virtual ~BasicSetRepository();
  typedef unsigned int Index;

  /**
   * Takes a sorted list indices, returns a set representing them
   * */
  Set createSetFromIndices(const std::vector<Index>& indices);

  /**
   * Takes a simple set of indices
   * */
  Set createSet(const std::set<Index>& indices);

  /**
   * Creates a set that only contains that single index.
   * For better performance, you should create bigger sets than this.
   * */
  Set createSet(Index i);
  
  void printStatistics() const;
  
  ///Is called when this index is not part of any set any more
  virtual void itemRemovedFromSets(uint index);
  
  ///Is called when this index is added to one of the contained sets for the first time
  virtual void itemAddedToSets(uint index);
  
  inline const SetNodeData* nodeFromIndex(uint index) const {
      if(index)
          return dataRepository.itemFromIndex(index);
      else
          return 0;
  }
  
  inline QMutex* mutex() const {
      return m_mutex;
  }
  
  ///Only public to get statistics and such
  const SetDataRepository& getDataRepository() const {
      return dataRepository;
  }
  
  ///Set whether set-nodes with reference-count zero should be deleted only after a delay
  ///The default is true.
  ///This may be faster when the structure is large anyway and many temporary sets
  ///are created, but leads to a sparse structure in memory, which is bad for cache.
  void setDelayedDeletion(bool delayed) {
    m_delayedDeletion = delayed;
  }
  
  inline bool delayedDeletion() const {
    return m_delayedDeletion;
  }
  
private:
  friend class Set;
  friend class Set::Iterator;
  class Private;
  Private* d;
  SetDataRepository dataRepository;
  QMutex* m_mutex;
  bool m_delayedDeletion;
  
//   SetNode
};

/**
 * Use this to iterate over the indices contained in a set
 * */
class KDEVPLATFORMLANGUAGE_EXPORT Set::Iterator {
public:
  Iterator();
  Iterator(const Iterator& rhs);
  Iterator& operator=(const Iterator& rhs);
  
  ~Iterator();
  operator bool() const;
  Iterator& operator++();
  BasicSetRepository::Index operator*() const;
private:
  friend class Set;
  friend class IteratorPrivate;
  static inline BasicSetRepository::Private *getPrivatePtr(BasicSetRepository *repo) { return repo->d; }
  static inline SetDataRepository &getDataRepository(BasicSetRepository *repo) { return repo->dataRepository; }
  class IteratorPrivate;
  IteratorPrivate* d;
};

}

#endif
