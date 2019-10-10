/*******************************************************************************
 * reservoir/btree.hpp
 *
 * Originally part of tlx - http://panthema.net/tlx
 * modified to include subtree sizes of every node
 *
 * Copyright (C) 2008-2017 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2018-2019 Tobias Theuer <tobias.theuer@gmx.de>
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_BTREE_HEADER
#define RESERVOIR_BTREE_HEADER

#include <tlx/die/core.hpp>

// *** Required Headers from the STL

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <ostream>
#include <utility>

namespace reservoir {

// *** Debugging Macros

#ifdef TLX_BTREE_DEBUG

//! Print out debug information to std::cout if TLX_BTREE_DEBUG is defined.
#define TLX_BTREE_PRINT(x)                                                     \
    do {                                                                       \
        if (debug)                                                             \
            (std::cout << x << std::endl);                                     \
    } while (0)

//! Assertion only if TLX_BTREE_DEBUG is defined. This is not used in verify().
#define TLX_BTREE_ASSERT(x)                                                    \
    do {                                                                       \
        tlx_die_unless(x);                                                     \
    } while (0)

#else
//! Print out debug information to std::cout if TLX_BTREE_DEBUG is defined.
#define TLX_BTREE_PRINT(x)                                                     \
    do {                                                                       \
    } while (0)

//! Assertion only if TLX_BTREE_DEBUG is defined. This is not used in verify().
#define TLX_BTREE_ASSERT(x)                                                    \
    do {                                                                       \
    } while (0)

#endif


//! The maximum of a and b. Used in some compile-time formulas.
#define TLX_BTREE_MAX(a, b) ((a) < (b) ? (b) : (a))

#ifndef TLX_BTREE_FRIENDS
//! The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
//! tree internals. This was added for wxBTreeDemo to be able to draw the
//! tree.
#define TLX_BTREE_FRIENDS friend class btree_friend
#endif

/*!
 * Generates default traits for a B+ tree used as a set or map. It estimates
 * leaf and inner node sizes by assuming a cache line multiple of 256 bytes.
 */
template <typename Key, typename Value>
struct btree_default_traits {
    //! If true, the tree will self verify its invariants after each insert() or
    //! erase(). The header must have been compiled with TLX_BTREE_DEBUG
    //! defined.
    static const bool self_verify = false;

    //! If true, the tree will print out debug information and a tree dump
    //! during insert() or erase() operation. The header must have been
    //! compiled with TLX_BTREE_DEBUG defined and key_type must be std::ostream
    //! printable.
    static const bool debug = false;

    //! Number of slots in each leaf of the tree. Estimated so that each node
    //! has a size of about 256 bytes.
    static const uint16_t leaf_slots = TLX_BTREE_MAX(8, 256 / (sizeof(Value)));

    //! Number of slots in each inner node of the tree. Estimated so that each
    //! node has a size of about 256 bytes.
    static const uint16_t inner_slots =
        TLX_BTREE_MAX(8, 256 / (sizeof(Key) + sizeof(void*)));

    //! As of stx-btree-0.9, the code does linear search in find_lower() and
    //! find_upper() instead of binary_search, unless the node size is larger
    //! than this threshold. See notes at
    //! http://panthema.net/2013/0504-STX-B+Tree-Binary-vs-Linear-Search
    static const size_t binsearch_threshold = 256;
};

/*!
 * Basic class implementing a B+ tree data structure in memory.
 *
 * The base implementation of an in-memory B+ tree. It is based on the
 * implementation in Cormen's Introduction into Algorithms, Jan Jannink's paper
 * and other algorithm resources. Almost all STL-required function calls are
 * implemented. The asymptotic time requirements of the STL are not always
 * fulfilled in theory, however, in practice this B+ tree performs better than a
 * red-black tree and almost always uses less memory. The insertion function
 * splits the nodes on the recursion unroll. Erase is largely based on Jannink's
 * ideas.
 *
 * This class is specialized into btree_set, btree_multiset, btree_map and
 * btree_multimap using default template parameters and facade functions.
 */
template <typename Key, typename Value, typename KeyOfValue,
          typename Compare = std::less<Key>,
          typename Traits = btree_default_traits<Key, Value>,
          bool Duplicates = false, typename Allocator = std::allocator<Value>>
class BTree {
public:
    //! \name Template Parameter Types
    //! \{

    //! First template parameter: The key type of the B+ tree. This is stored in
    //! inner nodes.
    using key_type = Key;

    //! Second template parameter: Composition pair of key and data types, or
    //! just the key for set containers. This data type is stored in the leaves.
    using value_type = Value;

    //! Third template: key extractor class to pull key_type from value_type.
    using key_of_value = KeyOfValue;

    //! Fourth template parameter: key_type comparison function object
    using key_compare = Compare;

    //! Fifth template parameter: Traits object used to define more parameters
    //! of the B+ tree
    using traits = Traits;

    //! Sixth template parameter: Allow duplicate keys in the B+ tree. Used to
    //! implement multiset and multimap.
    static constexpr bool allow_duplicates = Duplicates;

    //! Seventh template parameter: STL allocator for tree nodes
    using allocator_type = Allocator;

    //! \}

    // The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    TLX_BTREE_FRIENDS;

private:
    //! Type for indexing the arrays of slots and children.
    using SlotIndexType = uint16_t;

    //! Type for the number of used slots in a node
    using NumSlotType = uint16_t;

    //! Type for the level of a node
    using LevelType = uint16_t;

public:
    //! \name Constructed Types
    //! \{

    //! Size type used to count keys
    using size_type = size_t;

    //! \}

public:
    //! \name Static Constant Options and Values of the B+ Tree
    //! \{

    //! Base B+ tree parameter: The number of key/data slots in each leaf
    static const NumSlotType leaf_slotmax = traits::leaf_slots;

    //! Base B+ tree parameter: The number of key slots in each inner node,
    //! this can differ from slots in each leaf.
    static const NumSlotType inner_slotmax = traits::inner_slots;

    //! Computed B+ tree parameter: The minimum number of key/data slots used
    //! in a leaf. If fewer slots are used, the leaf will be merged or slots
    //! shifted from it's siblings.
    static const NumSlotType leaf_slotmin = (leaf_slotmax / 2);

    //! Computed B+ tree parameter: The minimum number of key slots used
    //! in an inner node. If fewer slots are used, the inner node will be
    //! merged or slots shifted from it's siblings.
    static const NumSlotType inner_slotmin = (inner_slotmax / 2);

    //! Debug parameter: Enables expensive and thorough checking of the B+ tree
    //! invariants after each insert/erase operation.
    static const bool self_verify = traits::self_verify;

    //! Debug parameter: Prints out lots of debug information about how the
    //! algorithms change the tree. Requires the header file to be compiled
    //! with TLX_BTREE_DEBUG and the key type must be std::ostream printable.
    static const bool debug = traits::debug;

    //! \}

private:
    //! \name Node Classes for In-Memory Nodes
    //! \{

    //! The header structure of each node in-memory. This structure is extended
    //! by InnerNode or LeafNode.
    struct node {
        //! Level in the b-tree, if level == 0 -> leaf node
        LevelType level;

        //! Number of key slotuse use, so the number of valid children or data
        //! pointers
        NumSlotType slotuse;

        //! Delayed initialisation of constructed node.
        void initialize(const LevelType l) noexcept {
            level = l;
            slotuse = 0;
        }

        //! True if this is a leaf node.
        bool is_leafnode() const noexcept {
            return (level == 0);
        }
    };

    //! Extended structure of a inner node in-memory. Contains only keys and no
    //! data items.
    struct InnerNode : public node {
        //! Define an related allocator for the InnerNode structs.
        using alloc_type = typename Allocator::template rebind<InnerNode>::other;

        //! Keys of children or data pointers
        key_type slotkey[inner_slotmax]; // NOLINT

        //! Pointers to children
        node* childid[inner_slotmax + 1]; // NOLINT

        //! Sum of the number of elements in leafs below this node
        size_type subtree_size;

        //! Set variables to initial values.
        void initialize(const LevelType l) noexcept {
            node::initialize(l);
        }

        //! Return key in slot s
        const key_type& key(size_t s) const noexcept {
            return slotkey[s];
        }

