/*******************************************************************************
 * tests/btree_test.cpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2007-2017 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2018-2019 Tobias Theuer <tobias.theuer@gmx.de>
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <reservoir/btree_map.hpp>
#include <reservoir/btree_multimap.hpp>
#include <reservoir/btree_multiset.hpp>
#include <reservoir/btree_set.hpp>

#include <tlx/die.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <set>
#include <vector>

#if RESERVOIR_MORE_TESTS
static const bool more_tests = true;
#else
static const bool more_tests = false;
#endif

//! Key Extractor Struct
template <typename key_type, typename value_type>
struct key_of_value {
    //! pull first out of pair
    static const key_type& get(const value_type& v) {
        return v;
    }
};

/******************************************************************************/
// Instantiation Tests

template class reservoir::btree_set<unsigned int>;
template class reservoir::btree_map<int, double>;
template class reservoir::btree_multiset<int>;
template class reservoir::btree_multimap<int, int>;

/******************************************************************************/
// Simple Tests

template <int Slots>
struct SimpleTest {
    template <typename KeyType>
    struct traits_nodebug : reservoir::btree_default_traits<KeyType, KeyType> {
        static const bool self_verify = true;
        static const bool debug = false;

        static const int leaf_slots = Slots;
        static const int inner_slots = Slots;
    };

    static void test_empty() {
        typedef reservoir::btree_multiset<unsigned int, std::less<>,
                                          traits_nodebug<unsigned int>>
            btree_type;

        btree_type bt, bt2;
        bt.verify();

        die_unless(bt.erase(42) == false);

        die_unless(bt == bt2);
    }

