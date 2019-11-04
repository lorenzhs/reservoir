/*******************************************************************************
 * reservoir/btree_set.hpp
 *
 * Originally part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2008-2017 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2018-2019 Tobias Theuer <tobias.theuer@gmx.de>
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_BTREE_SET_HEADER
#define RESERVOIR_BTREE_SET_HEADER

#include <reservoir/btree.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace reservoir {

/*!
 * Specialized B+ tree template class implementing STL's set container.
 *
 * Implements the STL set using a B+ tree. It can be used as a drop-in
 * replacement for std::set. Not all asymptotic time requirements are met in
 * theory. The class has a traits class defining B+ tree properties like slots
 * and self-verification. Furthermore an allocator can be specified for tree
 * nodes.
 *
 * It is somewhat inefficient to implement a set using a B+ tree, a plain B
 * tree would hold fewer copies of the keys.
 */
template <typename Key_, typename Compare_ = std::less<Key_>,
          typename Traits_ = btree_default_traits<Key_, Key_>,
          typename Alloc_ = std::allocator<Key_>>
class btree_set {
public:
    //! \name Template Parameter Types
    //! \{

    //! First template parameter: The key type of the B+ tree. This is stored
    //! in inner nodes and leaves.
    using key_type = Key_;

    //! Second template parameter: Key comparison function object
    using key_compare = Compare_;

    //! Third template parameter: Traits object used to define more parameters
    //! of the B+ tree
    using traits = Traits_;

    //! Fourth template parameter: STL allocator
    using allocator_type = Alloc_;

    //! \}

    //! The macro TLX_BTREE_FRIENDS can be used by outside class to access the
    //! B+ tree internals. This was added for wxBTreeDemo to be able to draw the
    //! tree.
    TLX_BTREE_FRIENDS;

public:
    //! \name Constructed Types
    //! \{

    //! Construct the set value_type: the key_type.
    using value_type = key_type;

    //! Key Extractor Struct
    struct key_of_value {
        //! pull first out of pair
        static const key_type& get(const value_type& v) noexcept {
            return v;
        }
    };

    //! Implementation type of the btree_base
    using btree_impl =
        BTree<key_type, value_type, key_of_value, key_compare, traits, false, allocator_type>;

    //! Function class comparing two value_type keys.
    using value_compare = typename btree_impl::value_compare;

    //! Size type used to count keys
    using size_type = typename btree_impl::size_type;

    //! \}

public:
    //! \name Static Constant Options and Values of the B+ Tree
    //! \{

    //! Base B+ tree parameter: The number of key slots in each leaf
    static const uint16_t leaf_slotmax = btree_impl::leaf_slotmax;

    //! Base B+ tree parameter: The number of key slots in each inner node,
    //! this can differ from slots in each leaf.
    static const uint16_t inner_slotmax = btree_impl::inner_slotmax;

    //! Computed B+ tree parameter: The minimum number of key slots used in a
    //! leaf. If fewer slots are used, the leaf will be merged or slots shifted
    //! from it's siblings.
    static const uint16_t leaf_slotmin = btree_impl::leaf_slotmin;

    //! Computed B+ tree parameter: The minimum number of key slots used
    //! in an inner node. If fewer slots are used, the inner node will be
    //! merged or slots shifted from it's siblings.
    static const uint16_t inner_slotmin = btree_impl::inner_slotmin;

    //! Debug parameter: Enables expensive and thorough checking of the B+ tree
    //! invariants after each insert/erase operation.
    static const bool self_verify = btree_impl::self_verify;

    //! Debug parameter: Prints out lots of debug information about how the
    //! algorithms change the tree. Requires the header file to be compiled with
    //! TLX_BTREE_DEBUG and the key type must be std::ostream printable.
    static const bool debug = btree_impl::debug;

    //! Operational parameter: Allow duplicate keys in the btree.
    static const bool allow_duplicates = btree_impl::allow_duplicates;

    //! \}

public:
    //! \name Iterators and Reverse Iterators
    //! \{

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    using iterator = typename btree_impl::iterator;

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    using const_iterator = typename btree_impl::const_iterator;

    //! create mutable reverse iterator by using STL magic
    using reverse_iterator = typename btree_impl::reverse_iterator;

    //! create constant reverse iterator by using STL magic
    using const_reverse_iterator = typename btree_impl::const_reverse_iterator;

    //! \}

private:
    //! \name Tree Implementation Object
    //! \{

    //! The contained implementation object
    btree_impl tree_;

    //! \}