        //! True if the node's slots are full.
        bool is_full() const noexcept {
            return (node::slotuse == inner_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const noexcept {
            return (node::slotuse <= inner_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const noexcept {
            return (node::slotuse < inner_slotmin);
        }

        //! From the split-implementation
        void copy_slots_from(const InnerNode& n) noexcept {
            std::copy(n.slotkey, n.slotkey + n.slotuse, slotkey);
            std::copy(n.childid, n.childid + n.slotuse + 1, childid);
            node::slotuse = n.slotuse;
            subtree_size = n.subtree_size;
        }

        void swap(InnerNode& n) noexcept {
            InnerNode tmp_node;

            tmp_node.level = node::level;
            tmp_node.copy_slots_from(*this);

            node::level = n.level;
            copy_slots_from(n);

            n.level = tmp_node.level;
            n.copy_slots_from(tmp_node);
        }
    };

    //! Extended structure of a leaf node in memory. Contains pairs of keys and
    //! data items. Key and data slots are kept together in value_type.
    struct LeafNode : public node {
        //! Define an related allocator for the LeafNode structs.
        using alloc_type = typename Allocator::template rebind<LeafNode>::other;

        //! Double linked list pointers to traverse the leaves
        LeafNode* prev_leaf;

        //! Double linked list pointers to traverse the leaves
        LeafNode* next_leaf;

        //! Array of (key, data) pairs
        value_type slotdata[leaf_slotmax]; // NOLINT

        //! Set variables to initial values
        void initialize() noexcept {
            node::initialize(0);
            prev_leaf = next_leaf = nullptr;
        }

        //! Return key in slot s.
        const key_type& key(size_t s) const noexcept {
            return key_of_value::get(slotdata[s]);
        }

        //! True if the node's slots are full.
        bool is_full() const noexcept {
            return (node::slotuse == leaf_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const noexcept {
            return (node::slotuse <= leaf_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const noexcept {
            return (node::slotuse < leaf_slotmin);
        }

        //! Set the (key,data) pair in slot. Overloaded function used by
        //! bulk_load().
        void set_slot(SlotIndexType slot, const value_type& value) noexcept {
            TLX_BTREE_ASSERT(slot < node::slotuse);
            slotdata[slot] = value;
        }
    };

    //! \}

public:
    //! \name Iterators and Reverse Iterators
    //! \{

    class iterator;
    class const_iterator;
    class reverse_iterator;
    class const_reverse_iterator;

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    class iterator {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        using key_type = typename BTree::key_type;

        //! The value type of the btree. Returned by operator*().
        using value_type = typename BTree::value_type;

        //! Reference to the value_type. STL required.
        using reference = value_type&;

        //! Pointer to the value_type. STL required.
        using pointer = value_type*;

        //! STL-magic iterator category
        using iterator_category = std::bidirectional_iterator_tag;

        //! STL-magic
        using difference_type = ptrdiff_t;

        //! Our own type
        using self = iterator;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename BTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        SlotIndexType curr_slot{0};

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class const_iterator;

        //! Also friendly to the reverse_iterator, so it may access the two
        //! data items directly.
        friend class reverse_iterator;

        //! Also friendly to the const_reverse_iterator, so it may access the
        //! two data items directly.
        friend class const_reverse_iterator;

        //! Also friendly to the base btree class, because erase_iter() needs
        //! to read the curr_leaf and curr_slot values directly.
        friend class BTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a mutable iterator
        iterator() noexcept : curr_leaf(nullptr) {}

        //! Initializing-Constructor of a mutable iterator
        iterator(typename BTree::LeafNode* l, SlotIndexType s) noexcept
            : curr_leaf(l), curr_slot(s) {}

        //! Copy-constructor from a reverse iterator
        iterator(const reverse_iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Dereference the iterator.
        reference operator*() const noexcept {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator->() const noexcept {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const noexcept {
            return curr_leaf->key(curr_slot);
        }

        //! Prefix++ advance the iterator to the next slot.
        iterator& operator++() noexcept {
            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            } else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        iterator operator++(int) noexcept {
            iterator tmp = *this; // copy ourselves

            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            } else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        iterator& operator--() noexcept {
            if (curr_slot > 0) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            } else {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        iterator operator--(int) noexcept {
            iterator tmp = *this; // copy ourselves

            if (curr_slot > 0) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            } else {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
            return (lhs.curr_leaf == rhs.curr_leaf) &&
                   (lhs.curr_slot == rhs.curr_slot);
        }

        //! Inequality of iterators.
        friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept {
            return (lhs.curr_leaf != rhs.curr_leaf) ||
                   (lhs.curr_slot != rhs.curr_slot);
        }
    };

    //! STL-like read-only iterator object for B+ tree items. The iterator
    //! points to a specific slot number in a leaf.
    class const_iterator {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        using key_type = typename BTree::key_type;

        //! The value type of the btree. Returned by operator*().
        using value_type = typename BTree::value_type;

        //! Reference to the value_type. STL required.
        using reference = const value_type&;

        //! Pointer to the value_type. STL required.
        using pointer = const value_type*;

        //! STL-magic iterator category
        using iterator_category = std::bidirectional_iterator_tag;

        //! STL-magic
        using difference_type = ptrdiff_t;

        //! Our own type
        using self = const_iterator;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename BTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        SlotIndexType curr_slot{0};

        //! Friendly to the reverse_const_iterator, so it may access the two
        //! data items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const iterator
        const_iterator() noexcept : curr_leaf(nullptr) {}

        //! Initializing-Constructor of a const iterator
        const_iterator(const typename BTree::LeafNode* l, SlotIndexType s) noexcept
            : curr_leaf(l), curr_slot(s) {}

        //! Copy-constructor from a mutable iterator
        const_iterator(const iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Copy-constructor from a mutable reverse iterator
        const_iterator(const reverse_iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Copy-constructor from a const reverse iterator
        const_iterator(const const_reverse_iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Dereference the iterator.
        reference operator*() const noexcept {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator->() const noexcept {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const noexcept {
            return curr_leaf->key(curr_slot);
        }

        //! Prefix++ advance the iterator to the next slot.
        const_iterator& operator++() noexcept {
            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            } else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        const_iterator operator++(int) noexcept {
            const_iterator tmp = *this; // copy ourselves

            if (curr_slot + 1u < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            } else {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        const_iterator& operator--() noexcept {
            if (curr_slot > 0) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            } else {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        const_iterator operator--(int) noexcept {
            const_iterator tmp = *this; // copy ourselves

            if (curr_slot > 0) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            } else {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        friend bool operator==(const const_iterator& lhs,
                               const const_iterator& rhs) noexcept {
            return (lhs.curr_leaf == rhs.curr_leaf) &&
                   (lhs.curr_slot == rhs.curr_slot);
        }

        //! Inequality of iterators.
        friend bool operator!=(const const_iterator& lhs,
                               const const_iterator& rhs) noexcept {
            return (lhs.curr_leaf != rhs.curr_leaf) ||
                   (lhs.curr_slot != rhs.curr_slot);
        }
    };

    // TODO: Why not use std::reverse_iterator<iterator> and use the extra int space to represent rend?

    //! STL-like mutable reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class reverse_iterator {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        using key_type = typename BTree::key_type;

        //! The value type of the btree. Returned by operator*().
        using value_type = typename BTree::value_type;

        //! Reference to the value_type. STL required.
        using reference = value_type&;

        //! Pointer to the value_type. STL required.
        using pointer = value_type*;

        //! STL-magic iterator category
        using iterator_category = std::bidirectional_iterator_tag;

        //! STL-magic
        using difference_type = ptrdiff_t;

        //! Our own type
        using self = reverse_iterator;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename BTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        SlotIndexType curr_slot{0};

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly
        friend class iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a reverse iterator
        reverse_iterator() noexcept : curr_leaf(nullptr) {}

        //! Initializing-Constructor of a mutable reverse iterator
        reverse_iterator(typename BTree::LeafNode* l, SlotIndexType s) noexcept
            : curr_leaf(l), curr_slot(s) {}

        //! Copy-constructor from a mutable iterator
        reverse_iterator(const iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Dereference the iterator.
        reference operator*() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator->() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the next slot.
        reverse_iterator& operator++() noexcept {
            if (curr_slot > 1) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            } else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        reverse_iterator operator++(int) noexcept {
            reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot > 1) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            } else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        reverse_iterator& operator--() noexcept {
            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            } else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        reverse_iterator operator--(int) noexcept {
            reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            } else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        friend bool operator==(const reverse_iterator& lhs,
                               const reverse_iterator& rhs) noexcept {
            return (lhs.curr_leaf == rhs.curr_leaf) &&
                   (lhs.curr_slot == rhs.curr_slot);
        }

        //! Inequality of iterators.
        friend bool operator!=(const reverse_iterator& lhs,
                               const reverse_iterator& rhs) noexcept {
            return (lhs.curr_leaf != rhs.curr_leaf) ||
                   (lhs.curr_slot != rhs.curr_slot);
        }
    };

    //! STL-like read-only reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class const_reverse_iterator {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        using key_type = typename BTree::key_type;

        //! The value type of the btree. Returned by operator*().
        using value_type = typename BTree::value_type;

        //! Reference to the value_type. STL required.
        using reference = const value_type&;

        //! Pointer to the value_type. STL required.
        using pointer = const value_type*;

        //! STL-magic iterator category
        using iterator_category = std::bidirectional_iterator_tag;

        //! STL-magic
        using difference_type = ptrdiff_t;

        //! Our own type
        using self = const_reverse_iterator;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename BTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        SlotIndexType curr_slot{0};

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const reverse iterator.
        const_reverse_iterator() noexcept : curr_leaf(nullptr) {}

        //! Initializing-Constructor of a const reverse iterator.
        const_reverse_iterator(const typename BTree::LeafNode* l,
                               SlotIndexType s) noexcept
            : curr_leaf(l), curr_slot(s) {}

        //! Copy-constructor from a mutable iterator.
        const_reverse_iterator(const iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Copy-constructor from a const iterator.
        const_reverse_iterator(const const_iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Copy-constructor from a mutable reverse iterator.
        const_reverse_iterator(const reverse_iterator& it) noexcept
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot) {}

        //! Dereference the iterator.
        reference operator*() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator->() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const noexcept {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the previous slot.
        const_reverse_iterator& operator++() noexcept {
            if (curr_slot > 1) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            } else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the previous slot.
        const_reverse_iterator operator++(int) noexcept {
            const_reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot > 1) {
                --curr_slot;
            } else if (curr_leaf->prev_leaf != nullptr) {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            } else {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the next slot.
        const_reverse_iterator& operator--() noexcept {
            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            } else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the next slot.
        const_reverse_iterator operator--(int) noexcept {
            const_reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot < curr_leaf->slotuse) {
                ++curr_slot;
            } else if (curr_leaf->next_leaf != nullptr) {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            } else {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        friend bool operator==(const const_reverse_iterator& lhs,
                               const const_reverse_iterator& rhs) noexcept {
            return (lhs.curr_leaf == rhs.curr_leaf) &&
                   (lhs.curr_slot == rhs.curr_slot);
        }

        //! Inequality of iterators.
        friend bool operator!=(const const_reverse_iterator& lhs,
                               const const_reverse_iterator& rhs) noexcept {
            return (lhs.curr_leaf != rhs.curr_leaf) ||
                   (lhs.curr_slot != rhs.curr_slot);
        }
    };

    //! \}

private:
    //! \name Tree Object Data Members
    //! \{

    //! Pointer to the B+ tree's root node, either leaf or inner node.
    node* root_;

    //! Pointer to first leaf in the double linked leaf chain.
    LeafNode* head_leaf_;

    //! Pointer to last leaf in the double linked leaf chain.
    LeafNode* tail_leaf_;

    //! Key comparison object. More comparison functions are generated from
    //! this < relation.
    key_compare key_less_;

    //! Memory allocator.
    allocator_type allocator_;

    //! \}

public:
    //! \name Constructors and Destructor
    //! \{

    //! Default constructor initializing an empty B+ tree with the standard key
    //! comparison function.
    explicit BTree(const allocator_type& alloc = allocator_type()) noexcept
        : root_(nullptr), head_leaf_(nullptr), tail_leaf_(nullptr),
          allocator_(alloc) {}

    //! Constructor initializing an empty B+ tree with a special key
    //! comparison object.
    explicit BTree(const key_compare& kcf,
                   const allocator_type& alloc = allocator_type()) noexcept
        : root_(nullptr), head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(kcf), allocator_(alloc) {}

    //! Constructor initializing a B+ tree with the range [first,last). The
    //! range need not be sorted. To create a B+ tree from a sorted range, use
    //! bulk_load().
    template <class InputIterator>
    BTree(InputIterator first, InputIterator last,
          const allocator_type& alloc = allocator_type()) noexcept
        : root_(nullptr), head_leaf_(nullptr), tail_leaf_(nullptr),
          allocator_(alloc) {
        insert(first, last);
    }

    //! Constructor initializing a B+ tree with the range [first,last) and a
    //! special key comparison object.  The range need not be sorted. To create
    //! a B+ tree from a sorted range, use bulk_load().
    template <class InputIterator>
    BTree(InputIterator first, InputIterator last, const key_compare& kcf,
          const allocator_type& alloc = allocator_type()) noexcept
        : root_(nullptr), head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(kcf), allocator_(alloc) {
        insert(first, last);
    }

    //! Frees up all used B+ tree memory pages
    ~BTree() noexcept {
        clear();
    }

    //! Fast swapping of two identical B+ tree objects.
    void swap(BTree& from) noexcept {
        std::swap(root_, from.root_);
        std::swap(head_leaf_, from.head_leaf_);
        std::swap(tail_leaf_, from.tail_leaf_);
        std::swap(key_less_, from.key_less_);
        std::swap(allocator_, from.allocator_);
    }
    friend void swap(BTree& a, BTree& b) noexcept {
        a.swap(b);
    }
    //! \}

public:
    //! \name Key and Value Comparison Function Objects
    //! \{

    //! Function class to compare value_type objects. Required by the STL
    class value_compare {
    protected:
        //! Key comparison function from the template parameter
        key_compare key_comp;

        //! Constructor called from BTree::value_comp()
        explicit value_compare(key_compare kc) noexcept : key_comp(kc) {}

        //! Friendly to the btree class so it may call the constructor
        friend class BTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

    public:
        //! Function call "less"-operator resulting in true if x < y.
        bool operator()(const value_type& x, const value_type& y) const noexcept {
            return key_comp(x.first, y.first);
        }
    };

    //! Constant access to the key comparison object sorting the B+ tree.
    key_compare key_comp() const noexcept {
        return key_less_;
    }

    //! Constant access to a constructed value_type comparison object. Required
    //! by the STL.
    value_compare value_comp() const noexcept {
        return value_compare(key_less_);
    }

    //! \}

private:
    //! \name Convenient Key Comparison Functions Generated From key_less
    //! \{

    //! True if a < b ? "constructed" from key_less_()
    bool key_less(const key_type& a, const key_type& b) const noexcept {
        return key_less_(a, b);
    }

    //! True if a <= b ? constructed from key_less()
    bool key_lessequal(const key_type& a, const key_type& b) const noexcept {
        return !key_less_(b, a);
    }

    //! True if a > b ? constructed from key_less()
    bool key_greater(const key_type& a, const key_type& b) const noexcept {
        return key_less_(b, a);
    }

    //! True if a >= b ? constructed from key_less()
    bool key_greaterequal(const key_type& a, const key_type& b) const noexcept {
        return !key_less_(a, b);
    }

    //! True if a == b ? constructed from key_less(). This requires the <
    //! relation to be a total order, otherwise the B+ tree cannot be sorted.
    bool key_equal(const key_type& a, const key_type& b) const noexcept {
        return !key_less_(a, b) && !key_less_(b, a);
    }

    //! \}

public:
    //! \name Allocators
    //! \{

    //! Return the base node allocator provided during construction.
    allocator_type get_allocator() const noexcept {
        return allocator_;
    }

    //! \}

private:
    //! \name Node Object Allocation and Deallocation Functions
    //! \{

    //! Return an allocator for LeafNode objects.
    typename LeafNode::alloc_type leaf_node_allocator() noexcept {
        return typename LeafNode::alloc_type(allocator_);
    }

    //! Return an allocator for InnerNode objects.
    typename InnerNode::alloc_type inner_node_allocator() noexcept {
        return typename InnerNode::alloc_type(allocator_);
    }

    //! Allocate and initialize a leaf node
    LeafNode* allocate_leaf() noexcept {
        LeafNode* n = new (leaf_node_allocator().allocate(1)) LeafNode();
        n->initialize();
        return n;
    }

    //! Allocate and initialize an inner node
    InnerNode* allocate_inner(LevelType level) noexcept {
        InnerNode* n = new (inner_node_allocator().allocate(1)) InnerNode();
        n->initialize(level);
        return n;
    }

    //! Set a new root if insert or merge created an overflow in existing root
    void new_root(node* newchild, const key_type& splitkey) noexcept {
        TLX_BTREE_ASSERT(root_->level == newchild->level);
        if (newchild->is_leafnode()) {
            TLX_BTREE_ASSERT(static_cast<LeafNode*>(newchild)->prev_leaf == root_);
            TLX_BTREE_ASSERT(static_cast<LeafNode*>(root_)->next_leaf == newchild);
        }
        InnerNode* newroot = allocate_inner(LevelType(root_->level + 1));
        newroot->slotkey[0] = splitkey;

        newroot->childid[0] = root_;
        newroot->childid[1] = newchild;

        newroot->slotuse = 1;
        if (newroot->level > 1) {
            newroot->subtree_size =
                static_cast<InnerNode*>(newchild)->subtree_size +
                static_cast<InnerNode*>(root_)->subtree_size;
        } else {
            newroot->subtree_size = newchild->slotuse + root_->slotuse;
        }

        TLX_BTREE_PRINT("BTree::new_root: root changed from " << root_ << " to "
                                                              << newroot);
        root_ = newroot;
    }

    //! Correctly free either inner or leaf node, destructs all contained key
    //! and value objects.
    void free_node(node* n) noexcept {
        if (n->is_leafnode()) {
            LeafNode* ln = static_cast<LeafNode*>(n);
            typename LeafNode::alloc_type a(leaf_node_allocator());
            a.destroy(ln);
            a.deallocate(ln, 1);
        } else {
            InnerNode* in = static_cast<InnerNode*>(n);
            typename InnerNode::alloc_type a(inner_node_allocator());
            a.destroy(in);
            a.deallocate(in, 1);
        }
    }

    //! \}

public:
    //! \name Fast Destruction of the B+ Tree
    //! \{

    //! Frees all key/data pairs and all nodes of the tree.
    void clear() noexcept {
        if (root_) {
            clear_recursive(root_);
            free_node(root_);

            root_ = nullptr;
            head_leaf_ = tail_leaf_ = nullptr;
        }
        TLX_BTREE_ASSERT(size() == 0);
    }

private:
    //! Recursively free up nodes.
    void clear_recursive(node* n) noexcept {
        if (n->is_leafnode()) {
            LeafNode* leafnode = static_cast<LeafNode*>(n);

            for (SlotIndexType slot = 0; slot < leafnode->slotuse; ++slot) {
                // data objects are deleted by LeafNode's destructor
            }
        } else {
            InnerNode* innernode = static_cast<InnerNode*>(n);

            for (NumSlotType slot = 0; slot < innernode->slotuse + 1; ++slot) {
                clear_recursive(innernode->childid[slot]);
                free_node(innernode->childid[slot]);
            }
        }
    }

    //! \}

public:
    //! \name STL Iterator Construction Functions
    //! \{

    //! Constructs a read/data-write iterator that points to the first slot in
    //! the first leaf of the B+ tree.
    iterator begin() noexcept {
        return iterator(head_leaf_, 0);
    }

    //! Constructs a read/data-write iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree.
    iterator end() noexcept {
        return iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read-only constant iterator that points to the first slot
    //! in the first leaf of the B+ tree.
    const_iterator begin() const noexcept {
        return const_iterator(head_leaf_, 0);
    }

    //! Constructs a read-only constant iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree.
    const_iterator end() const noexcept {
        return const_iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    reverse_iterator rbegin() noexcept {
        return reverse_iterator(end());
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! slot in the first leaf of the B+ tree. Uses STL magic.
    reverse_iterator rend() noexcept {
        return reverse_iterator(begin());
    }

    //! Constructs a read-only reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    //! Constructs a read-only reverse iterator that points to the first slot
    //! in the first leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    //! \}

private:
    //! \name B+ Tree Node Binary Search Functions
    //! \{

    //! Searches for the first key in the node n greater or equal to key. Uses
    //! binary search with an optional linear self-verification. This is a
    //! template function, because the slotkey array is located at different
    //! places in LeafNode and InnerNode.
    template <typename node_type>
    SlotIndexType find_lower(const node_type* n, const key_type& key) const
        noexcept {
        if constexpr (sizeof(*n) > traits::binsearch_threshold) {
            if (n->slotuse == 0)
                return 0;

            SlotIndexType lo = 0, hi = n->slotuse;

            while (lo < hi) {
                SlotIndexType mid = (lo + hi) >> 1;

                if (key_lessequal(key, n->key(mid))) {
                    hi = mid; // key <= mid
                } else {
                    lo = mid + 1; // key > mid
                }
            }

            TLX_BTREE_PRINT("BTree::find_lower: on " << n << " key " << key << " -> "
                                                     << lo << " / " << hi);

            // verify result using simple linear search
            if (self_verify) {
                SlotIndexType i = 0;
                while (i < n->slotuse && key_less(n->key(i), key))
                    ++i;

                TLX_BTREE_PRINT("BTree::find_lower: testfind: " << i);
                TLX_BTREE_ASSERT(i == lo);
            }

            return lo;
        } else // for nodes <= binsearch_threshold do linear search.
        {
            SlotIndexType lo = 0;
            while (lo < n->slotuse && key_less(n->key(lo), key))
                ++lo;
            return lo;
        }
    }

    //! Searches for the first key in the node n greater than key. Uses binary
    //! search with an optional linear self-verification. This is a template
    //! function, because the slotkey array is located at different places in
    //! LeafNode and InnerNode.
    template <typename node_type>
    SlotIndexType find_upper(const node_type* n, const key_type& key) const
        noexcept {
        if (sizeof(*n) > traits::binsearch_threshold) {
            if (n->slotuse == 0)
                return 0;

            SlotIndexType lo = 0, hi = n->slotuse;

            while (lo < hi) {
                SlotIndexType mid = (lo + hi) >> 1;

                if (key_less(key, n->key(mid))) {
                    hi = mid; // key < mid
                } else {
                    lo = mid + 1; // key >= mid
                }
            }

            TLX_BTREE_PRINT("BTree::find_upper: on " << n << " key " << key << " -> "
                                                     << lo << " / " << hi);

            // verify result using simple linear search
            if (self_verify) {
                SlotIndexType i = 0;
                while (i < n->slotuse && key_lessequal(n->key(i), key))
                    ++i;

                TLX_BTREE_PRINT("BTree::find_upper testfind: " << i);
                TLX_BTREE_ASSERT(i == hi);
            }

            return lo;
        } else // for nodes <= binsearch_threshold do linear search.
        {
            SlotIndexType lo = 0;
            while (lo < n->slotuse && key_lessequal(n->key(lo), key))
                ++lo;
            return lo;
        }
    }

    //! \}

public:
    //! \name Access Functions to the Item Count
    //! \{

    //! Return the number of key/data pairs in the B+ tree
    size_type size() const noexcept {
        if (!root_) {
            return 0;
        } else if (root_->is_leafnode()) {
            return root_->slotuse;
        } else {
            return static_cast<InnerNode*>(root_)->subtree_size;
        }
    }

    //! Returns true if there is at least one key/data pair in the B+ tree
    bool empty() const noexcept {
        return (size() == size_type(0));
    }

    //! Returns the largest possible size of the B+ Tree. This is just a
    //! function required by the STL standard, the B+ Tree can hold more items.
    size_type max_size() const noexcept {
        return size_type(-1);
    }

    //! \}

public:
    //! \name STL Access Functions Querying the Tree by Descending to a Leaf
    //! \{

    //! Non-STL function checking whether a key is in the B+ tree. The same as
    //! (find(k) != end()) or (count() != 0).
    bool exists(const key_type& key) const noexcept {
        const node* n = root_;
        if (!n)
            return false;

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot)));
    }

    //! Tries to locate a key in the B+ tree and returns an iterator to the
    //! key/data slot if found. If unsuccessful it returns end().
    iterator find(const key_type& key) noexcept {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
                   ? iterator(leaf, slot)
                   : end();
    }

    //! Tries to locate a key in the B+ tree and returns an constant iterator to
    //! the key/data slot if found. If unsuccessful it returns end().
    const_iterator find(const key_type& key) const noexcept {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
                   ? const_iterator(leaf, slot)
                   : end();
    }

    //! Tries to locate a key in the B+ tree and returns the number of identical
    //! key entries found.
    size_type count(const key_type& key) const noexcept {
        const node* n = root_;
        if (!n)
            return 0;

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        size_type num = 0;

        while (leaf && slot < leaf->slotuse && key_equal(key, leaf->key(slot))) {
            ++num;
            if (++slot >= leaf->slotuse) {
                leaf = leaf->next_leaf;
                slot = 0;
            }
        }

        return num;
    }

    //! Searches the B+ tree and returns an iterator to the first pair equal to
    //! or greater than key, or end() if all keys are smaller.
    iterator lower_bound(const key_type& key) noexcept {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! equal to or greater than key, or end() if all keys are smaller.
    const_iterator lower_bound(const key_type& key) const noexcept {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        SlotIndexType slot = find_lower(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns an iterator to the first pair greater
    //! than key, or end() if all keys are smaller or equal.
    iterator upper_bound(const key_type& key) noexcept {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        SlotIndexType slot = find_upper(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! greater than key, or end() if all keys are smaller or equal.
    const_iterator upper_bound(const key_type& key) const noexcept {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        SlotIndexType slot = find_upper(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<iterator, iterator> equal_range(const key_type& key) noexcept {
        return std::pair<iterator, iterator>(lower_bound(key), upper_bound(key));
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const
        noexcept {
        return std::pair<const_iterator, const_iterator>(lower_bound(key),
                                                         upper_bound(key));
    }

    //! \}

public:
    //! \name B+ Tree Object Comparison Functions
    //! \{

    //! Equality relation of B+ trees of the same type. B+ trees of the same
    //! size and equal elements (both key and data) are considered equal. Beware
    //! of the random ordering of duplicate keys.
    bool operator==(const BTree& other) const noexcept {
        return (size() == other.size()) &&
               std::equal(begin(), end(), other.begin());
    }

    //! Inequality relation. Based on operator==.
    bool operator!=(const BTree& other) const noexcept {
        return !(*this == other);
    }

    //! Total ordering relation of B+ trees of the same type. It uses
    //! std::lexicographical_compare() for the actual comparison of elements.
    bool operator<(const BTree& other) const noexcept {
        return std::lexicographical_compare(begin(), end(), other.begin(),
                                            other.end());
    }

    //! Greater relation. Based on operator<.
    bool operator>(const BTree& other) const noexcept {
        return other < *this;
    }

    //! Less-equal relation. Based on operator<.
    bool operator<=(const BTree& other) const noexcept {
        return !(other < *this);
    }

    //! Greater-equal relation. Based on operator<.
    bool operator>=(const BTree& other) const noexcept {
        return !(*this < other);
    }

    //! \}

public:
    //! \name Fast Copy / Move: (Move) Assign Operator and Copy / Move Constructors
    //! \{

    //! Assignment operator. All the key/data pairs are copied.
    BTree& operator=(const BTree& other) noexcept {
        if (this != &other) {
            clear();

            key_less_ = other.key_comp();
            allocator_ = other.get_allocator();
            if (!other.empty()) {
                root_ = copy_recursive(other.root_);
            }

            if (self_verify)
                verify();
        }
        return *this;
    }


    //! Move assignment operator. No key/data pairs are copied; the argument becomes the empty tree.
    BTree& operator=(BTree&& other) noexcept {
        if (this != &other) {
            clear();
            root_ = other.root_;
            head_leaf_ = other.head_leaf_;
            tail_leaf_ = other.tail_leaf_;
            key_less_ = std::move(other.key_less_);
            allocator_ = std::move(other.allocator_);
            other.root_ = other.head_leaf_ = other.tail_leaf_ = nullptr;
            other.key_less_ = key_compare{};
            other.allocator_ = allocator_type{};
        }
        return *this;
    }

    //! Copy constructor. The newly initialized B+ tree object will contain a
    //! copy of all key/data pairs.
    BTree(const BTree& other) noexcept
        : root_(nullptr), head_leaf_(nullptr), tail_leaf_(nullptr),
          key_less_(other.key_comp()), allocator_(other.get_allocator()) {
        if (!other.empty()) {
            root_ = copy_recursive(other.root_);
            if (self_verify)
                verify();
        }
        TLX_BTREE_ASSERT(size() == other.size());
    }

    //! Move constructor. The other BTree
    BTree(BTree&& other) noexcept {
        root_ = other.root_;
        head_leaf_ = other.head_leaf_;
        tail_leaf_ = other.tail_leaf_;
        key_less_ = std::move(other.key_less_);
        allocator_ = std::move(other.allocator_);
        other.root_ = other.head_leaf_ = other.tail_leaf_ = nullptr;
        other.key_less_ = key_compare{};
        other.allocator_ = allocator_type{};
    }

private:
    //! Recursively copy nodes from another B+ tree object
    node* copy_recursive(const node* n) noexcept {
        if (n->is_leafnode()) {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);
            LeafNode* newleaf = allocate_leaf();

            newleaf->slotuse = leaf->slotuse;
            std::copy(leaf->slotdata, leaf->slotdata + leaf->slotuse,
                      newleaf->slotdata);

            if (head_leaf_ == nullptr) {
                head_leaf_ = tail_leaf_ = newleaf;
                newleaf->prev_leaf = newleaf->next_leaf = nullptr;
            } else {
                newleaf->prev_leaf = tail_leaf_;
                tail_leaf_->next_leaf = newleaf;
                tail_leaf_ = newleaf;
            }

            return newleaf;
        } else {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            InnerNode* newinner = allocate_inner(inner->level);

            newinner->subtree_size = inner->subtree_size;
            newinner->slotuse = inner->slotuse;
            std::copy(inner->slotkey, inner->slotkey + inner->slotuse,
                      newinner->slotkey);

            for (NumSlotType slot = 0; slot <= inner->slotuse; ++slot) {
                newinner->childid[slot] = copy_recursive(inner->childid[slot]);
            }

            return newinner;
        }
    }

    //! \}

private:
    //! Compute the sum of the subtree sizes of children of inner with index
    //! between begin and end
    // vectorisation disabled because g++ miscompiles this for avx-512 :(
#if defined(__GNUC__) && !defined(__clang__)
    __attribute__((optimize("no-tree-vectorize")))
#endif
    static size_type sum_subtree_size(const InnerNode* inner, SlotIndexType begin,
                                      SlotIndexType end) noexcept {
        size_type result = 0;
        if (inner->level == 1) {
            for (SlotIndexType i = begin; i < end; i++) {
                node* n = inner->childid[i];
                TLX_BTREE_ASSERT(n->is_leafnode());
                result += n->slotuse;
            }
        } else {
            for (SlotIndexType i = begin; i < end; i++) {
                const node* n = inner->childid[i];
                TLX_BTREE_ASSERT(!n->is_leafnode());
                // enabling this check causes the miscompilation to go away
                // tlx_die_unless(!n->is_leafnode());
                result += reinterpret_cast<const InnerNode*>(n)->subtree_size;
            }
        }
        return result;
    }
    static size_type sum_subtree_size(const InnerNode* inner) {
        return sum_subtree_size(inner, 0, inner->slotuse + 1);
    }

    //! Calculate the shifted subtree size when copying the first shiftnum nodes
    //! from inner to target and update subtreesizes accordingly Used for
    //! balancing when joining leaves
    static void shift_subtree_size(InnerNode* inner, InnerNode* target,
                                   SlotIndexType shiftnum,
                                   SlotIndexType beginIndex = 0) noexcept {
        size_type shifted_subtree_size =
            sum_subtree_size(inner, beginIndex, beginIndex + shiftnum);
        target->subtree_size += shifted_subtree_size;
        inner->subtree_size -= shifted_subtree_size;
        TLX_BTREE_PRINT("BTree::shift_subtree_size: subtree of size "
                        << shifted_subtree_size << " moved from " << inner
                        << " (new size: " << inner->subtree_size << ") to " << target
                        << " (new size: " << target->subtree_size << ")");
    }

public:
    //! \name Public Insertion Functions
    //! \{

    //! Attempt to insert a key/data pair into the B+ tree. If the tree does not
    //! allow duplicate keys, then the insert may fail if it is already present.
    std::pair<iterator, bool> insert(const value_type& x) noexcept {
        return insert_start(key_of_value::get(x), x);
    }

    //! Attempt to insert a key/data pair into the B+ tree. The iterator hint is
    //! currently ignored by the B+ tree insertion routine.
    iterator insert(iterator /* hint */, const value_type& x) noexcept {
        return insert_start(key_of_value::get(x), x).first;
    }

    //! Attempt to insert the range [first,last) of value_type pairs into the B+
    //! tree. Each key/data pair is inserted individually; to bulk load the
    //! tree, use a constructor with range.
    template <typename InputIterator>
    void insert(InputIterator first, InputIterator last) noexcept {
        InputIterator iter = first;
        while (iter != last) {
            insert(*iter);
            ++iter;
        }
    }

    //! \}

private:
    //! \name Private Insertion Functions
    //! \{

    //! Start the insertion descent at the current root and handle root splits.
    //! Returns true if the item was inserted
    std::pair<iterator, bool> insert_start(const key_type& key,
                                           const value_type& value) noexcept {
        node* newchild = nullptr;
        key_type newkey = key_type();

        if (root_ == nullptr) {
            root_ = head_leaf_ = tail_leaf_ = allocate_leaf();
        }

        std::pair<iterator, bool> r =
            insert_descend(root_, key, value, &newkey, &newchild);

        // insert_descend was called with root_ as first argument, so there is no point in testing of root_ is full now
        if (newchild) {
            new_root(newchild, newkey);
        }

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif

        if (self_verify) {
            verify();
            TLX_BTREE_ASSERT(exists(key));
        }

        return r;
    }

    /*!
     * Insert an item into the B+ tree.
     *
     * Descend down the nodes to a leaf, insert the key/data pair in a free
     * slot. If the node overflows, then it must be split and the new split node
     * inserted into the parent. Unroll / this splitting up to the root.
     */
    std::pair<iterator, bool> insert_descend(node* n, const key_type& key,
                                             const value_type& value,
                                             key_type* splitkey,
                                             node** splitnode) noexcept {
        if (!n->is_leafnode()) {
            InnerNode* inner = static_cast<InnerNode*>(n);

            key_type newkey = key_type();
            node* newchild = nullptr;

            SlotIndexType slot = find_lower(inner, key);

            TLX_BTREE_PRINT("BTree::insert_descend into " << inner->childid[slot]);

            std::pair<iterator, bool> r = insert_descend(
                inner->childid[slot], key, value, &newkey, &newchild);

            if (newchild) {
                TLX_BTREE_PRINT("BTree::insert_descend newchild"
                                << " with key " << newkey << " node "
                                << newchild << " at slot " << slot);

                if (inner->is_full()) {
                    size_type new_subtree_size =
                        newchild->is_leafnode()
                            ? newchild->slotuse
                            : static_cast<InnerNode*>(newchild)->subtree_size;

                    split_inner_node(inner, splitkey, splitnode, slot);

                    TLX_BTREE_PRINT("BTree::insert_descend done split_inner:"
                                    << " putslot: " << slot << " putkey: " << newkey
                                    << " upkey: " << *splitkey
                                    << " subtree_size: " << inner->subtree_size);

#ifdef TLX_BTREE_DEBUG
                    if (debug) {
                        print_node(std::cout, inner);
                        print_node(std::cout, *splitnode);
                    }
#endif

                    // check if insert slot is in the split sibling node
                    TLX_BTREE_PRINT("BTree::insert_descend switch: "
                                    << slot << " > " << inner->slotuse + 1);

                    if (slot == inner->slotuse + 1 &&
                        inner->slotuse < (*splitnode)->slotuse) {
                        // special case when the insert slot matches the split
                        // place between the two nodes, then the insert key
                        // becomes the split key.

                        TLX_BTREE_ASSERT(inner->slotuse + 1 < inner_slotmax);

                        InnerNode* split = static_cast<InnerNode*>(*splitnode);

                        node* moved = split->childid[0];
                        size_type moved_subtreesize =
                            moved->is_leafnode()
                                ? moved->slotuse
                                : static_cast<InnerNode*>(moved)->subtree_size;

                        TLX_BTREE_PRINT(
                            "BTree::insert_descend: special case! moved: "
                            << moved_subtreesize
                            << " subtree_size: " << inner->subtree_size);
                        // move the split key and it's datum into the left node
                        inner->slotkey[inner->slotuse] = *splitkey;
                        inner->childid[inner->slotuse + 1] = moved;
                        inner->slotuse++;

                        inner->subtree_size += moved_subtreesize;
                        split->subtree_size += new_subtree_size - moved_subtreesize;

                        // set new split key and move corresponding datum into
                        // right node
                        split->childid[0] = newchild;
                        *splitkey = newkey;

                        return r;
                    } else if (slot >= inner->slotuse + 1) {
                        // in case the insert slot is in the newly create split
                        // node, we reuse the code below.

                        slot -= inner->slotuse + 1;
                        InnerNode* split = static_cast<InnerNode*>(*splitnode);
                        inner = split;
                        TLX_BTREE_PRINT("BTree::insert_descend switching to "
                                        "splitted node "
                                        << inner << " slot " << slot);
                    }
                    inner->subtree_size +=
                        new_subtree_size - 1; // will be incremented later
                } else {
                    // inner->is_full(), move items and put pointer to child
                    // node into correct slot
                    TLX_BTREE_ASSERT(slot >= 0 && slot <= inner->slotuse);
                }

                std::copy_backward(inner->slotkey + slot,
                                   inner->slotkey + inner->slotuse,
                                   inner->slotkey + inner->slotuse + 1);
                std::copy_backward(inner->childid + slot,
                                   inner->childid + inner->slotuse + 1,
                                   inner->childid + inner->slotuse + 2);

                inner->slotkey[slot] = newkey;
                inner->childid[slot + 1] = newchild;
                inner->slotuse++;
            } // newchild
            if (r.second) {
                inner->subtree_size++;
            }
            return r;
        } else // n->is_leafnode() == true
        {
            LeafNode* leaf = static_cast<LeafNode*>(n);

            SlotIndexType slot = find_lower(leaf, key);

            if (!allow_duplicates && slot < leaf->slotuse &&
                key_equal(key, leaf->key(slot))) {
                return std::pair<iterator, bool>(iterator(leaf, slot), false);
            }

            if (leaf->is_full()) {
                split_leaf_node(leaf, splitkey, splitnode);

                // check if insert slot is in the split sibling node
                if (slot >= leaf->slotuse) {
                    slot -= leaf->slotuse;
                    leaf = static_cast<LeafNode*>(*splitnode);
                }
            }

            // move items and put data item into correct data slot
            TLX_BTREE_ASSERT(slot >= 0 && slot <= leaf->slotuse);

            std::copy_backward(leaf->slotdata + slot,
                               leaf->slotdata + leaf->slotuse,
                               leaf->slotdata + leaf->slotuse + 1);

            leaf->slotdata[slot] = value;
            leaf->slotuse++;

            if (splitnode && leaf != *splitnode && slot == leaf->slotuse - 1) {
                // special case: the node was split, and the insert is at the
                // last slot of the old node. then the splitkey must be updated.
                *splitkey = key;
            }

            return std::pair<iterator, bool>(iterator(leaf, slot), true);
        }
    }

    //! Split up a leaf node into two equally-filled sibling leaves. Returns the
    //! new nodes and it's insertion key in the two parameters.
    void split_leaf_node(LeafNode* leaf, key_type* out_newkey,
                         node** out_newleaf) noexcept {
        TLX_BTREE_ASSERT(leaf->is_full());

        SlotIndexType mid = (leaf->slotuse >> 1);

        TLX_BTREE_PRINT("BTree::split_leaf_node on " << leaf);

        LeafNode* newleaf = allocate_leaf();

        newleaf->slotuse = NumSlotType(leaf->slotuse - mid);

        newleaf->next_leaf = leaf->next_leaf;
        if (newleaf->next_leaf == nullptr) {
            TLX_BTREE_ASSERT(leaf == tail_leaf_);
            tail_leaf_ = newleaf;
        } else {
            newleaf->next_leaf->prev_leaf = newleaf;
        }

        std::copy(leaf->slotdata + mid, leaf->slotdata + leaf->slotuse,
                  newleaf->slotdata);

        leaf->slotuse = NumSlotType(mid);
        leaf->next_leaf = newleaf;
        newleaf->prev_leaf = leaf;

        *out_newkey = leaf->key(leaf->slotuse - 1);
        *out_newleaf = newleaf;
    }

    //! Split up an inner node into two equally-filled sibling nodes. Returns
    //! the new nodes and it's insertion key in the two parameters. Requires the
    //! slot of the item will be inserted, so the nodes will be the same size
    //! after the insert.
    void split_inner_node(InnerNode* inner, key_type* out_newkey,
                          node** out_newinner, SlotIndexType addslot) noexcept {
        TLX_BTREE_ASSERT(inner->is_full());

        SlotIndexType mid = (inner->slotuse >> 1);

        TLX_BTREE_PRINT("BTree::split_inner: mid " << mid << " addslot "
                                                   << addslot);

        // if the split is uneven and the overflowing item will be put into the
        // larger node, then the smaller split node may underflow
        if (addslot <= mid && mid > inner->slotuse - (mid + 1))
            mid--;

        TLX_BTREE_PRINT("BTree::split_inner: mid " << mid << " addslot "
                                                   << addslot);

        TLX_BTREE_PRINT("BTree::split_inner_node on "
                        << inner << " into two nodes " << mid << " and "
                        << inner->slotuse - (mid + 1) << " sized");

        InnerNode* newinner = allocate_inner(inner->level);

        newinner->slotuse = NumSlotType(inner->slotuse - (mid + 1));

        std::copy(inner->slotkey + mid + 1, inner->slotkey + inner->slotuse,
                  newinner->slotkey);
        std::copy(inner->childid + mid + 1, inner->childid + inner->slotuse + 1,
                  newinner->childid);

        inner->slotuse = NumSlotType(mid);
        newinner->subtree_size = sum_subtree_size(newinner);
        inner->subtree_size = sum_subtree_size(inner);

        TLX_BTREE_PRINT("BTree::split_inner: subtree size of "
                        << inner << " decreased from "
                        << (inner->subtree_size + newinner->subtree_size) << " to "
                        << inner->subtree_size << ", " << newinner->subtree_size);

        *out_newkey = inner->key(mid);
        *out_newinner = newinner;
    }

    //! \}

public:
    //! \name Bulk Loader - Construct Tree from Sorted Sequence
    //! \{

    //! Bulk load a sorted range. Loads items into leaves and constructs a
    //! B-tree above them. The tree must be empty when calling this function.
    template <typename Iterator>
    void bulk_load(Iterator ibegin, Iterator iend) noexcept {
        TLX_BTREE_ASSERT(empty());
        TLX_BTREE_ASSERT(iend - ibegin >= 0);

        // calculate number of leaves needed, round up.
        size_t num_items = static_cast<size_t>(iend - ibegin);
        size_t num_leaves = (num_items + leaf_slotmax - 1) / leaf_slotmax;

        TLX_BTREE_PRINT("BTree::bulk_load, level 0: "
                        << size() << " items into " << num_leaves
                        << " leaves with up to "
                        << ((iend - ibegin + num_leaves - 1) / num_leaves)
                        << " items per leaf.");

        Iterator it = ibegin;
        for (size_t i = 0; i < num_leaves; ++i) {
            // allocate new leaf node
            LeafNode* leaf = allocate_leaf();

            // copy keys or (key,value) pairs into leaf nodes, uses template
            // switch leaf->set_slot().
            leaf->slotuse = static_cast<NumSlotType>(num_items / (num_leaves - i));
            for (SlotIndexType s = 0; s < leaf->slotuse; ++s, ++it)
                leaf->set_slot(s, *it);

            if (tail_leaf_ != nullptr) {
                tail_leaf_->next_leaf = leaf;
                leaf->prev_leaf = tail_leaf_;
            } else {
                head_leaf_ = leaf;
            }
            tail_leaf_ = leaf;

            num_items -= leaf->slotuse;
        }

        TLX_BTREE_ASSERT(it == iend && num_items == 0);

        // if the btree is so small to fit into one leaf, then we're done.
        if (head_leaf_ == tail_leaf_) {
            root_ = head_leaf_;
            return;
        }

        // create first level of inner nodes, pointing to the leaves.
        size_t num_parents =
            (num_leaves + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

        TLX_BTREE_PRINT("BTree::bulk_load, level 1: "
                        << num_leaves << " leaves in " << num_parents
                        << " inner nodes with up to "
                        << ((num_leaves + num_parents - 1) / num_parents)
                        << " leaves per inner node.");

        // save inner nodes and maxkey for next level.
        using nextlevel_type = std::pair<InnerNode*, const key_type*>;
        nextlevel_type* nextlevel = new nextlevel_type[num_parents];

        LeafNode* leaf = head_leaf_;
        for (size_t i = 0; i < num_parents; ++i) {
            // allocate new inner node at level 1
            InnerNode* n = allocate_inner(1);

            n->slotuse = static_cast<NumSlotType>(num_leaves / (num_parents - i));
            TLX_BTREE_ASSERT(n->slotuse > 0);
            // this counts keys, but an inner node has keys+1 children.
            --n->slotuse;

            n->subtree_size = 0;
            // copy last key from each leaf and set child as well as subtree size
            for (NumSlotType s = 0; s < n->slotuse; ++s) {
                n->slotkey[s] = leaf->key(leaf->slotuse - 1);
                n->childid[s] = leaf;
                n->subtree_size += leaf->slotuse;
                leaf = leaf->next_leaf;
            }
            n->childid[n->slotuse] = leaf;
            n->subtree_size += leaf->slotuse;

            // track max key of any descendant.
            nextlevel[i].first = n;
            nextlevel[i].second = &leaf->key(leaf->slotuse - 1);

            leaf = leaf->next_leaf;
            num_leaves -= n->slotuse + 1;
        }

        TLX_BTREE_ASSERT(leaf == nullptr && num_leaves == 0);

        // recursively build inner nodes pointing to inner nodes.
        for (LevelType level = 2; num_parents != 1; ++level) {
            size_t num_children = num_parents;
            num_parents =
                (num_children + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

            TLX_BTREE_PRINT("BTree::bulk_load, level "
                            << level << ": " << num_children << " children in "
                            << num_parents << " inner nodes with up to "
                            << ((num_children + num_parents - 1) / num_parents)
                            << " children per inner node.");

            size_t inner_index = 0;
            for (size_t i = 0; i < num_parents; ++i) {
                // allocate new inner node at level
                InnerNode* n = allocate_inner(level);

                n->slotuse =
                    static_cast<NumSlotType>(num_children / (num_parents - i));
                TLX_BTREE_ASSERT(n->slotuse > 0);
                // this counts keys, but an inner node has keys+1 children.
                --n->slotuse;
                n->subtree_size = 0;

                // copy children and maxkeys from nextlevel
                for (NumSlotType s = 0; s < n->slotuse; ++s) {
                    n->slotkey[s] = *nextlevel[inner_index].second;
                    n->childid[s] = nextlevel[inner_index].first;
                    n->subtree_size += nextlevel[inner_index].first->subtree_size;
                    ++inner_index;
                }
                n->childid[n->slotuse] = nextlevel[inner_index].first;
                n->subtree_size += nextlevel[inner_index].first->subtree_size;

                // reuse nextlevel array for parents, because we can overwrite
                // slots we've already consumed.
                nextlevel[i].first = n;
                nextlevel[i].second = nextlevel[inner_index].second;

                ++inner_index;
                num_children -= n->slotuse + 1;
            }

            TLX_BTREE_ASSERT(num_children == 0);
        }

        root_ = nextlevel[0].first;
        delete[] nextlevel;

        if (self_verify)
            verify();
    }

    //! \}

private:
    //! \name Support Class Encapsulating Deletion Results
    //! \{

    //! Result flags of recursive deletion.
    enum result_flags_t {
        //! Deletion successful and no fix-ups necessary.
        btree_ok = 0,

        //! Deletion not successful because key was not found.
        btree_not_found = 1,

        //! Deletion successful, the last key was updated so parent slotkeys
        //! need updates.
        btree_update_lastkey = 2,

        //! Deletion successful, children nodes were merged and the parent needs
        //! to remove the empty node.
        btree_fixmerge = 4
    };

    //! B+ tree recursive deletion has much information which it needs to be
    //! passed upward.
    struct result_t {
        //! Merged result flags
        result_flags_t flags;

        //! The key to be updated at the parent's slot
        key_type lastkey;

        //! Constructor of a result with a specific flag, this can also be used
        //! as for implicit conversion.
        result_t(result_flags_t f = btree_ok) // NOLINT
            : flags(f), lastkey() {}

        //! Constructor with a lastkey value.
        result_t(result_flags_t f, const key_type& k) : flags(f), lastkey(k) {}

        //! Test if this result object has a given flag set.
        bool has(result_flags_t f) const {
            return (flags & f) != 0;
        }

        //! Merge two results OR-ing the result flags and overwriting lastkeys.
        result_t& operator|=(const result_t& other) {
            flags = result_flags_t(flags | other.flags);

            // we overwrite existing lastkeys on purpose
            if (other.has(btree_update_lastkey))
                lastkey = other.lastkey;

            return *this;
        }
    };

    //! \}

public:
    //! \name Public Erase Functions
    //! \{

    //! Erases one (the first) of the key/data pairs associated with the given
    //! key.
    bool erase_one(const key_type& key) noexcept {
        TLX_BTREE_PRINT("BTree::erase_one(" << key << ") on btree size " << size());

        if (self_verify)
            verify();

        if (!root_)
            return false;

        result_t result = erase_one_descend(key, root_, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, 0);

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif
        if (self_verify)
            verify();

        return !result.has(btree_not_found);
    }

    //! Erases all the key/data pairs associated with the given key. This is
    //! implemented using erase_one().
    size_type erase(const key_type& key) noexcept {
        size_type c = 0;

        while (erase_one(key)) {
            ++c;
            if (!allow_duplicates)
                break;
        }

        return c;
    }

    //! Erase the key/data pair referenced by the iterator.
    void erase(iterator iter) noexcept {
        TLX_BTREE_PRINT("BTree::erase_iter(" << iter.curr_leaf << "," << iter.curr_slot
                                             << ") on btree size " << size());

        if (self_verify)
            verify();

        if (!root_)
            return;

        erase_iter_descend(iter, root_, nullptr, nullptr, nullptr, nullptr,
                           nullptr, 0);

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif
        if (self_verify)
            verify();
    }

#ifdef BTREE_TODO
    //! Erase all key/data pairs in the range [first,last). This function is
    //! currently not implemented by the B+ Tree.
    void erase(iterator /* first */, iterator /* last */) {
        abort();
    }
#endif

    //! \}

private:
    //! \name Private Erase Functions
    //! \{

    /*!
     * Erase one (the first) key/data pair in the B+ tree matching key.
     *
     * Descends down the tree in search of key. During the descent the parent,
     * left and right siblings and their parents are computed and passed
     * down. Once the key/data pair is found, it is removed from the leaf. If
     * the leaf underflows 6 different cases are handled. These cases resolve
     * the underflow by shifting key/data pairs from adjacent sibling nodes,
     * merging two sibling nodes or trimming the tree.
     */
    result_t erase_one_descend(const key_type& key, node* curr, node* left,
                               node* right, InnerNode* left_parent,
                               InnerNode* right_parent, InnerNode* parent,
                               SlotIndexType parentslot) noexcept {
        if (curr->is_leafnode()) {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            LeafNode* left_leaf = static_cast<LeafNode*>(left);
            LeafNode* right_leaf = static_cast<LeafNode*>(right);

            SlotIndexType slot = find_lower(leaf, key);

            if (slot >= leaf->slotuse || !key_equal(key, leaf->key(slot))) {
                TLX_BTREE_PRINT("Could not find key " << key << " to erase.");

                return btree_not_found;
            }

            TLX_BTREE_PRINT("Found key in leaf " << curr << " at slot " << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;

            result_t myres = btree_ok;

            // if the last key of the leaf was changed, the parent is notified
            // and updates the key of this leaf
            if (slot == leaf->slotuse) {
                if (parent && parentslot < parent->slotuse) {
                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = leaf->key(leaf->slotuse - 1);
                } else {
                    if (leaf->slotuse >= 1) {
                        TLX_BTREE_PRINT("Scheduling lastkeyupdate: key "
                                        << leaf->key(leaf->slotuse - 1));
                        myres |= result_t(btree_update_lastkey,
                                          leaf->key(leaf->slotuse - 1));
                    } else {
                        TLX_BTREE_ASSERT(leaf == root_);
                    }
                }
            }

            if (leaf->is_underflow() && !(leaf == root_ && leaf->slotuse >= 1)) {
                // determine what to do about the underflow

                // case : if this empty leaf is the root, then delete all nodes
                // and set root to nullptr.
                if (left_leaf == nullptr && right_leaf == nullptr) {
                    TLX_BTREE_ASSERT(leaf == root_);
                    TLX_BTREE_ASSERT(leaf->slotuse == 0);

                    free_node(root_);

                    root_ = leaf = nullptr;
                    head_leaf_ = tail_leaf_ = nullptr;
                    TLX_BTREE_ASSERT(size() == 0);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_leaf == nullptr || left_leaf->is_few()) &&
                         (right_leaf == nullptr || right_leaf->is_few())) {
                    if (left_parent == parent)
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_leaf != nullptr && left_leaf->is_few()) &&
                         (right_leaf != nullptr && !right_leaf->is_few())) {
                    if (right_parent == parent)
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    else
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_leaf != nullptr && !left_leaf->is_few()) &&
                         (right_leaf != nullptr && right_leaf->is_few())) {
                    if (left_parent == parent)
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent) {
                    if (left_leaf->slotuse <= right_leaf->slotuse)
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    else
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                } else {
                    if (left_parent == parent)
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    else
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                }
            }

            return myres;
        } else // !curr->is_leafnode()
        {
            InnerNode* inner = static_cast<InnerNode*>(curr);
            InnerNode* left_inner = static_cast<InnerNode*>(left);
            InnerNode* right_inner = static_cast<InnerNode*>(right);

            node *myleft, *myright;
            InnerNode *myleft_parent, *myright_parent;

            SlotIndexType slot = find_lower(inner, key);

            if (slot == 0) {
                myleft =
                    (left == nullptr)
                        ? nullptr
                        : static_cast<InnerNode*>(left)->childid[left->slotuse - 1];
                myleft_parent = left_parent;
            } else {
                myleft = inner->childid[slot - 1];
                myleft_parent = inner;
            }

            if (slot == inner->slotuse) {
                myright = (right == nullptr)
                              ? nullptr
                              : static_cast<InnerNode*>(right)->childid[0];
                myright_parent = right_parent;
            } else {
                myright = inner->childid[slot + 1];
                myright_parent = inner;
            }

            TLX_BTREE_PRINT("erase_one_descend into " << inner->childid[slot]);

            result_t result =
                erase_one_descend(key, inner->childid[slot], myleft, myright,
                                  myleft_parent, myright_parent, inner, slot);

            result_t myres = btree_ok;

            if (result.has(btree_not_found)) {
                return result;
            }

            inner->subtree_size--;

            if (result.has(btree_update_lastkey)) {
                if (parent && parentslot < parent->slotuse) {
                    TLX_BTREE_PRINT("Fixing lastkeyupdate: key "
                                    << result.lastkey << " into parent " << parent
                                    << " at parentslot " << parentslot);

                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = result.lastkey;
                } else {
                    TLX_BTREE_PRINT("Forwarding lastkeyupdate: key "
                                    << result.lastkey);
                    myres |= result_t(btree_update_lastkey, result.lastkey);
                }
            }

            if (result.has(btree_fixmerge)) {
                // either the current node or the next is empty and should be
                // removed
                if (inner->childid[slot]->slotuse != 0)
                    slot++;

                // this is the child slot invalidated by the merge
                TLX_BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

                free_node(inner->childid[slot]);

                std::copy(inner->slotkey + slot, inner->slotkey + inner->slotuse,
                          inner->slotkey + slot - 1);
                std::copy(inner->childid + slot + 1,
                          inner->childid + inner->slotuse + 1,
                          inner->childid + slot);

                inner->slotuse--;

                if (inner->level == 1) {
                    // fix split key for children leaves
                    slot--;
                    LeafNode* child = static_cast<LeafNode*>(inner->childid[slot]);
                    inner->slotkey[slot] = child->key(child->slotuse - 1);
                }
            }

            if (inner->is_underflow() && !(inner == root_ && inner->slotuse >= 1)) {
                // case: the inner node is the root and has just one child. that
                // child becomes the new root
                if (left_inner == nullptr && right_inner == nullptr) {
                    TLX_BTREE_ASSERT(inner == root_);
                    TLX_BTREE_ASSERT(inner->slotuse == 0);

                    root_ = inner->childid[0];

                    inner->slotuse = 0;
                    free_node(inner);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_inner == nullptr || left_inner->is_few()) &&
                         (right_inner == nullptr || right_inner->is_few())) {
                    if (left_parent == parent)
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                    else
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_inner != nullptr && left_inner->is_few()) &&
                         (right_inner != nullptr && !right_inner->is_few())) {
                    if (right_parent == parent)
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    else
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_inner != nullptr && !left_inner->is_few()) &&
                         (right_inner != nullptr && right_inner->is_few())) {
                    if (left_parent == parent)
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    else
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent) {
                    if (left_inner->slotuse <= right_inner->slotuse)
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    else
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                } else {
                    if (left_parent == parent)
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    else
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                }
            }

            return myres;
        }
    }

    /*!
     * Erase one key/data pair referenced by an iterator in the B+ tree.
     *
     * Descends down the tree in search of an iterator. During the descent the
     * parent, left and right siblings and their parents are computed and passed
     * down. The difficulty is that the iterator contains only a pointer to a
     * LeafNode, which means that this function must do a recursive depth first
     * search for that leaf node in the subtree containing all pairs of the same
     * key. This subtree can be very large, even the whole tree, though in
     * practice it would not make sense to have so many duplicate keys.
     *
     * Once the referenced key/data pair is found, it is removed from the leaf
     * and the same underflow cases are handled as in erase_one_descend.
     */
    result_t erase_iter_descend(const iterator& iter, node* curr, node* left,
                                node* right, InnerNode* left_parent,
                                InnerNode* right_parent, InnerNode* parent,
                                SlotIndexType parentslot) noexcept {
        if (curr->is_leafnode()) {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            LeafNode* left_leaf = static_cast<LeafNode*>(left);
            LeafNode* right_leaf = static_cast<LeafNode*>(right);

            // if this is not the correct leaf, get next step in recursive
            // search
            if (leaf != iter.curr_leaf) {
                return btree_not_found;
            }

            if (iter.curr_slot >= leaf->slotuse) {
                TLX_BTREE_PRINT("Could not find iterator ("
                                << iter.curr_leaf << "," << iter.curr_slot
                                << ") to erase. Invalid leaf node?");

                return btree_not_found;
            }

            SlotIndexType slot = iter.curr_slot;

            TLX_BTREE_PRINT("Found iterator in leaf " << curr << " at slot "
                                                      << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;

            result_t myres = btree_ok;

            // if the last key of the leaf was changed, the parent is notified
            // and updates the key of this leaf
            if (slot == leaf->slotuse) {
                if (parent && parentslot < parent->slotuse) {
                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = leaf->key(leaf->slotuse - 1);
                } else {
                    if (leaf->slotuse >= 1) {
                        TLX_BTREE_PRINT("Scheduling lastkeyupdate: key "
                                        << leaf->key(leaf->slotuse - 1));
                        myres |= result_t(btree_update_lastkey,
                                          leaf->key(leaf->slotuse - 1));
                    } else {
                        TLX_BTREE_ASSERT(leaf == root_);
                    }
                }
            }

            if (leaf->is_underflow() && !(leaf == root_ && leaf->slotuse >= 1)) {
                // determine what to do about the underflow

                // case : if this empty leaf is the root, then delete all nodes
                // and set root to nullptr.
                if (left_leaf == nullptr && right_leaf == nullptr) {
                    TLX_BTREE_ASSERT(leaf == root_);
                    TLX_BTREE_ASSERT(leaf->slotuse == 0);

                    free_node(root_);

                    root_ = leaf = nullptr;
                    head_leaf_ = tail_leaf_ = nullptr;
                    TLX_BTREE_ASSERT(size() == 0);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_leaf == nullptr || left_leaf->is_few()) &&
                         (right_leaf == nullptr || right_leaf->is_few())) {
                    if (left_parent == parent)
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_leaf != nullptr && left_leaf->is_few()) &&
                         (right_leaf != nullptr && !right_leaf->is_few())) {
                    if (right_parent == parent) {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    } else {
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_leaf != nullptr && !left_leaf->is_few()) &&
                         (right_leaf != nullptr && right_leaf->is_few())) {
                    if (left_parent == parent) {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    } else {
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent) {
                    if (left_leaf->slotuse <= right_leaf->slotuse) {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    } else {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    }
                } else {
                    if (left_parent == parent) {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    } else {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    }
                }
            }

            return myres;
        } else // !curr->is_leafnode()
        {
            InnerNode* inner = static_cast<InnerNode*>(curr);
            InnerNode* left_inner = static_cast<InnerNode*>(left);
            InnerNode* right_inner = static_cast<InnerNode*>(right);

            // find first slot below which the searched iterator might be
            // located.

            result_t result;
            SlotIndexType slot = find_lower(inner, iter.key());

            while (slot <= inner->slotuse) {
                node *myleft, *myright;
                InnerNode *myleft_parent, *myright_parent;

                if (slot == 0) {
                    myleft =
                        (left == nullptr)
                            ? nullptr
                            : static_cast<InnerNode*>(left)->childid[left->slotuse - 1];
                    myleft_parent = left_parent;
                } else {
                    myleft = inner->childid[slot - 1];
                    myleft_parent = inner;
                }

                if (slot == inner->slotuse) {
                    myright = (right == nullptr)
                                  ? nullptr
                                  : static_cast<InnerNode*>(right)->childid[0];
                    myright_parent = right_parent;
                } else {
                    myright = inner->childid[slot + 1];
                    myright_parent = inner;
                }

                TLX_BTREE_PRINT("erase_iter_descend into " << inner->childid[slot]);

                result = erase_iter_descend(iter, inner->childid[slot], myleft,
                                            myright, myleft_parent,
                                            myright_parent, inner, slot);

                if (!result.has(btree_not_found))
                    break;

                // continue recursive search for leaf on next slot

                if (slot < inner->slotuse &&
                    key_less(inner->slotkey[slot], iter.key())) {
                    return btree_not_found;
                }

                ++slot;
            }

            if (slot > inner->slotuse)
                return btree_not_found;

            inner->subtree_size--;

            result_t myres = btree_ok;

            if (result.has(btree_update_lastkey)) {
                if (parent && parentslot < parent->slotuse) {
                    TLX_BTREE_PRINT("Fixing lastkeyupdate: key "
                                    << result.lastkey << " into parent " << parent
                                    << " at parentslot " << parentslot);

                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = result.lastkey;
                } else {
                    TLX_BTREE_PRINT("Forwarding lastkeyupdate: key "
                                    << result.lastkey);
                    myres |= result_t(btree_update_lastkey, result.lastkey);
                }
            }

            if (result.has(btree_fixmerge)) {
                // either the current node or the next is empty and should be
                // removed
                if (inner->childid[slot]->slotuse != 0)
                    slot++;

                // this is the child slot invalidated by the merge
                TLX_BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

                free_node(inner->childid[slot]);

                std::copy(inner->slotkey + slot, inner->slotkey + inner->slotuse,
                          inner->slotkey + slot - 1);
                std::copy(inner->childid + slot + 1,
                          inner->childid + inner->slotuse + 1,
                          inner->childid + slot);

                inner->slotuse--;

                if (inner->level == 1) {
                    // fix split key for children leaves
                    slot--;
                    LeafNode* child = static_cast<LeafNode*>(inner->childid[slot]);
                    inner->slotkey[slot] = child->key(child->slotuse - 1);
                }
            }

            if (inner->is_underflow() && !(inner == root_ && inner->slotuse >= 1)) {
                // case: the inner node is the root and has just one
                // child. that child becomes the new root
                if (left_inner == nullptr && right_inner == nullptr) {
                    TLX_BTREE_ASSERT(inner == root_);
                    TLX_BTREE_ASSERT(inner->slotuse == 0);

                    root_ = inner->childid[0];

                    inner->slotuse = 0;
                    free_node(inner);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                else if ((left_inner == nullptr || left_inner->is_few()) &&
                         (right_inner == nullptr || right_inner->is_few())) {
                    if (left_parent == parent) {
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                    } else {
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                    }
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_inner != nullptr && left_inner->is_few()) &&
                         (right_inner != nullptr && !right_inner->is_few())) {
                    if (right_parent == parent) {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    } else {
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_inner != nullptr && !left_inner->is_few()) &&
                         (right_inner != nullptr && right_inner->is_few())) {
                    if (left_parent == parent) {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    } else {
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent) {
                    if (left_inner->slotuse <= right_inner->slotuse) {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    } else {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    }
                } else {
                    if (left_parent == parent) {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    } else {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    }
                }
            }

            return myres;
        }
    }

    //! Merge two leaf nodes. The function moves all key/data pairs from right
    //! to left and sets right's slotuse to zero. The right slot is then removed
    //! by the calling parent node.
    result_t merge_leaves(LeafNode* left, LeafNode* right,
                          InnerNode* parent) noexcept {
        TLX_BTREE_PRINT("Merge leaf nodes " << left << " and " << right
                                            << " with common parent " << parent
                                            << ".");
        (void)parent;

        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < leaf_slotmax);

        std::copy(right->slotdata, right->slotdata + right->slotuse,
                  left->slotdata + left->slotuse);

        left->slotuse = NumSlotType(left->slotuse + right->slotuse);

        left->next_leaf = right->next_leaf;
        if (left->next_leaf)
            left->next_leaf->prev_leaf = left;
        else
            tail_leaf_ = left;

        right->slotuse = 0;

        return btree_fixmerge;
    }

    //! Merge two inner nodes. The function moves all key/childid pairs from
    //! right to left and sets right's slotuse to zero. The right slot is then
    //! removed by the calling parent node.
    static result_t merge_inner(InnerNode* left, InnerNode* right,
                                InnerNode* parent,
                                SlotIndexType parentslot) noexcept {
        TLX_BTREE_PRINT("Merge inner nodes " << left << " and " << right
                                             << " with common parent " << parent
                                             << ".");

        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < inner_slotmax);

        if (self_verify) {
            // find the left node's slot in the parent's children
            SlotIndexType leftslot = 0;
            while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(parentslot == leftslot);
        }

        // retrieve the decision key from parent
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy over keys and children from right
        std::copy(right->slotkey, right->slotkey + right->slotuse,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + right->slotuse + 1,
                  left->childid + left->slotuse);

        left->slotuse = NumSlotType(left->slotuse + right->slotuse);
        right->slotuse = 0;

        left->subtree_size += right->subtree_size;
        right->subtree_size = 0;

        return btree_fixmerge;
    }

    //! Balance two leaf nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static result_t shift_left_leaf(LeafNode* left, LeafNode* right,
                                    InnerNode* parent,
                                    SlotIndexType parentslot) noexcept {
        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        SlotIndexType shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) "
                        << shiftnum << " entries to left " << left << " from right "
                        << right << " with common parent " << parent << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < leaf_slotmax);

        // copy the first items from the right node to the last slot in the left
        // node.

        std::copy(right->slotdata, right->slotdata + shiftnum,
                  left->slotdata + left->slotuse);

        left->slotuse = NumSlotType(left->slotuse + shiftnum);

        // shift all slots in the right node to the left

        std::copy(right->slotdata + shiftnum, right->slotdata + right->slotuse,
                  right->slotdata);

        right->slotuse = NumSlotType(right->slotuse - shiftnum);

        // fixup parent
        if (parentslot < parent->slotuse) {
            parent->slotkey[parentslot] = left->key(left->slotuse - 1);
            return btree_ok;
        } else { // the update is further up the tree
            return result_t(btree_update_lastkey, left->key(left->slotuse - 1));
        }
    }

    //! Balance two inner nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_left_inner(InnerNode* left, InnerNode* right,
                                 InnerNode* parent,
                                 SlotIndexType parentslot) noexcept {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        SlotIndexType shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (inner) "
                        << shiftnum << " entries to left " << left << " from right "
                        << right << " with common parent " << parent << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < inner_slotmax);

        if (self_verify) {
            // find the left node's slot in the parent's children and compare to
            // parentslot

            SlotIndexType leftslot = 0;
            while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        shift_subtree_size(right, left, shiftnum);

        // copy the parent's decision slotkey and childid to the first new key
        // on the left
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy the other items from the right node to the last slots in the
        // left node.
        std::copy(right->slotkey, right->slotkey + shiftnum - 1,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + shiftnum,
                  left->childid + left->slotuse);

        left->slotuse = NumSlotType(left->slotuse + shiftnum - 1);

        // fixup parent
        parent->slotkey[parentslot] = right->slotkey[shiftnum - 1];

        // shift all slots in the right node
        std::copy(right->slotkey + shiftnum, right->slotkey + right->slotuse,
                  right->slotkey);
        std::copy(right->childid + shiftnum,
                  right->childid + right->slotuse + 1, right->childid);

        right->slotuse = NumSlotType(right->slotuse - shiftnum);
    }

    //! Balance two leaf nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_leaf(LeafNode* left, LeafNode* right, InnerNode* parent,
                                 SlotIndexType parentslot) noexcept {
        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);

        SlotIndexType shiftnum = (left->slotuse - right->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) "
                        << shiftnum << " entries to right " << right << " from left "
                        << left << " with common parent " << parent << ".");

        if (self_verify) {
            // find the left node's slot in the parent's children
            SlotIndexType leftslot = 0;
            while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < leaf_slotmax);

        std::copy_backward(right->slotdata, right->slotdata + right->slotuse,
                           right->slotdata + right->slotuse + shiftnum);

        right->slotuse = NumSlotType(right->slotuse + shiftnum);

        // copy the last items from the left node to the first slot in the right
        // node.
        std::copy(left->slotdata + left->slotuse - shiftnum,
                  left->slotdata + left->slotuse, right->slotdata);

        left->slotuse = NumSlotType(left->slotuse - shiftnum);

        parent->slotkey[parentslot] = left->key(left->slotuse - 1);
    }

    //! Balance two inner nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_inner(InnerNode* left, InnerNode* right,
                                  InnerNode* parent,
                                  SlotIndexType parentslot) noexcept {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        SlotIndexType shiftnum = (left->slotuse - right->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) "
                        << shiftnum << " entries to right " << right << " from left "
                        << left << " with common parent " << parent << ".");

        if (self_verify) {
            // find the left node's slot in the parent's children
            SlotIndexType leftslot = 0;
            while (leftslot <= parent->slotuse && parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        shift_subtree_size(left, right, shiftnum, left->slotuse - shiftnum + 1);

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < inner_slotmax);

        std::copy_backward(right->slotkey, right->slotkey + right->slotuse,
                           right->slotkey + right->slotuse + shiftnum);
        std::copy_backward(right->childid, right->childid + right->slotuse + 1,
                           right->childid + right->slotuse + 1 + shiftnum);

        right->slotuse = NumSlotType(right->slotuse + shiftnum);

        // copy the parent's decision slotkey and childid to the last new key on
        // the right
        right->slotkey[shiftnum - 1] = parent->slotkey[parentslot];

        // copy the remaining last items from the left node to the first slot in
        // the right node.
        std::copy(left->slotkey + left->slotuse - shiftnum + 1,
                  left->slotkey + left->slotuse, right->slotkey);
        std::copy(left->childid + left->slotuse - shiftnum + 1,
                  left->childid + left->slotuse + 1, right->childid);

        // copy the first to-be-removed key from the left node to the parent's
        // decision slot
        parent->slotkey[parentslot] = left->slotkey[left->slotuse - shiftnum];

        left->slotuse = NumSlotType(left->slotuse - shiftnum);
    }

    //! \}

public:
    //! Split operation; modified version of https://git.scc.kit.edu/akhremtsev/ParallelBST/blob/master/util/btree.h
    void split(BTree& left, key_type key, BTree& right) noexcept {
        if (empty()) {
            return;
        }
        [[maybe_unused]] size_type original_size = size();
        split_recursive(root_, left, key, right);

        if (left.empty()) {
            TLX_BTREE_ASSERT(left.tail_leaf_ == nullptr);
            left.head_leaf_ = nullptr;
        } else {
            left.head_leaf_ = head_leaf_;
            TLX_BTREE_ASSERT(left.tail_leaf_);
            TLX_BTREE_ASSERT(left.tail_leaf_->next_leaf == right.head_leaf_);
            left.tail_leaf_->next_leaf = nullptr;
        }
        if (right.empty()) {
            TLX_BTREE_ASSERT(right.head_leaf_ == nullptr);
            right.tail_leaf_ = nullptr;
        } else {
            right.tail_leaf_ = tail_leaf_;
            TLX_BTREE_ASSERT(right.head_leaf_);
            TLX_BTREE_ASSERT(left.tail_leaf_ == right.head_leaf_->prev_leaf);
            right.head_leaf_->prev_leaf = nullptr;
        }

        TLX_BTREE_PRINT("BTree::split tree of size "
                        << original_size << ", size left: " << left.size()
                        << " right: " << right.size());
        TLX_BTREE_ASSERT(right.size() + left.size() == original_size);

        if (root_) {
            root_ = nullptr;
            head_leaf_ = tail_leaf_ = nullptr;
        }

        if (self_verify) {
            verify();
            left.verify();
            right.verify();
        }
    }

    //! Split the tree such that left has size k. Complexity O(log n + j) where
    // j is the number of elements with key equal to the key of element with
    // rank k
    void splitAt(BTree& left, size_type k, BTree& right) noexcept {
        return splitAt(left, k, find_rank(k), right);
    }

    //! Split the tree such that left has size k. iter must be this->begin() + k
    void splitAt(BTree& left, size_type k, const_iterator iter,
                 BTree& right) noexcept {
        TLX_BTREE_ASSERT(find_rank(k) == iter);
        if (k <= 0) {
            left.clear();
            right = std::move(*this);
            return;
        }
        --iter; // split() inserts the split iterator into the left tree
        split(left, key_of_value::get(*iter), right);
        TLX_BTREE_PRINT("split tree of size "
                        << (left.size() + right.size()) << " at " << k
                        << " into trees of size " << left.size() << " and "
                        << right.size());
        TLX_BTREE_ASSERT(left.size() >= k);
        if constexpr (allow_duplicates) {
            while (left.size() > k) {
                // rbegin() returns the first invalid element, not the last
                // valid
                iterator moved{std::prev(left.end())};

                TLX_BTREE_ASSERT(right.empty() ||
                                 !key_less(key_of_value::get(*right.begin()),
                                           key_of_value::get(*moved)));
                right.insert(*moved);
                left.erase(moved);
                // TODO: If left.size()-k is too big, use split again (can
                // find iter by looping over nodes)
            }
        }
        TLX_BTREE_ASSERT(left.size() == k);
    }

    //! Delete the k smallest elements using split(). Complexity O(log n + j)
    //! where j is the number of elements with key equal to the key of element
    //! with rank k
    BTree bulk_delete(size_type k) noexcept {
        return bulk_delete(k, find_rank(k));
    }

    //! Delete the k smallest elements using split(). iter must be this->begin() + k.
    BTree bulk_delete(size_type k, iterator iter) {
        BTree left, right;
        splitAt(left, k, iter, right);
        swap(right);
        return left;
    }

private:
    using TPairTreeKey = std::pair<BTree, key_type>;

    //! Select a slot and split the node into two subtrees strictly left / right
    //! of the slot, then recursively split the subtree of the selected slot and
    //! join both left / right subtrees
    void split_recursive(node* n, BTree& left, key_type key, BTree& right) noexcept {
        if (!n->is_leafnode()) {
            InnerNode* inner = static_cast<InnerNode*>(n);
            NumSlotType slot = find_upper(inner, key);

            TPairTreeKey new_left, new_right;

            // unknown, don't set: Will be set when merging
            new_left.first.tail_leaf_ = new_right.first.head_leaf_ = nullptr;

            node* child = inner->childid[slot];
            split_inner_node(inner, slot, new_left, new_right);
            TLX_BTREE_PRINT("After splitting "
                            << n << " left size: " << new_left.first.size()
                            << ", right size: " << new_right.first.size());

            BTree bottom_left, bottom_right;
            split_recursive(child, bottom_left, key, bottom_right);

            new_left.first.join_greater(new_left.second, bottom_left, tail_leaf_);
            new_right.first.join_less(new_right.second, bottom_right, tail_leaf_);

            left.swap(new_left.first);
            right.swap(new_right.first);
            TLX_BTREE_PRINT("After joining (splitting "
                            << n << "): left size: " << left.size()
                            << " right size: " << right.size());
        } else {
            LeafNode* leaf = static_cast<LeafNode*>(n);
            NumSlotType slot = find_upper(leaf, key);
            split_leaf_node(leaf, slot, left, right);
        }
    }

    //! Split the inner node into two nodes of slots strictly left and right of
    //! the slot, return the resulting trees as well as the two split keys
    void split_inner_node(InnerNode* n, NumSlotType slot, TPairTreeKey& left,
                          TPairTreeKey& right) noexcept {
        LevelType level = n->level;
        InnerNode* new_left_root = nullptr;
        InnerNode* new_right_root = nullptr;

        TLX_BTREE_PRINT("BTree::split_inner_node: node "
                        << n << " level " << level << " slot " << slot << " of "
                        << n->slotuse);
        // slot - 1 >= n->slotuse - (slot + 1)
        if (2 * slot >= n->slotuse) {
            new_left_root = n;
        } else {
            new_right_root = n;
        }

        if (slot != 0 && new_left_root != n)
            new_left_root = allocate_inner(level);

        if (slot != n->slotuse && new_right_root != n)
            new_right_root = allocate_inner(level);

        NumSlotType slotuse = n->slotuse;
        if (slot > 0) {
            left.second = n->slotkey[slot - 1];

            // if node of the spliting tree will be in the left tree then
            // no need to copy, only change its slotuse
            if (n != new_left_root) {
                std::copy(n->slotkey, n->slotkey + slot - 1,
                          new_left_root->slotkey);
                std::copy(n->childid, n->childid + slot, new_left_root->childid);
            }
            new_left_root->slotuse = slot - 1;
            new_left_root->subtree_size = sum_subtree_size(new_left_root);
        }

        if (slot < slotuse) {
            right.second = n->slotkey[slot];

            std::copy(n->slotkey + slot + 1, n->slotkey + slotuse,
                      new_right_root->slotkey);
            std::copy(n->childid + slot + 1, n->childid + slotuse + 1,
                      new_right_root->childid);

            new_right_root->slotuse = slotuse - (slot + 1);
            new_right_root->subtree_size = sum_subtree_size(new_right_root);
        }

        left.first.root_ = new_left_root;
        right.first.root_ = new_right_root;
        if (new_left_root && new_left_root->slotuse == 0) {
            left.first.root_ = new_left_root->childid[0];
            free_node(new_left_root);
        }

        if (new_right_root && new_right_root->slotuse == 0) {
            right.first.root_ = new_right_root->childid[0];
            free_node(new_right_root);
        }

        TLX_BTREE_PRINT("new left size: " << left.first.size() << ", new right size: "
                                          << right.first.size());
    }

    //! Split the leaf node, updating the linked list of leaves
    void split_leaf_node(LeafNode* n, NumSlotType slot, BTree& left,
                         BTree& right) noexcept {
        LeafNode* new_left_root = nullptr;
        LeafNode* new_right_root = nullptr;

        TLX_BTREE_PRINT("BTree::split_leaf_node " << n << " slot " << slot
                                                  << " of " << n->slotuse);

        // slot >= n->slotuse - slot
        if (2 * slot >= n->slotuse) {
            new_left_root = n;
        } else {
            new_right_root = n;
        }

        if (slot != 0 && new_left_root != n)
            new_left_root = allocate_leaf();

        if (slot != n->slotuse && new_right_root != n)
            new_right_root = allocate_leaf();

        if (new_left_root == nullptr) {
            TLX_BTREE_ASSERT(new_right_root == n && slot == 0);
            left.root_ = nullptr;
            left.head_leaf_ = left.tail_leaf_ = n->prev_leaf;
            right.root_ = right.head_leaf_ = right.tail_leaf_ = n;
            return;
        } else if (new_right_root == nullptr) {
            TLX_BTREE_ASSERT(new_left_root == n && slot == n->slotuse);
            right.root_ = nullptr;
            right.head_leaf_ = right.tail_leaf_ = n->next_leaf;
            left.root_ = left.head_leaf_ = left.tail_leaf_ = n;
            return;
        }

        NumSlotType slotuse = n->slotuse;
        if (slot > 0) {
            if (n != new_left_root) {
                std::copy(n->slotdata, n->slotdata + slot, new_left_root->slotdata);
            }
            new_left_root->slotuse = slot;
        }

        if (slot < slotuse) {
            std::copy(n->slotdata + slot, n->slotdata + slotuse,
                      new_right_root->slotdata);
            new_right_root->slotuse = slotuse - slot;
        }
        left.root_ = new_left_root;
        right.root_ = new_right_root;

        left.head_leaf_ = left.tail_leaf_ = new_left_root;
        right.head_leaf_ = right.tail_leaf_ = new_right_root;

        // update leaflinks if the leaf was split
        if (n->next_leaf != nullptr) {
            n->next_leaf->prev_leaf = new_right_root;
        } else {
            TLX_BTREE_ASSERT(n == tail_leaf_);
            tail_leaf_ = new_right_root;
        }
        if (n->prev_leaf != nullptr) {
            n->prev_leaf->next_leaf = new_left_root;
        } else {
            TLX_BTREE_ASSERT(n == head_leaf_);
            head_leaf_ = new_left_root;
        }
        new_right_root->next_leaf = n->next_leaf;
        new_left_root->prev_leaf = n->prev_leaf;
        new_left_root->next_leaf = new_right_root;
        new_right_root->prev_leaf = new_left_root;
        TLX_BTREE_PRINT("BTree::split_leave_node "
                        << n << " into " << new_left_root << " (slotuse "
                        << new_left_root->slotuse << ") and " << new_right_root
                        << " (slotuse " << new_right_root->slotuse << ")");
        TLX_BTREE_ASSERT(!(left.root_ == nullptr && right.root_ == nullptr));
    }

public:
    //! Join this tree with another tree where every element is not less than
    //! any element in this tree.
    void join(BTree& other_tree) noexcept {
        if (empty()) {
            swap(other_tree);
            return;
        } else if (other_tree.empty()) {
            return;
        }
        TLX_BTREE_ASSERT(head_leaf_);
        TLX_BTREE_ASSERT(other_tree.head_leaf_);
        tail_leaf_->next_leaf = other_tree.head_leaf_;
        other_tree.head_leaf_->prev_leaf = tail_leaf_;
        TLX_BTREE_ASSERT(key_lessequal(key_of_value::get(*std::prev(end())),
                                       key_of_value::get(*(other_tree.begin()))));
        if (root_->level >= other_tree.root_->level) {
            join_greater(key_of_value::get(*std::prev(end())), other_tree,
                         tail_leaf_);
        } else {
            // auto& splitKey = key_of_value::get(*(other_tree.begin()));
            other_tree.join_less(key_of_value::get(*std::prev(end())), *this,
                                 other_tree.tail_leaf_);
            swap(other_tree);
        }
    }

private:
    //! Join with another tree of less or equal height where no key is greater
    //! than the given key, while on this tree no key is less
    void join_less(key_type key, BTree& other_tree, LeafNode*& tail) noexcept {
        TLX_BTREE_PRINT("BTree::join_less: key: "
                        << key << ", root: " << root_ << ", other root: "
                        << other_tree.root_ << ", size: " << size()
                        << ", other size: " << other_tree.size());
        if (empty()) {
            swap(other_tree);
            return;
        }

        head_leaf_ = other_tree.head_leaf_;

        if (other_tree.empty()) {
            return;
        }

        // This private function is only called by split() and join() and they
        // make sure the other tree's level is not greater
        TLX_BTREE_ASSERT(root_->level >= other_tree.root_->level);

        join_less_start(key, other_tree, tail);

        other_tree.root_ = nullptr;
        other_tree.head_leaf_ = other_tree.tail_leaf_ = nullptr;
    }

    //! Join with another tree of less or equal height where no key is less than
    //! the given key, while on this tree no key is greater
    void join_greater(key_type key, BTree& other_tree, LeafNode*& tail) noexcept {
        TLX_BTREE_PRINT("BTree::join_greater: key: "
                        << key << ", root: " << root_ << ", other root: "
                        << other_tree.root_ << ", size: " << size()
                        << ", other size: " << other_tree.size());
        if (empty()) {
            swap(other_tree);
            return;
        }

        tail_leaf_ = other_tree.tail_leaf_;

        if (other_tree.empty()) {
            return;
        }

        // This private function is only called by split() and join() and they
        // make sure the other tree's level is not greater
        TLX_BTREE_ASSERT(root_->level >= other_tree.root_->level);

        join_greater_start(key, other_tree, tail);

        other_tree.root_ = nullptr;
        other_tree.head_leaf_ = other_tree.tail_leaf_ = nullptr;
    }

    // *** Private Join Functions

    //! TODO: NONE is never used (except to initialize a variable that always
    //! gets overidden), and SPLITTED and NO_DIFF are treated in exactly the
    //! same way -- change to bool?  Also, SPLITTED seems like a somewhat
    //! confusing name
    enum EJoinType { NONE, MERGED, SPLITED, NO_DIFF };

    //! Either redistribute slots or append all slots of the right leave to the
    //! left leaf if possible.  Both leaves may be underflowing before this
    //! operation.  Updates the tail leave in case of merging
    EJoinType join_leaves(node* n, node* other_node, const key_type& key,
                          key_type* newkey, node** newchild,
                          LeafNode*& tail) noexcept {
        *newkey = key;

        TLX_BTREE_PRINT("join leaves " << n << " (slotuse " << n->slotuse
                                       << ") and " << other_node << " (slotuse "
                                       << other_node->slotuse << ")");

        LeafNode* leaf = static_cast<LeafNode*>(n);
        LeafNode* other_leaf = static_cast<LeafNode*>(other_node);

        // This invariant should hold if join was called from split() or the
        // public join function.
        TLX_BTREE_ASSERT(leaf->next_leaf == other_leaf);
        TLX_BTREE_ASSERT(other_leaf->prev_leaf == leaf);

        if (leaf_slotmax >= leaf->slotuse + other_leaf->slotuse) {
            TLX_BTREE_PRINT("merge leaves");
            // elements of both leaves can be placed in one node
            NumSlotType slot = leaf->slotuse;

            std::copy(other_leaf->slotdata,
                      other_leaf->slotdata + other_leaf->slotuse,
                      leaf->slotdata + slot);
            leaf->slotuse = leaf->slotuse + other_leaf->slotuse;

            leaf->next_leaf = other_leaf->next_leaf;
            if (other_leaf->next_leaf) {
                other_leaf->next_leaf->prev_leaf = leaf;
            } else {
                tail = leaf;
            }

            return MERGED;
        } else {
            // need to redistribute nodes
            if (leaf->slotuse < leaf_slotmin) {
                TLX_BTREE_PRINT("copy slots from right to left");
                std::copy(other_leaf->slotdata,
                          other_leaf->slotdata + (leaf_slotmin - leaf->slotuse),
                          leaf->slotdata + leaf->slotuse);

                *newkey =
                    leaf->key(leaf->slotuse + leaf_slotmin - leaf->slotuse - 1);

                std::copy(other_leaf->slotdata + leaf_slotmin - leaf->slotuse,
                          other_leaf->slotdata + other_leaf->slotuse,
                          other_leaf->slotdata);

                other_leaf->slotuse -= leaf_slotmin - leaf->slotuse;
                leaf->slotuse = leaf_slotmin;
                *newchild = other_leaf;
                return SPLITED;
            } else if (other_leaf->slotuse < leaf_slotmin) {
                TLX_BTREE_PRINT("copy slots from left to right");
                std::copy_backward(other_leaf->slotdata,
                                   other_leaf->slotdata + other_leaf->slotuse,
                                   other_leaf->slotdata + leaf_slotmin);

                std::copy_backward(leaf->slotdata + leaf->slotuse -
                                       (leaf_slotmin - other_leaf->slotuse),
                                   leaf->slotdata + leaf->slotuse,
                                   other_leaf->slotdata +
                                       (leaf_slotmin - other_leaf->slotuse));

                *newkey = leaf->key(leaf->slotuse -
                                    (leaf_slotmin - other_leaf->slotuse) - 1);

                leaf->slotuse -= leaf_slotmin - other_leaf->slotuse;
                other_leaf->slotuse = leaf_slotmin;
                *newchild = other_leaf;
                return SPLITED;
            }

            *newchild = other_leaf;
            return NO_DIFF;
        }
    }

    //! Either redistribute slots or append all slots from the right node to the
    //! left node if possible. Both nodes may be underflowing before this
    //! operation.
    EJoinType join_inner(node* n, node* other_node, const key_type& key,
                         key_type* newkey, node** newchild) noexcept {
        *newkey = key;

        InnerNode* inner = static_cast<InnerNode*>(n);
        InnerNode* other_inner = static_cast<InnerNode*>(other_node);

        TLX_BTREE_PRINT("BTree::join_inner: n: "
                        << n << ", slotuse: " << inner->slotuse
                        << ", other node: " << other_node << ", slotuse: "
                        << other_inner->slotuse << ", key: " << key);

        if (inner_slotmax >= inner->slotuse + other_inner->slotuse + 1) {
            // elements of both roots can be placed
            // in one node
            NumSlotType slot = inner->slotuse;
            inner->slotkey[slot] = key;

            std::copy(other_inner->slotkey,
                      other_inner->slotkey + other_inner->slotuse,
                      inner->slotkey + slot + 1);

            std::copy(other_inner->childid,
                      other_inner->childid + other_inner->slotuse + 1,
                      inner->childid + slot + 1);

            inner->subtree_size += other_inner->subtree_size;

            inner->slotuse = inner->slotuse + other_inner->slotuse + 1;
            return MERGED;
        } else { // need to redistribute nodes between nodes
            if (inner->slotuse < inner_slotmin) {
                // copy from other node to this node
                inner->slotkey[inner->slotuse] = key;

                shift_subtree_size(other_inner, inner,
                                   inner_slotmin - inner->slotuse);

                std::copy(other_inner->slotkey,
                          other_inner->slotkey + (inner_slotmin - inner->slotuse),
                          inner->slotkey + inner->slotuse + 1);

                std::copy(other_inner->childid,
                          other_inner->childid + (inner_slotmin - inner->slotuse),
                          inner->childid + inner->slotuse + 1);

                *newkey = inner->slotkey[inner->slotuse + 1 + inner_slotmin -
                                         inner->slotuse - 1];

                std::copy(other_inner->slotkey + (inner_slotmin - inner->slotuse),
                          other_inner->slotkey + other_inner->slotuse,
                          other_inner->slotkey);

                std::copy(other_inner->childid + (inner_slotmin - inner->slotuse),
                          other_inner->childid + other_inner->slotuse + 1,
                          other_inner->childid);

                other_inner->slotuse -= inner_slotmin - inner->slotuse;
                inner->slotuse = inner_slotmin;
                *newchild = other_inner;
                return SPLITED;
            } else if (other_inner->slotuse < inner_slotmin) {
                // copy from this node to other node
                shift_subtree_size(
                    inner, other_inner, inner_slotmin - other_inner->slotuse,
                    inner->slotuse - (inner_slotmin - other_inner->slotuse - 1));

                std::copy_backward(other_inner->slotkey,
                                   other_inner->slotkey + other_inner->slotuse,
                                   other_inner->slotkey + inner_slotmin);

                std::copy_backward(other_inner->childid,
                                   other_inner->childid + other_inner->slotuse + 1,
                                   other_inner->childid + inner_slotmin + 1);

                other_inner->slotkey[inner_slotmin - other_inner->slotuse - 1] =
                    key;

                std::copy_backward(inner->slotkey + inner->slotuse -
                                       (inner_slotmin - other_inner->slotuse - 1),
                                   inner->slotkey + inner->slotuse,
                                   other_inner->slotkey +
                                       (inner_slotmin - other_inner->slotuse - 1));

                std::copy_backward(inner->childid + inner->slotuse + 1 -
                                       (inner_slotmin - other_inner->slotuse),
                                   inner->childid + inner->slotuse + 1,
                                   other_inner->childid +
                                       (inner_slotmin - other_inner->slotuse));

                *newkey = inner->slotkey[inner->slotuse -
                                         (inner_slotmin - other_inner->slotuse)];

                inner->slotuse -= inner_slotmin - other_inner->slotuse - 1 + 1;
                other_inner->slotuse = inner_slotmin;
                *newchild = other_inner;
                return SPLITED;
            }

            *newchild = other_inner;
            return NO_DIFF;
        }
    }

    //! The maximum element of other_tree is not greater than the minimum
    //! element of this tree
    void join_less_start(const key_type& key, BTree& other_tree,
                         LeafNode*& tail) noexcept {
        node* newchild = nullptr;
        key_type newkey = key;
        EJoinType leaves_merged = NONE;

        if (root_->level > other_tree.root_->level) {
            join_less_descend(root_, key, other_tree, &newkey, &newchild,
                              &leaves_merged, tail);
        } else {
            TLX_BTREE_ASSERT(root_->level == other_tree.root_->level);
            if (root_->is_leafnode()) {
                TLX_BTREE_ASSERT(other_tree.head_leaf_->next_leaf == root_);
                LeafNode* root = static_cast<LeafNode*>(root_);
                LeafNode* other_root = static_cast<LeafNode*>(other_tree.root_);

                leaves_merged =
                    join_leaves(other_root, root, key, &newkey, &newchild, tail);

                if (leaves_merged == MERGED) {
                    TLX_BTREE_ASSERT(other_tree.head_leaf_ == other_tree.root_);
                    TLX_BTREE_ASSERT(root->next_leaf != nullptr ||
                                     tail == other_root);
                    free_node(root_);
                    root_ = tail_leaf_ = other_root;
                }

                if (leaves_merged == NO_DIFF || leaves_merged == SPLITED) {
                    root_ = other_root;
                    newchild = root;
                    // root->swap(*other_root);
                    // newchild = other_tree.root_;
                }
            } else {
                InnerNode* root = static_cast<InnerNode*>(root_);
                InnerNode* other_root = static_cast<InnerNode*>(other_tree.root_);

                EJoinType nodes_merged =
                    join_inner(other_root, root, key, &newkey, &newchild);

                if (nodes_merged == MERGED) {
                    root->copy_slots_from(*other_root);
                    free_node(other_tree.root_);
                }

                if (nodes_merged == NO_DIFF || nodes_merged == SPLITED) {
                    root->swap(*other_root);
                    newchild = other_tree.root_;
                }
            }
        }

        head_leaf_ = other_tree.head_leaf_;

        if (newchild) {
            new_root(newchild, newkey);
        }

#ifdef BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif

        TLX_BTREE_ASSERT(exists(key));
    }

    //! The minimum element of other_tree is not smaller than the maximum
    //! element of this tree
    void join_greater_start(const key_type& key, BTree& other_tree,
                            LeafNode*& tail) noexcept {
        node* newchild = nullptr;
        key_type newkey = key;
        EJoinType leaves_merged = NONE;

        if (root_->level > other_tree.root_->level) {
            join_greater_descend(root_, key, other_tree, &newkey, &newchild,
                                 &leaves_merged, tail);
        } else {
            TLX_BTREE_ASSERT(root_->level == other_tree.root_->level);
            EJoinType nodes_merged = NONE;
            if (root_->is_leafnode()) {
                TLX_BTREE_ASSERT(static_cast<LeafNode*>(root_)->next_leaf ==
                                 other_tree.root_);
                leaves_merged = join_leaves(root_, other_tree.root_, key,
                                            &newkey, &newchild, tail);
            } else {
                nodes_merged =
                    join_inner(root_, other_tree.root_, key, &newkey, &newchild);
            }

            if (leaves_merged == MERGED) {
                TLX_BTREE_ASSERT(other_tree.tail_leaf_ == other_tree.root_);
                TLX_BTREE_ASSERT(other_tree.tail_leaf_->next_leaf != nullptr ||
                                 tail == root_);
                other_tree.tail_leaf_ = static_cast<LeafNode*>(root_);
            }
            if (nodes_merged == MERGED || leaves_merged == MERGED) {
                free_node(other_tree.root_);
            }
        }
        tail_leaf_ = other_tree.tail_leaf_;

        if (newchild) {
            new_root(newchild, newkey);
        }
#ifdef BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif

        TLX_BTREE_ASSERT(exists(key));
    }

    //! Recursively calls itself with the leftmost child until both trees have
    //! the same level.
    void join_less_descend(node*& n, const key_type& key, BTree& other_tree,
                           key_type* splitkey, node** splitnode,
                           EJoinType* leaves_merged, LeafNode*& tail) noexcept {
        TLX_BTREE_PRINT("BTree::join_less_descend: n: "
                        << n << ", key: " << key << ", root: " << root_
                        << ", other root: " << other_tree.root_);
        if (!n->is_leafnode()) {
            InnerNode* inner = static_cast<InnerNode*>(n);

            key_type newkey;
            node* newchild = nullptr;

            if (n->level > other_tree.root_->level) {
                inner->subtree_size += other_tree.size();
                join_less_descend(inner->childid[0], key, other_tree, &newkey,
                                  &newchild, leaves_merged, tail);
            } else {
                TLX_BTREE_ASSERT(n->level == other_tree.root_->level);

                InnerNode* other_inner = static_cast<InnerNode*>(other_tree.root_);

                EJoinType type =
                    join_inner(other_inner, inner, key, splitkey, splitnode);

                if (type == MERGED) {
                    inner->copy_slots_from(*other_inner);
                    free_node(other_tree.root_);
                }

                if (type == NO_DIFF || type == SPLITED) {
                    inner->swap(*other_inner);
                    *splitnode = other_tree.root_;
                }
                return;
            }

            if (newchild) {
                if (inner->is_full()) {
                    split_inner_node(inner, splitkey, splitnode, 0);

                    if (newchild->is_leafnode()) {
                        inner->subtree_size += newchild->slotuse;
                    } else {
                        inner->subtree_size +=
                            static_cast<InnerNode*>(newchild)->subtree_size;
                    }
#ifdef BTREE_DEBUG
                    if (debug) {
                        print_node(std::cout, inner);
                        print_node(std::cout, *splitnode);
                    }
#endif
                }

                // move items and put pointer to child node into correct slot
                std::copy_backward(inner->slotkey, inner->slotkey + inner->slotuse,
                                   inner->slotkey + inner->slotuse + 1);
                std::copy_backward(inner->childid,
                                   inner->childid + inner->slotuse + 1,
                                   inner->childid + inner->slotuse + 2);

                inner->slotkey[0] = newkey;
                inner->childid[1] = newchild;
                inner->slotuse++;
            }
        } else { // n->is_leafnode()
            LeafNode* leaf = static_cast<LeafNode*>(n);
            LeafNode* other_leaf = static_cast<LeafNode*>(other_tree.root_);
            TLX_BTREE_ASSERT(other_leaf->next_leaf == leaf &&
                             leaf->prev_leaf == other_leaf);

            // would have been handled in join_greater_start; tail_leaf_ is not
            // always set correctly
            TLX_BTREE_ASSERT(leaf->next_leaf != nullptr);

            EJoinType type =
                join_leaves(other_leaf, leaf, key, splitkey, splitnode, tail);

            if (type == MERGED) {
                TLX_BTREE_ASSERT(other_tree.head_leaf_ == other_leaf);
                free_node(leaf);
                n = other_leaf;
            }

            if (type == NO_DIFF || type == SPLITED) {
                // instead of copying the content of the leaf nodes, just swap poiners
                n = other_leaf;
                *splitnode = leaf;
            }

            *leaves_merged = type;
        }
    }

    //! Recursively calls itself with the leftmost child until both trees have
    //! the same level.
    void join_greater_descend(node* n, const key_type& key, BTree& other_tree,
                              key_type* splitkey, node** splitnode,
                              EJoinType* leaves_merged, LeafNode*& tail) noexcept {
        TLX_BTREE_PRINT("BTree::join_greater_descend: n: "
                        << n << ", key: " << key << ", root: " << root_
                        << ", other root: " << other_tree.root_);
        if (!n->is_leafnode()) {
            InnerNode* inner = static_cast<InnerNode*>(n);

            TLX_BTREE_PRINT("btree::join_descend into "
                            << inner->childid[inner->slotuse - 1]);

            key_type newkey;
            node* newchild = nullptr;

            if (n->level > other_tree.root_->level) {
                inner->subtree_size += other_tree.size();
                join_greater_descend(inner->childid[inner->slotuse], key,
                                     other_tree, &newkey, &newchild,
                                     leaves_merged, tail);
            } else {
                // levels of trees equal.Check if duplicates are allowed
                // and insert new tree as if it was propagated from below
                TLX_BTREE_ASSERT(other_tree.root_->level = n->level);

                EJoinType type =
                    join_inner(n, other_tree.root_, key, splitkey, splitnode);
                if (type == MERGED)
                    free_node(other_tree.root_);
                return;
            }

            if (newchild) {
                if (inner->is_full()) {
                    split_inner_node(inner, splitkey, splitnode, inner->slotuse);
                    inner = static_cast<InnerNode*>(*splitnode);
                    if (newchild->is_leafnode()) {
                        inner->subtree_size += newchild->slotuse;
                    } else {
                        inner->subtree_size +=
                            static_cast<InnerNode*>(newchild)->subtree_size;
                    }
                }

                // move items and put pointer to child node into correct slot

                inner->slotkey[inner->slotuse] = newkey;
                inner->childid[inner->slotuse + 1] = newchild;
                inner->slotuse++;
            }
        } else {
            LeafNode* leaf = static_cast<LeafNode*>(n);
            LeafNode* other_leaf = static_cast<LeafNode*>(other_tree.root_);

            EJoinType type =
                join_leaves(leaf, other_leaf, key, splitkey, splitnode, tail);
            if (type == MERGED) {
                TLX_BTREE_ASSERT(other_tree.tail_leaf_ == other_leaf);
                TLX_BTREE_ASSERT(other_leaf->next_leaf != nullptr || tail == leaf);
                other_tree.tail_leaf_ = leaf;
                free_node(other_tree.root_);
                *leaves_merged = type;
            }
        }
    }

private:
    enum RankQuery { EXACT, LOWER_BOUND, UPPER_BOUND };

    template <RankQuery Query>
    std::pair<size_type, iterator> rankImpl(const key_type& key) noexcept {
        size_type result = 0;
        node* n = root_;
        if (!n)
            return {0, end()};

        while (!n->is_leafnode()) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            SlotIndexType slot = Query == UPPER_BOUND ? find_upper(inner, key)
                                                      : find_lower(inner, key);
            result += sum_subtree_size(inner, 0, slot);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        SlotIndexType slot = Query == UPPER_BOUND ? find_upper(leaf, key)
                                                  : find_lower(leaf, key);
        if (slot < leaf->slotuse)
            if (Query != EXACT || key_equal(key, leaf->key(slot))) {
                return {result + slot, iterator{leaf, slot}};
            }
        return {size(), end()};
    }

    template <RankQuery Query>
    std::pair<size_type, const_iterator> rankImpl(const key_type& key) const
        noexcept {
        auto [rank, iter] = const_cast<BTree*>(this)->rankImpl<Query>(key);
        return {rank, static_cast<const_iterator>(iter)};
    }


public:
    iterator find_rank(size_type rank) noexcept {
        node* n = root_;
        if (rank >= size()) {
            return end();
        }

        while (n->level > 1) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            for (SlotIndexType i = 0; i <= inner->slotuse; ++i) {
                size_type additionalRank =
                    static_cast<InnerNode*>(inner->childid[i])->subtree_size;
                if (additionalRank > rank) {
                    n = inner->childid[i];
                    break;
                }
                rank -= additionalRank;
            }
        }
        if (n->level == 1) {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            for (SlotIndexType i = 0; i <= inner->slotuse; ++i) {
                size_type additionalRank = inner->childid[i]->slotuse;
                if (additionalRank > rank) {
                    n = inner->childid[i];
                    break;
                }
                rank -= additionalRank;
            }
        }
        TLX_BTREE_ASSERT(n->is_leafnode());
        TLX_BTREE_ASSERT(rank < n->slotuse);
        return iterator(static_cast<LeafNode*>(n),
                        static_cast<SlotIndexType>(rank));
    }

    const_iterator find_rank(size_type rank) const noexcept {
        return static_cast<const_iterator>(
            const_cast<BTree*>(this)->find_rank(rank));
    }

    //! return the smallest rank (index in array) of an element with the given
    //! key or size() if no such element exists
    std::pair<size_type, const_iterator> rank_of(const key_type& key) const
        noexcept {
        return rankImpl<EXACT>(key);
    }
    std::pair<size_type, iterator> rank_of(const key_type& key) noexcept {
        return rankImpl<EXACT>(key);
    }

    //! return the rank of the element referenced by iter.
    //! worst-case complexity linear (if all elements have the same key)
    size_type rank_of(const_iterator iter) const noexcept {
        if (iter == end()) {
            return size();
        } else {
            auto [rank, it] = rank_of(key_of_value::get(*iter));
            TLX_BTREE_ASSERT(std::distance(it, iter) >= 0);
            return rank + static_cast<size_type>(std::distance(it, iter));
        }
    }

    //! return the smallest rank of an element with a key not less than the
    //! given key
    std::pair<size_type, const_iterator> rank_of_lower_bound(const key_type& key) const
        noexcept {
        return rankImpl<LOWER_BOUND>(key);
    }
    std::pair<size_type, iterator> rank_of_lower_bound(const key_type& key) noexcept {
        return rankImpl<LOWER_BOUND>(key);
    }

    //! return the smallest rank of an element with key greater than the given
    //! key.
    std::pair<size_type, const_iterator> rank_of_upper_bound(const key_type& key) const
        noexcept {
        return rankImpl<UPPER_BOUND>(key);
    }
    std::pair<size_type, iterator> rank_of_upper_bound(const key_type& key) noexcept {
        return rankImpl<UPPER_BOUND>(key);
    }


#ifdef TLX_BTREE_DEBUG

public:
    //! \name Debug Printing
    //! \{

    //! Print out the B+ tree structure with keys onto the given ostream. This
    //! function requires that the header is compiled with TLX_BTREE_DEBUG and
    //! that key_type is printable via std::ostream.
    void print(std::ostream& os) const noexcept {
        if (root_) {
            print_node(os, root_, 0, true);
        }
    }

    //! Print out only the leaves via the double linked list.
    void print_leaves(std::ostream& os) const noexcept {
        os << "leaves:" << std::endl;

        const LeafNode* n = head_leaf_;

        while (n) {
            os << "  " << n << std::endl;

            n = n->next_leaf;
        }
    }

private:
    //! Recursively descend down the tree and print out nodes.
    static void print_node(std::ostream& os, const node* node,
                           LevelType depth = 0, bool recursive = false) noexcept {
        for (LevelType i = 0; i < depth; i++)
            os << "  ";

        os << "node " << node << " level " << node->level << " slotuse "
           << node->slotuse << std::endl;

        if (node->is_leafnode()) {
            const LeafNode* leafnode = static_cast<const LeafNode*>(node);

            for (LevelType i = 0; i < depth; i++)
                os << "  ";
            os << "  leaf prev " << leafnode->prev_leaf << " next "
               << leafnode->next_leaf << std::endl;

            for (LevelType i = 0; i < depth; i++)
                os << "  ";

            for (SlotIndexType slot = 0; slot < leafnode->slotuse; ++slot) {
                // os << leafnode->key(slot) << " "
                //    << "(data: " << leafnode->slotdata[slot] << ") ";
                os << leafnode->key(slot) << "  ";
            }
            os << std::endl;
        } else {
            const InnerNode* innernode = static_cast<const InnerNode*>(node);

            for (LevelType i = 0; i < depth; i++)
                os << "  ";

            for (NumSlotType slot = 0; slot < innernode->slotuse; ++slot) {
                os << "(" << innernode->childid[slot] << ") "
                   << innernode->slotkey[slot] << " ";
            }
            os << "(" << innernode->childid[innernode->slotuse] << ")" << std::endl;

            if (recursive) {
                for (NumSlotType s = 0; s < innernode->slotuse + 1; ++s) {
                    print_node(os, innernode->childid[s], depth + 1, recursive);
                }
            }
        }
    }

//! \}
#endif

public:
    //! \name Verification of B+ Tree Invariants
    //! \{

    //! Run a thorough verification of all B+ tree invariants. The program
    //! aborts via tlx_die_unless() if something is wrong.
    void verify() const {
        key_type minkey, maxkey;
        size_type subtree_size = 0;

        if (root_) {
            subtree_size = verify_node(root_, &minkey, &maxkey);

            tlx_die_unless(subtree_size == size());
            verify_leaflinks();
        } else {
            tlx_die_unless(head_leaf_ == tail_leaf_);
            tlx_die_unless(size() == 0);
        }
    }

private:
    //! Recursively descend down the tree and verify each node
    //! Returns the size of the subtree
    size_type verify_node(const node* n, key_type* minkey, key_type* maxkey) const {
        TLX_BTREE_PRINT("verifynode " << n);

        if (n->is_leafnode()) {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);

            tlx_die_unless(leaf == root_ || !leaf->is_underflow());
            tlx_die_unless(leaf->slotuse > 0);

            for (NumSlotType slot = 0; slot < leaf->slotuse - 1; ++slot) {
                tlx_die_unless(key_lessequal(leaf->key(slot), leaf->key(slot + 1)));
            }

            *minkey = leaf->key(0);
            *maxkey = leaf->key(leaf->slotuse - 1);
            return leaf->slotuse;
        } else // !n->is_leafnode()
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);

            tlx_die_unless(inner == root_ || !inner->is_underflow());
            tlx_die_unless(inner->slotuse > 0);

            for (NumSlotType slot = 0; slot < inner->slotuse - 1; ++slot) {
                tlx_die_unless(
                    key_lessequal(inner->key(slot), inner->key(slot + 1)));
            }

            size_type subtree_size = 0;

            for (NumSlotType slot = 0; slot <= inner->slotuse; ++slot) {
                const node* subnode = inner->childid[slot];
                key_type subminkey = key_type();
                key_type submaxkey = key_type();

                tlx_die_unless(subnode->level + 1 == inner->level);
                subtree_size += verify_node(subnode, &subminkey, &submaxkey);

                TLX_BTREE_PRINT("verify subnode " << subnode << ": " << subminkey
                                                  << " - " << submaxkey);

                if (slot == 0)
                    *minkey = subminkey;
                else
                    tlx_die_unless(
                        key_greaterequal(subminkey, inner->key(slot - 1)));

                if (slot == inner->slotuse)
                    *maxkey = submaxkey;
                else
                    tlx_die_unless(key_equal(inner->key(slot), submaxkey));

                if (inner->level == 1 && slot < inner->slotuse) {
                    // children are leaves and must be linked together in the
                    // correct order
                    const LeafNode* leafa =
                        static_cast<const LeafNode*>(inner->childid[slot]);
                    const LeafNode* leafb =
                        static_cast<const LeafNode*>(inner->childid[slot + 1]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
                if (inner->level == 2 && slot < inner->slotuse) {
                    // verify leaf links between the adjacent inner nodes
                    const InnerNode* parenta =
                        static_cast<const InnerNode*>(inner->childid[slot]);
                    const InnerNode* parentb =
                        static_cast<const InnerNode*>(inner->childid[slot + 1]);

                    const LeafNode* leafa = static_cast<const LeafNode*>(
                        parenta->childid[parenta->slotuse]);
                    const LeafNode* leafb =
                        static_cast<const LeafNode*>(parentb->childid[0]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
            }
            tlx_die_unless(inner->subtree_size == subtree_size);
            return subtree_size;
        }
    }

    //! Verify the double linked list of leaves.
    void verify_leaflinks() const {
        const LeafNode* n = head_leaf_;

        tlx_die_unless(n);
        tlx_die_unless(n->level == 0);
        tlx_die_unless(!n || n->prev_leaf == nullptr);

        size_type testcount = 0;

        while (n) {
            tlx_die_unless(n->level == 0);
            tlx_die_unless(n->slotuse > 0);

            for (NumSlotType slot = 0; slot < n->slotuse - 1; ++slot) {
                tlx_die_unless(key_lessequal(n->key(slot), n->key(slot + 1)));
            }

            testcount += n->slotuse;

            if (n->next_leaf) {
                tlx_die_unless(key_lessequal(n->key(n->slotuse - 1),
                                             n->next_leaf->key(0)));

                tlx_die_unless(n == n->next_leaf->prev_leaf);
            } else {
                LeafNode* curr = tail_leaf_;
                while (curr->prev_leaf) {
                    curr = curr->prev_leaf;
                }
                tlx_die_unless(tail_leaf_ == n);
            }

            n = n->next_leaf;
        }

        tlx_die_unless(testcount == size());
    }

    //! \}
};

//! \}
//! \}

} // namespace reservoir

#endif // !TLX_RESERVOIR_BTREE_HEADER

/******************************************************************************/