    static void test_set_insert_erase_3200() {
        typedef reservoir::btree_multiset<unsigned int, std::less<>,
                                          traits_nodebug<unsigned int>>
            btree_type;

        btree_type bt;
        bt.verify();

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == i);
            bt.insert(rand() % 100);
            die_unless(bt.size() == i + 1);
        }

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == 3200 - i);
            die_unless(bt.erase_one(rand() % 100));
            die_unless(bt.size() == 3200 - i - 1);
        }

        die_unless(bt.empty());
    }

    static void test_set_insert_erase_3200_descending() {
        typedef reservoir::btree_multiset<unsigned int, std::greater<>,
                                          traits_nodebug<unsigned int>>
            btree_type;

        btree_type bt;

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == i);
            bt.insert(rand() % 100);
            die_unless(bt.size() == i + 1);
        }

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == 3200 - i);
            die_unless(bt.erase_one(rand() % 100));
            die_unless(bt.size() == 3200 - i - 1);
        }

        die_unless(bt.empty());
    }

    static void test_map_insert_erase_3200() {
        typedef reservoir::btree_multimap<unsigned int, std::string, std::less<>,
                                          traits_nodebug<unsigned int>>
            btree_type;

        btree_type bt;

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == i);
            bt.insert2(rand() % 100, "101");
            die_unless(bt.size() == i + 1);
        }

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == 3200 - i);
            die_unless(bt.erase_one(rand() % 100));
            die_unless(bt.size() == 3200 - i - 1);
        }

        die_unless(bt.empty());
        bt.verify();
    }

    static void test_map_insert_erase_3200_descending() {
        typedef reservoir::btree_multimap<unsigned int, std::string, std::greater<>,
                                          traits_nodebug<unsigned int>>
            btree_type;

        btree_type bt;

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == i);
            bt.insert2(rand() % 100, "101");
            die_unless(bt.size() == i + 1);
        }

        srand(34234235);
        for (unsigned int i = 0; i < 3200; i++) {
            die_unless(bt.size() == 3200 - i);
            die_unless(bt.erase_one(rand() % 100));
            die_unless(bt.size() == 3200 - i - 1);
        }

        die_unless(bt.empty());
        bt.verify();
    }

    static void test2_map_insert_erase_strings() {
        typedef reservoir::btree_multimap<std::string, unsigned int, std::less<>,
                                          traits_nodebug<std::string>>
            btree_type;

        std::string letters = "abcdefghijklmnopqrstuvwxyz";

        btree_type bt;

        for (unsigned int a = 0; a < letters.size(); ++a) {
            for (unsigned int b = 0; b < letters.size(); ++b) {
                bt.insert2(std::string(1, letters[a]) + letters[b],
                           static_cast<unsigned int>(a * letters.size() + b));
            }
        }

        for (unsigned int b = 0; b < letters.size(); ++b) {
            for (unsigned int a = 0; a < letters.size(); ++a) {
                std::string key = std::string(1, letters[a]) + letters[b];

                die_unless(bt.find(key)->second == a * letters.size() + b);
                die_unless(bt.erase_one(key));
            }
        }

        die_unless(bt.empty());
        bt.verify();
    }

    static void test_set_100000_uint64() {
        reservoir::btree_map<uint64_t, uint8_t> bt;

        for (uint64_t i = 10; i < 100000; ++i) {
            uint64_t key = i % 1000;

            if (bt.find(key) == bt.end()) {
                bt.insert(std::make_pair(key, key % 100));
            }
        }

        die_unless(bt.size() == 1000);
    }

    static void test_multiset_100000_uint32() {
        reservoir::btree_multiset<uint32_t> bt;

        for (uint64_t i = 0; i < 100000; ++i) {
            uint32_t key = i % 1000;

            bt.insert(key);
        }

        die_unless(bt.size() == 100000);
    }

    static void test_multiset_split_10000() {
        using BTree =
            reservoir::btree_multiset<int, std::less<>, traits_nodebug<int>>;

        BTree bt;

        srand(2437624);
        const int size = 3200, maxv = 2000;
        for (int i = 0; i < size; i++) {
            bt.insert(rand() % maxv);
            die_unless(bt.size() == static_cast<size_t>(i + 1));
        }

        BTree copy(bt);

        // first, check that splitting off 0 elements works
        // split off 0 elements to the left
        BTree tr1, tr2;
        copy.splitAt(tr1, 0, tr2);
        die_unless(tr1.empty());
        die_unless(tr2.size() == size);
        tr1.verify();
        tr2.verify();
        copy = std::move(tr2);

        // split off 0 elements to the right
        copy.splitAt(tr1, size, tr2);
        die_unless(tr1.size() == size);
        die_unless(tr2.empty());
        tr1.verify();
        tr2.verify();
        copy = std::move(tr1);

        // split off 0 elements to the left by key
        copy.split(tr1, -1, tr2);
        die_unless(tr1.empty());
        die_unless(tr2.size() == size);
        tr1.verify();
        tr2.verify();
        copy = std::move(tr2);

        // split off 0 elements to the right by key
        copy.split(tr1, maxv, tr2);
        die_unless(tr1.size() == size);
        die_unless(tr2.empty());
        tr1.verify();
        tr2.verify();
        copy = std::move(tr1);

        for (unsigned int i = 0; i < 100; i++) {
            BTree tr1, tr2;
            size_t split;
            if (i % 2 == 0) {
                // split at key
                split = rand() % maxv;
                copy.split(tr1, static_cast<int>(split), tr2);
            } else {
                // split at number of elements
                split = rand() % size;
                copy.splitAt(tr1, split, tr2);
                copy.verify();
                tr1.verify();
                tr2.verify();
                die_unless(tr1.size() == split);
                split = static_cast<size_t>(*tr2.begin());
            }
            die_unless(copy.empty());
            copy.verify();
            tr1.verify();
            tr2.verify();
            die_unless(tr1.size() + tr2.size() == size);
            if (tr1.size() != 0 && tr2.size() != 0) {
                die_unless(*tr1.rbegin() <= *tr2.begin());
                die_unless(*tr1.rbegin() <= static_cast<int>(split) &&
                           *tr2.begin() >= static_cast<int>(split));

                auto iter1 = bt.begin(), iter2 = tr1.begin();
                for (size_t j = 0; j < tr1.size(); ++j) {
                    die_unless(*iter1 == *iter2);
                    ++iter1, ++iter2;
                }
                iter2 = tr2.begin();
                while (iter1 != bt.end()) {
                    die_unless(*iter1 == *iter2);
                    ++iter1, ++iter2;
                }
                die_unless(iter2 == tr2.end());
            } else if (tr1.size() != 0) {
                die_unless(tr1 == bt);
            } else {
                die_unless(tr2 == bt);
            }

            // re-join the trees
            tr1.join(tr2);
            die_unless(tr1.size() == size);
            die_unless(tr1 == bt);
            // restore copy
            copy = std::move(tr1);
        }
    }

    static void test_tree_rank_10000() {
        using BTree = reservoir::BTree<int, int, key_of_value<int, int>,
                                       std::less<>, traits_nodebug<int>, true>;
        BTree tr;
        srand(1);
        for (size_t i = 0; i < 10000; ++i) {
            tr.insert(rand() % 1000);
        }
        tr.verify();
        size_t i = 0;
        for (auto it = tr.begin(); it != tr.end(); ++it) {
            die_unless(tr.rank_of(it) == i);
            die_unless(tr.find_rank(i) == it);
            ++i;
        }
    }

    SimpleTest() {
        test_empty();
        test_set_insert_erase_3200();
        test_set_insert_erase_3200_descending();
        test_map_insert_erase_3200();
        test_map_insert_erase_3200_descending();
        test2_map_insert_erase_strings();
        test_set_100000_uint64();
        test_multiset_100000_uint32();
        test_multiset_split_10000();
        test_tree_rank_10000();
    }
};