    //! private constructor to wrap the btree_impl in a set
    btree_set(btree_impl&& other) noexcept : tree_(std::move(other)) {}

public:
    //! \name Constructors and Destructor
    //! \{

    //! Default constructor initializing an empty B+ tree with the standard key
    //! comparison function
    explicit btree_set(const allocator_type& alloc = allocator_type()) noexcept
        : tree_(alloc) {}

    //! Constructor initializing an empty B+ tree with a special key comparison
    //! object
    explicit btree_set(const key_compare& kcf,
                       const allocator_type& alloc = allocator_type()) noexcept
        : tree_(kcf, alloc) {}

    //! Constructor initializing a B+ tree with the range [first,last)
    template <class InputIterator>
    btree_set(InputIterator first, InputIterator last,
              const allocator_type& alloc = allocator_type()) noexcept
        : tree_(alloc) {
        insert(first, last);
    }

    //! Constructor initializing a B+ tree with the range [first,last) and a
    //! special key comparison object
    template <class InputIterator>
    btree_set(InputIterator first, InputIterator last, const key_compare& kcf,
              const allocator_type& alloc = allocator_type()) noexcept
        : tree_(kcf, alloc) {
        insert(first, last);
    }

    //! Frees up all used B+ tree memory pages
    ~btree_set() = default;

    //! Fast swapping of two identical B+ tree objects.
    void swap(btree_set& from) noexcept {
        tree_.swap(from.tree_);
    }

    friend void swap(btree_set& a, btree_set& b) noexcept {
        a.swap(b);
    }

    //! \}

public:
    //! \name Key and Value Comparison Function Objects
    //! \{

    //! Constant access to the key comparison object sorting the B+ tree
    key_compare key_comp() const noexcept {
        return tree_.key_comp();
    }

    //! Constant access to a constructed value_type comparison object. required
    //! by the STL
    value_compare value_comp() const noexcept {
        return tree_.value_comp();
    }

    //! \}

public:
    //! \name Allocators
    //! \{

    //! Return the base node allocator provided during construction.
    allocator_type get_allocator() const noexcept {
        return tree_.get_allocator();
    }

    //! \}

public:
    //! \name Fast Destruction of the B+ Tree
    //! \{

    //! Frees all keys and all nodes of the tree
    void clear() noexcept {
        tree_.clear();
    }

    //! \}

public:
    //! \name STL Iterator Construction Functions
    //! \{

    //! Constructs a read/data-write iterator that points to the first slot in
    //! the first leaf of the B+ tree.
    iterator begin() noexcept {
        return tree_.begin();
    }

    //! Constructs a read/data-write iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree.
    iterator end() noexcept {
        return tree_.end();
    }

    //! Constructs a read-only constant iterator that points to the first slot
    //! in the first leaf of the B+ tree.
    const_iterator begin() const noexcept {
        return tree_.begin();
    }

    //! Constructs a read-only constant iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree.
    const_iterator end() const noexcept {
        return tree_.end();
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    reverse_iterator rbegin() noexcept {
        return tree_.rbegin();
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! slot in the first leaf of the B+ tree. Uses STL magic.
    reverse_iterator rend() noexcept {
        return tree_.rend();
    }

    //! Constructs a read-only reverse iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rbegin() const noexcept {
        return tree_.rbegin();
    }

    //! Constructs a read-only reverse iterator that points to the first slot in
    //! the first leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rend() const noexcept {
        return tree_.rend();
    }

    //! \}

public:
    //! \name Access Functions to the Item Count
    //! \{

    //! Return the number of keys in the B+ tree
    size_type size() const noexcept {
        return tree_.size();
    }

    //! Returns true if there is at least one key in the B+ tree
    bool empty() const noexcept {
        return tree_.empty();
    }

    //! Returns the largest possible size of the B+ Tree. This is just a
    //! function required by the STL standard, the B+ Tree can hold more items.
    size_type max_size() const noexcept {
        return tree_.max_size();
    }

    //! \}

public:
    //! \name STL Access Functions Querying the Tree by Descending to a Leaf
    //! \{

    //! Non-STL function checking whether a key is in the B+ tree. The same as
    //! (find(k) != end()) or (count() != 0).
    bool exists(const key_type& key) const noexcept {
        return tree_.exists(key);
    }

    //! Tries to locate a key in the B+ tree and returns an iterator to the key
    //! slot if found. If unsuccessful it returns end().
    iterator find(const key_type& key) noexcept {
        return tree_.find(key);
    }

    //! Tries to locate a key in the B+ tree and returns an constant iterator to
    //! the key slot if found. If unsuccessful it returns end().
    const_iterator find(const key_type& key) const noexcept {
        return tree_.find(key);
    }