void test_simple() {
    // test binary search on different slot sizes
    if (more_tests) {
        SimpleTest<8>();
        SimpleTest<9>();
        SimpleTest<10>();
        SimpleTest<11>();
        SimpleTest<12>();
        SimpleTest<13>();
        SimpleTest<14>();
        SimpleTest<15>();
    }
    SimpleTest<16>();
    if (more_tests) {
        SimpleTest<17>();
        SimpleTest<19>();
        SimpleTest<20>();
        SimpleTest<21>();
        SimpleTest<23>();
        SimpleTest<24>();
        SimpleTest<32>();
        SimpleTest<48>();
        SimpleTest<63>();
        SimpleTest<64>();
        SimpleTest<65>();
        SimpleTest<101>();
        SimpleTest<203>();
    }
}

/******************************************************************************/
// Large Test

template <typename KeyType>
struct traits_nodebug : reservoir::btree_default_traits<KeyType, KeyType> {
    static const bool self_verify = true;
    static const bool debug = false;

    static const int leaf_slots = 8;
    static const int inner_slots = 8;
};

void test_large_multiset(const unsigned int insnum, const unsigned int modulo) {
    typedef reservoir::btree_multiset<unsigned int, std::less<>, traits_nodebug<unsigned int>>
        btree_type;

    btree_type bt;

    using multiset_type = std::multiset<unsigned int>;
    multiset_type set;

    // *** insert
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        die_unless(bt.size() == set.size());
        bt.insert(k);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    btree_type::iterator bi = bt.begin();
    multiset_type::const_iterator si = set.begin();
    for (; bi != bt.end() && si != set.end(); ++bi, ++si) {
        die_unless(*si == bi.key());
    }
    die_unless(bi == bt.end());
    die_unless(si == set.end());

    // *** existance
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        die_unless(bt.exists(k));
    }

    // *** counting
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** deletion
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        if (set.find(k) != set.end()) {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
            die_unless(std::equal(bt.begin(), bt.end(), set.begin()));
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

void test_large() {
    test_large_multiset(320, 1000);
    test_large_multiset(320, 10000);
    test_large_multiset(3200, 10);
    test_large_multiset(3200, 100);
    test_large_multiset(3200, 1000);
    test_large_multiset(3200, 10000);
    test_large_multiset(32000, 10000);
}

void test_large_sequence() {
    typedef reservoir::btree_multiset<unsigned int, std::less<>, traits_nodebug<unsigned int>>
        btree_type;

    btree_type bt;

    const unsigned int insnum = 10000;

    using multiset_type = std::multiset<unsigned int>;
    multiset_type set;

    // *** insert
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = i;

        die_unless(bt.size() == set.size());
        bt.insert(k);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    btree_type::iterator bi = bt.begin();
    multiset_type::const_iterator si = set.begin();
    for (; bi != bt.end() && si != set.end(); ++bi, ++si) {
        die_unless(*si == bi.key());
    }
    die_unless(bi == bt.end());
    die_unless(si == set.end());

    // *** existance
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = i;

        die_unless(bt.exists(k));
    }

    // *** counting
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = i;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** deletion
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = i;

        if (set.find(k) != set.end()) {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
            die_unless(std::equal(bt.begin(), bt.end(), set.begin()));
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

/******************************************************************************/
// Upper/Lower Bound Tests

void test_bounds_multimap(const unsigned int insnum, const unsigned int modulo) {
    typedef reservoir::btree_multimap<unsigned int, unsigned int, std::less<>,
                                      traits_nodebug<unsigned int>>
        btree_type;
    btree_type bt;

    using multiset_type = std::multiset<unsigned int>;
    multiset_type set;

    // *** insert
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;
        unsigned int v = 234;

        die_unless(bt.size() == set.size());
        bt.insert2(k, v);
        set.insert(k);
        die_unless(bt.count(k) == set.count(k));

        die_unless(bt.size() == set.size());
    }

    die_unless(bt.size() == insnum);

    // *** iterate
    {
        btree_type::iterator bi = bt.begin();
        multiset_type::const_iterator si = set.begin();
        for (; bi != bt.end() && si != set.end(); ++bi, ++si) {
            die_unless(*si == bi.key());
        }
        die_unless(bi == bt.end());
        die_unless(si == set.end());
    }

    // *** existance
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        die_unless(bt.exists(k));
    }

    // *** counting
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        die_unless(bt.count(k) == set.count(k));
    }

    // *** lower_bound
    for (unsigned int k = 0; k < modulo + 100; k++) {
        multiset_type::const_iterator si = set.lower_bound(k);
        btree_type::const_iterator bi = bt.lower_bound(k);

        if (bi == bt.end())
            die_unless(si == set.end());
        else if (si == set.end())
            die_unless(bi == bt.end());
        else
            die_unless(*si == bi.key());
    }

    // *** upper_bound
    for (unsigned int k = 0; k < modulo + 100; k++) {
        multiset_type::const_iterator si = set.upper_bound(k);
        btree_type::const_iterator bi = bt.upper_bound(k);

        if (bi == bt.end())
            die_unless(si == set.end());
        else if (si == set.end())
            die_unless(bi == bt.end());
        else
            die_unless(*si == bi.key());
    }

    // *** equal_range
    for (unsigned int k = 0; k < modulo + 100; k++) {
        std::pair<multiset_type::const_iterator, multiset_type::const_iterator> si =
            set.equal_range(k);
        std::pair<btree_type::const_iterator, btree_type::const_iterator> bi =
            bt.equal_range(k);

        if (bi.first == bt.end())
            die_unless(si.first == set.end());
        else if (si.first == set.end())
            die_unless(bi.first == bt.end());
        else
            die_unless(*si.first == bi.first.key());

        if (bi.second == bt.end())
            die_unless(si.second == set.end());
        else if (si.second == set.end())
            die_unless(bi.second == bt.end());
        else
            die_unless(*si.second == bi.second.key());
    }

    // *** deletion
    srand(34234235);
    for (unsigned int i = 0; i < insnum; i++) {
        unsigned int k = rand() % modulo;

        if (set.find(k) != set.end()) {
            die_unless(bt.size() == set.size());

            die_unless(bt.exists(k));
            die_unless(bt.erase_one(k));
            set.erase(set.find(k));

            die_unless(bt.size() == set.size());
        }
    }

    die_unless(bt.empty());
    die_unless(set.empty());
}

void test_bounds() {
    test_bounds_multimap(3200, 10);
    test_bounds_multimap(320, 1000);
}

/******************************************************************************/
// Test Iterators

void test_iterator1() {
    typedef reservoir::btree_multiset<unsigned int, std::less<>, traits_nodebug<unsigned int>>
        btree_type;

    std::vector<unsigned int> vector;

    srand(34234235);
    for (unsigned int i = 0; i < 3200; i++) {
        vector.push_back(rand() % 1000);
    }

    die_unless(vector.size() == 3200);

    // test construction and insert(iter, iter) function
    btree_type bt(vector.begin(), vector.end());

    die_unless(bt.size() == 3200);

    // copy for later use
    btree_type bt2 = bt;

    // empty out the first bt
    srand(34234235);
    for (unsigned int i = 0; i < 3200; i++) {
        die_unless(bt.size() == 3200 - i);
        die_unless(bt.erase_one(rand() % 1000));
        die_unless(bt.size() == 3200 - i - 1);
    }

    die_unless(bt.empty());

    // copy btree values back to a vector

    std::vector<unsigned int> vector2;
    vector2.assign(bt2.begin(), bt2.end());

    // afer sorting the vector, the two must be the same
    std::sort(vector.begin(), vector.end());

    die_unless(vector == vector2);

    // test reverse iterator
    vector2.clear();
    vector2.assign(bt2.rbegin(), bt2.rend());

    std::reverse(vector.begin(), vector.end());

    btree_type::reverse_iterator ri = bt2.rbegin();
    for (unsigned int i = 0; i < vector2.size(); ++i) {
        die_unless(vector[i] == vector2[i]);
        die_unless(vector[i] == *ri);

        ri++;
    }

    die_unless(ri == bt2.rend());
}