    //! Tries to locate a key in the B+ tree and returns the number of identical
    //! key entries found. As this is a unique set, count() returns either 0 or
    //! 1.
    size_type count(const key_type& key) const noexcept {
        return tree_.count(key);
    }

    //! Searches the B+ tree and returns an iterator to the first pair equal to
    //! or greater than key, or end() if all keys are smaller.
    iterator lower_bound(const key_type& key) noexcept {
        return tree_.lower_bound(key);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! equal to or greater than key, or end() if all keys are smaller.
    const_iterator lower_bound(const key_type& key) const noexcept {
        return tree_.lower_bound(key);
    }

    //! Searches the B+ tree and returns an iterator to the first pair greater
    //! than key, or end() if all keys are smaller or equal.
    iterator upper_bound(const key_type& key) noexcept {
        return tree_.upper_bound(key);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! greater than key, or end() if all keys are smaller or equal.
    const_iterator upper_bound(const key_type& key) const noexcept {
        return tree_.upper_bound(key);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<iterator, iterator> equal_range(const key_type& key) noexcept {
        return tree_.equal_range(key);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const
        noexcept {
        return tree_.equal_range(key);
    }

    //! \}

public:
    //! \name B+ Tree Object Comparison Functions
    //! \{

    //! Equality relation of B+ trees of the same type. B+ trees of the same
    //! size and equal elements are considered equal.
    bool operator==(const btree_set& other) const noexcept {
        return (tree_ == other.tree_);
    }

    //! Inequality relation. Based on operator==.
    bool operator!=(const btree_set& other) const noexcept {
        return (tree_ != other.tree_);
    }

    //! Total ordering relation of B+ trees of the same type. It uses
    //! std::lexicographical_compare() for the actual comparison of elements.
    bool operator<(const btree_set& other) const noexcept {
        return (tree_ < other.tree_);
    }

    //! Greater relation. Based on operator<.
    bool operator>(const btree_set& other) const noexcept {
        return (tree_ > other.tree_);
    }

    //! Less-equal relation. Based on operator<.
    bool operator<=(const btree_set& other) const noexcept {
        return (tree_ <= other.tree_);
    }

    //! Greater-equal relation. Based on operator<.
    bool operator>=(const btree_set& other) const noexcept {
        return (tree_ >= other.tree_);
    }

    //! \}

public:
    //! \name Fast Copy / Move: Assign Operator and Copy / Move Constructors

    //! Assignment operator. All the keys are copied
    btree_set& operator=(const btree_set& other) noexcept {
        // no need to check for pointer equality as btree operator= already does
        tree_ = other.tree_;
        return *this;
    }

    //! Move assignment operator. No keys are copied or moved.
    btree_set& operator=(btree_set&& other) noexcept {
        // no need to check for pointer equality as btree operator= already does
        tree_ = std::move(other.tree_);
        return *this;
    }

    //! Copy constructor. The newly initialized B+ tree object will contain a
    //! copy of all keys.
    btree_set(const btree_set& other) noexcept : tree_(other.tree_) {}

    //! Move constructor. The newly initialized B+ tree object will contain all
    //! keys.
    btree_set(btree_set&& other) noexcept : tree_(std::move(other.tree_)) {}

    //! \}

public:
    //! \name Public Insertion Functions
    //! \{

    //! Attempt to insert a key into the B+ tree. The insert will fail if it is
    //! already present.
    std::pair<iterator, bool> insert(const key_type& x) noexcept {
        return tree_.insert(x);
    }

    //! Attempt to insert a key into the B+ tree. The iterator hint is currently
    //! ignored by the B+ tree insertion routine.
    iterator insert(iterator hint, const key_type& x) noexcept {
        return tree_.insert(hint, x);
    }

    //! Attempt to insert the range [first,last) of iterators dereferencing to
    //! key_type into the B+ tree. Each key/data pair is inserted individually.
    template <typename InputIterator>
    void insert(InputIterator first, InputIterator last) noexcept {
        InputIterator iter = first;
        while (iter != last) {
            insert(*iter);
            ++iter;
        }
    }

    //! Bulk load a sorted range [first,last). Loads items into leaves and
    //! constructs a B-tree above them. The tree must be empty when calling this
    //! function.
    template <typename Iterator>
    void bulk_load(Iterator first, Iterator last) noexcept {
        return tree_.bulk_load(first, last);
    }

    //! \}

public:
    //! \name Public Erase Functions
    //! \{

    //! Erases the key from the set. As this is a unique set, there is no
    //! difference to erase().
    bool erase_one(const key_type& key) noexcept {
        return tree_.erase_one(key);
    }

    //! Erases all the key/data pairs associated with the given key.
    size_type erase(const key_type& key) noexcept {
        return tree_.erase(key);
    }

    //! Erase the key/data pair referenced by the iterator.
    void erase(iterator iter) noexcept {
        return tree_.erase(iter);
    }

#ifdef TLX_BTREE_TODO
    //! Erase all keys in the range [first,last). This function is currently
    //! not implemented by the B+ Tree.
    void erase(iterator /* first */, iterator /* last */) {
        abort();
    }
#endif

    //! \}

public:
    //! Join with another set where no element is less than a key in this set
    void join(btree_set& other_tree) noexcept {
        tree_.join(other_tree.tree_);
    }

    //! Split the set such that left has size k and no element in right is less
    //! than an element of left
    void splitAt(btree_set& left, size_type k, btree_set& right) noexcept {
        tree_.splitAt(left.tree_, k, right.tree_);
    }
    //! Split the set such that left has size k. iter must be begin() + k.
    void splitAt(btree_set& left, size_type k, iterator iter,
                 btree_set& right) noexcept {
        tree_.splitAt(left.tree_, k, iter, right.tree_);
    }

    //! Split the set such that every element in right is strictly greater than
    //! key
    void split(btree_set& left, key_type key, btree_set& right) noexcept {
        tree_.split(left.tree_, key, right.tree_);
    }

    //! \name Rank Functions
    //! \{

    //! Return begin() + rank in O(log size()), or end() if rank is greater than
    //! size()
    iterator find_rank(size_type rank) noexcept {
        return tree_.find_rank(rank);
    }
    //! Return begin() + rank in O(log size()), or end() if rank is greater than
    //! size()
    const_iterator find_rank(size_type rank) const noexcept {
        return tree_.find_rank(rank);
    }

    //! Return the rank of the leftmost element with the given key (or size() if
    //! there is no such element) in O(log size())
    std::pair<size_type, const_iterator> rank_of(const key_type& key) const
        noexcept {
        return tree_.rank_of(key);
    }
    std::pair<size_type, iterator> rank_of(const key_type& key) noexcept {
        return tree_.rank_of(key);
    }
    //! Return the rank of the elemnt pointed at by iter in O(log size() +
    //! std::distance(first, iter)) where first is the leftmost element with the
    //! same key
    size_type rank_of(const_iterator iter) const noexcept {
        return tree_.rank_of(iter);
    }

    //! return the smallest rank of an element with a key not less than the
    //! given key
    std::pair<size_type, const_iterator> rank_of_lower_bound(const key_type& key) const
        noexcept {
        return tree_.rank_of_lower_bound(key);
    }
    std::pair<size_type, iterator> rank_of_lower_bound(const key_type& key) noexcept {
        return tree_.rank_of_lower_bound(key);
    }

    //! return the smallest rank of an element with key greater than the given
    //! key
    std::pair<size_type, const_iterator> rank_of_upper_bound(const key_type& key) const
        noexcept {
        return tree_.rank_of_upper_bound(key);
    }
    std::pair<size_type, iterator> rank_of_upper_bound(const key_type& key) noexcept {
        return tree_.rank_of_upper_bound(key);
    }

    //! \}

    //! Delete the k smallest elements
    btree_set bulk_delete(size_type k) noexcept {
        return tree_.bulk_delete(k);
    }

    //! Delete the k smallest elements using split(). iter must be begin() + k.
    btree_set bulk_delete(size_type k, iterator iter) {
        return tree_.bulk_delete(k, iter);
    }

#ifdef TLX_BTREE_DEBUG

public:
    //! \name Debug Printing
    //! \{

    //! Print out the B+ tree structure with keys onto the given ostream. This
    //! function requires that the header is compiled with TLX_BTREE_DEBUG and
    //! that key_type is printable via std::ostream.
    void print(std::ostream& os) const noexcept {
        tree_.print(os);
    }

    //! Print out only the leaves via the double linked list.
    void print_leaves(std::ostream& os) const noexcept {
        tree_.print_leaves(os);
    }

    //! \}
#endif

public:
    //! \name Verification of B+ Tree Invariants
    //! \{

    //! Run a thorough verification of all B+ tree invariants. The program
    //! aborts via TLX_BTREE_ASSERT() if something is wrong.
    void verify() const noexcept {
        tree_.verify();
    }

    //! \}
};

} // namespace reservoir

#endif // !RESERVOIR_BTREE_SET_HEADER

/******************************************************************************/