void test_iterator2() {
    typedef reservoir::btree_multimap<unsigned int, unsigned int, std::less<>,
                                      traits_nodebug<unsigned int>>
        btree_type;

    std::vector<btree_type::value_type> vector;

    srand(34234235);
    for (unsigned int i = 0; i < 3200; i++) {
        vector.emplace_back(rand() % 1000, 0);
    }

    die_unless(vector.size() == 3200);

    // test construction and insert(iter, iter) function
    btree_type bt(vector.begin(), vector.end());

    die_unless(bt.size() == 3200);

    // copy for later use
    btree_type bt2 = bt;

    // empty out the first bt
    srand(34234235);
    for (unsigned int i = 0; i < 3200; i++) {
        die_unless(bt.size() == 3200 - i);
        die_unless(bt.erase_one(rand() % 1000));
        die_unless(bt.size() == 3200 - i - 1);
    }

    die_unless(bt.empty());

    // copy btree values back to a vector

    std::vector<btree_type::value_type> vector2;
    vector2.assign(bt2.begin(), bt2.end());

    // afer sorting the vector, the two must be the same
    std::sort(vector.begin(), vector.end());

    die_unless(vector == vector2);

    // test reverse iterator
    vector2.clear();
    vector2.assign(bt2.rbegin(), bt2.rend());

    std::reverse(vector.begin(), vector.end());

    btree_type::reverse_iterator ri = bt2.rbegin();
    for (unsigned int i = 0; i < vector2.size(); ++i, ++ri) {
        die_unless(vector[i].first == vector2[i].first);
        die_unless(vector[i].first == ri->first);
        die_unless(vector[i].second == ri->second);
    }

    die_unless(ri == bt2.rend());
}

void test_iterator3() {
    typedef reservoir::btree_map<unsigned int, unsigned int, std::less<>,
                                 traits_nodebug<unsigned int>>
        btree_type;

    btree_type map;

    unsigned int maxnum = 1000;

    for (unsigned int i = 0; i < maxnum; ++i) {
        map.insert(std::make_pair(i, i * 3));
    }

    {
        // test iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = map.begin(); i != map.end(); ++i) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --map.end(); i != map.begin(); --i) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = map.begin(); i != map.end(); ++i) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --map.end(); i != map.begin(); --i) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = map.rbegin(); i != map.rend(); ++i) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); --i) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = map.rbegin();
             i != map.rend(); ++i) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); --i) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    // postfix

    {
        // test iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::iterator i = map.begin(); i != map.end(); i++) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --map.end(); i != map.begin(); i--) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = map.begin(); i != map.end(); i++) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --map.end(); i != map.begin(); i--) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        nownum--;

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = map.rbegin(); i != map.rend(); i++) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); i--) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = map.rbegin();
             i != map.rend(); i++) {
            nownum--;

            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --map.rend(); i != map.rbegin(); i--) {
            die_unless(nownum == i->first);
            die_unless(nownum * 3 == i->second);

            nownum++;
        }

        die_unless(nownum == i->first);
        die_unless(nownum * 3 == i->second);

        nownum++;

        die_unless(nownum == maxnum);
    }
}

void test_iterator4() {
    typedef reservoir::btree_set<unsigned int, std::less<>, traits_nodebug<unsigned int>> btree_type;

    btree_type set;

    unsigned int maxnum = 1000;

    for (unsigned int i = 0; i < maxnum; ++i) {
        set.insert(i);
    }

    {
        // test iterator prefix++
        unsigned int nownum = 0;

        for (reservoir::BTree<
                 unsigned int, unsigned int,
                 reservoir::btree_set<unsigned int, std::less<>, traits_nodebug<unsigned int>,
                                      std::allocator<unsigned int>>::key_of_value,
                 std::less<>, traits_nodebug<unsigned int>, false,
                 std::allocator<unsigned int>>::iterator::value_type& i : set) {
            die_unless(nownum == i);
            nownum++;
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --set.end(); i != set.begin(); --i) {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator prefix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = set.begin(); i != set.end(); ++i) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator prefix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --set.end(); i != set.begin(); --i) {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = set.rbegin(); i != set.rend(); ++i) {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); --i) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator prefix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = set.rbegin();
             i != set.rend(); ++i) {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator prefix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); --i) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    // postfix

    {
        // test iterator postfix++
        unsigned int nownum = 0;

        for (reservoir::BTree<
                 unsigned int, unsigned int,
                 reservoir::btree_set<unsigned int, std::less<>, traits_nodebug<unsigned int>,
                                      std::allocator<unsigned int>>::key_of_value,
                 std::less<>, traits_nodebug<unsigned int>, false,
                 std::allocator<unsigned int>>::iterator::value_type& i : set) {
            die_unless(nownum++ == i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::iterator i;
        for (i = --set.end(); i != set.begin(); i--) {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test const_iterator postfix++
        unsigned int nownum = 0;

        for (btree_type::const_iterator i = set.begin(); i != set.end(); i++) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum == maxnum);
    }

    {
        // test const_iterator postfix--
        unsigned int nownum = maxnum;

        btree_type::const_iterator i;
        for (i = --set.end(); i != set.begin(); i--) {
            die_unless(--nownum == *i);
        }

        die_unless(--nownum == *i);

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::reverse_iterator i = set.rbegin(); i != set.rend(); i++) {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); i--) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }

    {
        // test const_reverse_iterator postfix++
        unsigned int nownum = maxnum;

        for (btree_type::const_reverse_iterator i = set.rbegin();
             i != set.rend(); i++) {
            die_unless(--nownum == *i);
        }

        die_unless(nownum == 0);
    }

    {
        // test const_reverse_iterator postfix--
        unsigned int nownum = 0;

        btree_type::const_reverse_iterator i;
        for (i = --set.rend(); i != set.rbegin(); i--) {
            die_unless(nownum++ == *i);
        }

        die_unless(nownum++ == *i);

        die_unless(nownum == maxnum);
    }
}

void test_iterator5() {
    typedef reservoir::btree_set<unsigned int, std::less<>, traits_nodebug<unsigned int>> btree_type;

    btree_type set;

    unsigned int maxnum = 100;

    for (unsigned int i = 0; i < maxnum; ++i) {
        set.insert(i);
    }

    {
        btree_type::iterator it;

        it = set.begin();
        it--;
        die_unless(it == set.begin());

        it = set.begin();
        --it;
        die_unless(it == set.begin());

        it = set.end();
        it++;
        die_unless(it == set.end());

        it = set.end();
        ++it;
        die_unless(it == set.end());
    }

    {
        btree_type::const_iterator it;

        it = set.begin();
        it--;
        die_unless(it == set.begin());

        it = set.begin();
        --it;
        die_unless(it == set.begin());

        it = set.end();
        it++;
        die_unless(it == set.end());

        it = set.end();
        ++it;
        die_unless(it == set.end());
    }

    {
        btree_type::reverse_iterator it;

        it = set.rbegin();
        it--;
        die_unless(it == set.rbegin());

        it = set.rbegin();
        --it;
        die_unless(it == set.rbegin());

        it = set.rend();
        it++;
        die_unless(it == set.rend());

        it = set.rend();
        ++it;
        die_unless(it == set.rend());
    }

    {
        btree_type::const_reverse_iterator it;

        it = set.rbegin();
        it--;
        die_unless(it == set.rbegin());

        it = set.rbegin();
        --it;
        die_unless(it == set.rbegin());

        it = set.rend();
        it++;
        die_unless(it == set.rend());

        it = set.rend();
        ++it;
        die_unless(it == set.rend());
    }
}

void test_erase_iterator1() {
    typedef reservoir::btree_multimap<int, int, std::less<>, traits_nodebug<int>> btree_type;

    btree_type map;

    const int size1 = 32;
    const int size2 = 256;

    for (int i = 0; i < size1; ++i) {
        for (int j = 0; j < size2; ++j) {
            map.insert2(i, j);
        }
    }

    die_unless(map.size() == size1 * size2);

    // erase in reverse order. that should be the worst case for
    // erase_iter()

    for (int i = size1 - 1; i >= 0; --i) {
        for (int j = size2 - 1; j >= 0; --j) {
            // find iterator
            btree_type::iterator it = map.find(i);

            while (it != map.end() && it->first == i && it->second != j)
                ++it;

            die_unless(it->first == i);
            die_unless(it->second == j);

            size_t mapsize = map.size();
            map.erase(it);
            die_unless(map.size() == mapsize - 1);
        }
    }

    die_unless(map.size() == 0);
}

void test_iterators() {
    test_iterator1();
    test_iterator2();
    test_iterator3();
    test_iterator4();
    test_iterator5();
    test_erase_iterator1();
}

/******************************************************************************/
// Test with Structs

struct TestData {
    unsigned int a{0}, b{0};

    // required by the btree
    TestData() {}

    // also used as implicit conversion constructor
    inline TestData(unsigned int _a) : a(_a), b(0) {}
};

struct TestCompare {
    unsigned int somevalue;

    inline TestCompare(unsigned int sv) : somevalue(sv) {}

    bool operator()(const struct TestData& a, const struct TestData& b) const {
        return a.a > b.a;
    }
};

void test_struct() {
    typedef reservoir::btree_multiset<struct TestData, struct TestCompare,
                                      struct traits_nodebug<struct TestData>>
        btree_type;

    btree_type bt(TestCompare(42));

    srand(34234235);
    for (unsigned int i = 0; i < 320; i++) {
        die_unless(bt.size() == i);
        bt.insert(rand() % 100);
        die_unless(bt.size() == i + 1);
    }

    srand(34234235);
    for (unsigned int i = 0; i < 320; i++) {
        die_unless(bt.size() == 320 - i);
        die_unless(bt.erase_one(rand() % 100));
        die_unless(bt.size() == 320 - i - 1);
    }
}

/******************************************************************************/
// Test Relations

void test_relations() {
    typedef reservoir::btree_multiset<unsigned int, std::less<>, traits_nodebug<unsigned int>>
        btree_type;

    btree_type bt1, bt2;

    srand(34234236);
    for (unsigned int i = 0; i < 320; i++) {
        unsigned int key = rand() % 1000;

        bt1.insert(key);
        bt2.insert(key);
    }

    die_unless(bt1 == bt2);

    bt1.insert(499);
    bt2.insert(500);

    die_unless(bt1 != bt2);
    die_unless(bt1 < bt2);
    die_unless(!(bt1 > bt2));

    bt1.insert(500);
    bt2.insert(499);

    die_unless(bt1 == bt2);
    die_unless(bt1 <= bt2);

    // test assignment operator
    btree_type bt3;

    bt3 = bt1;
    die_unless(bt1 == bt3);
    die_unless(bt1 >= bt3);

    // test copy constructor
    btree_type bt4 = bt3;

    die_unless(bt1 == bt4);
}

/******************************************************************************/
// Test Bulk Load

void test_bulkload_set_instance(size_t numkeys, unsigned int mod) {
    typedef reservoir::btree_multiset<unsigned int, std::less<>, traits_nodebug<unsigned int>>
        btree_type;

    std::vector<unsigned int> keys(numkeys);

    srand(34234235);
    for (unsigned int i = 0; i < numkeys; i++) {
        keys[i] = rand() % mod;
    }

    std::sort(keys.begin(), keys.end());

    btree_type bt;
    bt.bulk_load(keys.begin(), keys.end());

    unsigned int i = 0;
    for (btree_type::iterator it = bt.begin(); it != bt.end(); ++it, ++i) {
        die_unless(*it == keys[i]);
    }
}

void test_bulkload_map_instance(size_t numkeys, unsigned int mod) {
    typedef reservoir::btree_multimap<int, std::string, std::less<>, traits_nodebug<int>> btree_type;

    std::vector<std::pair<int, std::string>> pairs(numkeys);

    srand(34234235);
    for (unsigned int i = 0; i < numkeys; i++) {
        pairs[i].first = rand() % mod;
        pairs[i].second = "key";
    }

    std::sort(pairs.begin(), pairs.end());

    btree_type bt;
    bt.bulk_load(pairs.begin(), pairs.end());

    unsigned int i = 0;
    for (btree_type::iterator it = bt.begin(); it != bt.end(); ++it, ++i) {
        die_unless(*it == pairs[i]);
    }
}

void test_bulkload() {
    for (size_t n = 6; n < 3200; ++n)
        test_bulkload_set_instance(n, 1000);

    test_bulkload_set_instance(31996, 10000);
    test_bulkload_set_instance(32000, 10000);
    test_bulkload_set_instance(117649, 100000);

    for (size_t n = 6; n < 3200; ++n)
        test_bulkload_map_instance(n, 1000);

    test_bulkload_map_instance(31996, 10000);
    test_bulkload_map_instance(32000, 10000);
    test_bulkload_map_instance(117649, 100000);
}

/******************************************************************************/

int main() {
    test_simple();
    if (more_tests) {
        test_large();
        test_large_sequence();
        test_bounds();
        test_iterators();
        test_struct();
        test_relations();
        test_bulkload();
    }

    return 0;
}

/******************************************************************************/
