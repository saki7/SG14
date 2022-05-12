// Copyright (c) 2022, Matthew Bentley (mattreecebentley@gmail.com) www.plflib.org
// Modified by Arthur O'Dwyer, 2022. Original source:
// https://github.com/mattreecebentley/plf_hive/blob/7b7763f/plf_hive.h

// zLib license (https://www.zlib.net/zlib_license.html):
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgement in the product documentation would be
//  appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#ifndef PLF_HIVE_H
#define PLF_HIVE_H

#ifndef PLF_HIVE_RANDOM_ACCESS_ITERATORS
 #define PLF_HIVE_RANDOM_ACCESS_ITERATORS 0
#endif

#if PLF_HIVE_RANDOM_ACCESS_ITERATORS
 #define PLF_HIVE_RELATIONAL_OPERATORS 1  // random access iterators require relational operators
#endif

#ifndef PLF_HIVE_RELATIONAL_OPERATORS
 #define PLF_HIVE_RELATIONAL_OPERATORS 0
#endif

#ifndef PLF_HIVE_DEBUGGING
 #define PLF_HIVE_DEBUGGING 0
#endif

#include <algorithm>
#if __has_include(<bit>)
#include <bit>
#endif
#include <cassert>
#if __has_include(<compare>)
#include <compare>
#endif
#if __has_include(<concepts>)
#include <concepts>
#endif
#include <cstddef>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#if __has_include(<ranges>)
#include <ranges>
#endif
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace plf {

// Polyfill std::type_identity_t
template<class T> struct hive_identity { using type = T; };
template<class T> using hive_identity_t = typename hive_identity<T>::type;

// Polyfill std::forward_iterator
#if __cpp_lib_ranges >= 201911
template<class It>
using hive_fwd_iterator = std::bool_constant<std::forward_iterator<It>>;
#else
template<class It>
using hive_fwd_iterator = std::is_base_of<std::forward_iterator_tag, typename std::iterator_traits<It>::iterator_category>;
#endif

template<class R>
struct hive_txn {
    R& rollback_;
    bool done_ = false;
    explicit hive_txn(R& rollback) : rollback_(rollback) {}
    ~hive_txn() { if (!done_) rollback_(); }
};

template<class F, class R>
static inline void hive_try_rollback(F&& task, R&& rollback) {
    hive_txn<R> txn(rollback);
    task();
    txn.done_ = true;
}

template<class F, class R>
static inline void hive_try_finally(F&& task, R&& finally) {
    hive_txn<R> txn(finally);
    task();
}

struct hive_limits {
    constexpr hive_limits(size_t mn, size_t mx) noexcept : min(mn), max(mx) {}
    size_t min, max;
};

namespace hive_priority {
    struct performance {
        using skipfield_type = unsigned short;
    };
    struct memory_use {
        using skipfield_type = unsigned char;
    };
}

template <class T, class allocator_type = std::allocator<T>, class priority = plf::hive_priority::performance>
class hive {
    template<bool IsConst> class hive_iterator;
    template<bool IsConst> class hive_reverse_iterator;
    friend class hive_iterator<false>;
    friend class hive_iterator<true>;

    using skipfield_type = typename priority::skipfield_type;
    using AllocTraits = std::allocator_traits<allocator_type>;

public:
    using value_type = T;
    using size_type = typename AllocTraits::size_type;
    using difference_type = typename AllocTraits::difference_type;
    using pointer = typename AllocTraits::pointer;
    using const_pointer = typename AllocTraits::const_pointer;
    using reference = T&;
    using const_reference = const T&;
    using iterator = hive_iterator<false>;
    using const_iterator = hive_iterator<true>;
    using reverse_iterator = hive_reverse_iterator<false>;
    using const_reverse_iterator = hive_reverse_iterator<true>;

private:
    inline auto make_value_callback(size_type, const T& value) {
        struct CB {
            const T& value_;
            void construct_and_increment(allocator_type& ea, AlignedEltPtr& p) {
                std::allocator_traits<allocator_type>::construct(ea, p[0].t(), value_);
                ++p;
            }
        };
        return CB{value};
    }

    template<class It, class Sent>
    inline auto make_itpair_callback(It& first, Sent&) {
        struct CB {
            It& first_;
            void construct_and_increment(allocator_type& ea, AlignedEltPtr& p) {
                std::allocator_traits<allocator_type>::construct(ea, p[0].t(), *first_);
                ++p;
                ++first_;
            }
        };
        return CB{first};
    }

    template<class U> using AllocOf = typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;
    template<class U> using PtrOf = typename std::allocator_traits<AllocOf<U>>::pointer;

    struct overaligned_elt {
        union {
            char dummy_;
            struct {
                skipfield_type nextlink_;
                skipfield_type prevlink_;
            };
            T t_;
        };
        PtrOf<T> t() { return bitcast_pointer<PtrOf<T>>(std::addressof(t_)); }
    };

    template <class D, class S>
    static constexpr D bitcast_pointer(S source_pointer) {
#if __cpp_lib_bit_cast >= 201806 && __cpp_lib_to_address >= 201711
        return std::bit_cast<D>(std::to_address(source_pointer));
#elif __cpp_lib_to_address >= 201711
        return reinterpret_cast<D>(std::to_address(source_pointer));
#else
        return reinterpret_cast<D>(source_pointer); // reject fancy pointer types
#endif
    }

    struct group;
    using AlignedEltPtr = PtrOf<overaligned_elt>;
    using GroupPtr = PtrOf<group>;
    using SkipfieldPtr = PtrOf<skipfield_type>;

    struct group {
        AlignedEltPtr last_endpoint;
            // The address which is one-past the highest cell number that's been used so far in this group - does not change via erasure
            // but may change via insertion/emplacement/assignment (if no previously-erased locations are available to insert to).
            // This variable is necessary because an iterator cannot access the hive's end_. It is probably the most-used variable
            // in general hive usage (being heavily used in operator ++, --), so is first in struct. If all cells in the group have
            // been inserted into at some point, it will be == reinterpret_cast<AlignedEltPtr>(skipfield).
        GroupPtr next_group = nullptr;
        GroupPtr prev_group = nullptr;
        skipfield_type free_list_head = std::numeric_limits<skipfield_type>::max();
            // The index of the last erased element in the group. The last erased element will, in turn, contain
            // the number of the index of the next erased element, and so on. If this is == maximum skipfield_type value
            // then free_list is empty ie. no erasures have occurred in the group.
        skipfield_type capacity;
        skipfield_type size = 0;
            // The total number of active elements in group - changes with insert and erase commands - used to check for empty group in erase function,
            // as an indication to remove the group. Also used in combination with capacity to check if group is full.
        GroupPtr next_erasure_ = nullptr;
            // The next group in the singly-linked list of groups with erasures ie. with active erased-element free lists. nullptr if no next group.
#if PLF_HIVE_RELATIONAL_OPERATORS
        size_type groupno_ = 0;
            // Used for comparison (> < >= <= <=>) iterator operators (used by distance function and user).
#endif

#if PLF_HIVE_RELATIONAL_OPERATORS
        inline size_t group_number() const { return groupno_; }
        inline void set_group_number(size_type x) { groupno_ = x; }
#else
        inline size_t group_number() const { return 42; }
        inline void set_group_number(size_type) { }
#endif

#if PLF_HIVE_DEBUGGING
        void debug_dump() const {
            printf(
                "  group #%zu [%zu/%zu used] (last_endpoint=%p, elts=%p, skipfield=%p, freelist=%zu, erasenext=%p)",
                group_number(), size_t(size), size_t(capacity),
                (void*)last_endpoint, (void*)addr_of_element(0), (void*)addr_of_skipfield(0), size_t(free_list_head), (void*)next_erasure_
            );
            if (next_group) {
                printf(" next: #%zu", next_group->group_number());
            } else {
                printf(" next: null");
            }
            if (prev_group) {
                printf(" prev: #%zu\n", prev_group->group_number());
            } else {
                printf(" prev: null\n");
            }
            printf("  skipfield[] =");
            for (int i = 0; i < capacity; ++i) {
                if (skipfield(i) == 0) {
                    printf(" _");
                } else {
                    printf(" %d", int(skipfield(i)));
                }
            }
            if (skipfield(capacity) == 0) {
                printf(" [_]\n");
            } else {
                printf(" [%d]\n", int(skipfield(capacity)));
            }
        }
#endif // PLF_HIVE_DEBUGGING

        explicit group(skipfield_type cap) :
            last_endpoint(addr_of_element(0)), capacity(cap)
        {
            std::fill_n(addr_of_skipfield(0), cap + 1, skipfield_type());
        }

        bool is_packed() const {
            return free_list_head == std::numeric_limits<skipfield_type>::max();
        }

        AlignedEltPtr addr_of_element(size_type n) { return GroupAllocHelper::elements(this) + n; }
        overaligned_elt& element(size_type n) { return GroupAllocHelper::elements(this)[n]; }
        AlignedEltPtr end_of_elements() { return GroupAllocHelper::elements(this) + capacity; }

        SkipfieldPtr addr_of_skipfield(size_type n) { return GroupAllocHelper::skipfield(this) + n; }
        skipfield_type& skipfield(size_type n) { return GroupAllocHelper::skipfield(this)[n]; }

        void reset(skipfield_type increment, GroupPtr next, GroupPtr prev, size_type groupno) {
            last_endpoint = addr_of_element(increment);
            next_group = next;
            free_list_head = std::numeric_limits<skipfield_type>::max();
            prev_group = prev;
            size = increment;
            next_erasure_ = nullptr;
            set_group_number(groupno);
            std::fill_n(addr_of_skipfield(0), capacity, skipfield_type());
        }
    };

    template <bool IsConst>
    class hive_iterator {
        GroupPtr group_ = GroupPtr();
        AlignedEltPtr elt_ = AlignedEltPtr();
        SkipfieldPtr skipf_ = SkipfieldPtr();

    public:
#if PLF_HIVE_DEBUGGING
        void debug_dump() const {
            printf("iterator(");
            if (group_) printf("#%zu", group_->group_number());
            else printf("null");
            printf(", %p, %p)\n", elt_, skipf_);
        }
#endif // PLF_HIVE_DEBUGGING

#if PLF_HIVE_RANDOM_ACCESS_ITERATORS
        using iterator_category = std::random_access_iterator_tag;
#else
        using iterator_category = std::bidirectional_iterator_tag;
#endif
        using value_type = typename hive::value_type;
        using difference_type = typename hive::difference_type;
        using pointer = std::conditional_t<IsConst, typename hive::const_pointer, typename hive::pointer>;
        using reference = std::conditional_t<IsConst, typename hive::const_reference, typename hive::reference>;

        friend class hive;
        friend class hive_reverse_iterator<false>;
        friend class hive_reverse_iterator<true>;

        explicit hive_iterator() = default;
        hive_iterator(hive_iterator&&) = default;
        hive_iterator(const hive_iterator&) = default;
        hive_iterator& operator=(hive_iterator&&) = default;
        hive_iterator& operator=(const hive_iterator&) = default;

        template<bool IsConst_ = IsConst, class = std::enable_if_t<IsConst_>>
        hive_iterator(const hive_iterator<false>& rhs) :
            group_(rhs.group_),
            elt_(rhs.elt_),
            skipf_(rhs.skipf_)
        {}

        template<bool IsConst_ = IsConst, class = std::enable_if_t<IsConst_>>
        hive_iterator(hive_iterator<false>&& rhs) :
            group_(std::move(rhs.group_)),
            elt_(std::move(rhs.elt_)),
            skipf_(std::move(rhs.skipf_))
        {}

        friend void swap(hive_iterator& a, hive_iterator& b) noexcept {
            using std::swap;
            swap(a.group_, b.group_);
            swap(a.elt_, b.elt_);
            swap(a.skipf_, b.skipf_);
        }

#if __cpp_impl_three_way_comparison >= 201907
        friend bool operator==(const hive_iterator& a, const hive_iterator& b) { return a.elt_ == b.elt_; }
#else
        friend bool operator==(const hive_iterator& a, const hive_iterator& b) { return a.elt_ == b.elt_; }
        friend bool operator!=(const hive_iterator& a, const hive_iterator& b) { return a.elt_ != b.elt_; }
#endif

#if PLF_HIVE_RELATIONAL_OPERATORS
#if __cpp_impl_three_way_comparison >= 201907
        friend std::strong_ordering operator<=>(const hive_iterator& a, const hive_iterator& b) {
            // TODO: what about fancy pointer types that don't support <=> natively?
            return a.group_ == b.group_ ?
                a.elt_ <=> b.elt_ :
                a.group_->groupno_ <=> b.group_->groupno_;
        }
#else
        friend bool operator<(const hive_iterator& a, const hive_iterator& b) {
            return a.group_ == b.group_ ?
                a.elt_ < b.elt_ :
                a.group_->groupno_ < b.group_->groupno_;
        }

        friend bool operator>(const hive_iterator& a, const hive_iterator& b) {
            return a.group_ == b.group_ ?
                a.elt_ > b.elt_ :
                a.group_->groupno_ > b.group_->groupno_;
        }

        friend bool operator<=(const hive_iterator& a, const hive_iterator& b) {
            return a.group_ == b.group_ ?
                a.elt_ <= b.elt_ :
                a.group_->groupno_ < b.group_->groupno_;
        }

        friend bool operator>=(const hive_iterator& a, const hive_iterator& b) {
            return a.group_ == b.group_ ?
                a.elt_ >= b.elt_ :
                a.group_->groupno_ > b.group_->groupno_;
        }
#endif
#endif

        inline reference operator*() const noexcept {
            return (*elt_).t_;
        }

        inline pointer operator->() const noexcept {
            return (*elt_).t();
        }

        hive_iterator& operator++() {
            assert(group_ != nullptr);
            ++skipf_;
            skipfield_type skip = skipf_[0];
            elt_ += static_cast<size_type>(skip) + 1u;
            if (elt_ == group_->last_endpoint && group_->next_group != nullptr) {
                group_ = group_->next_group;
                elt_ = group_->addr_of_element(group_->skipfield(0));
                skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
            } else {
                skipf_ += skip;
            }
            return *this;
        }

        hive_iterator& operator--() {
            assert(group_ != nullptr);
            if (elt_ != group_->addr_of_element(0)) {
                skipfield_type skip = skipf_[-1];
                if (index_in_group() != static_cast<difference_type>(skip)) {
                   skipf_ -= skip + 1u;
                   elt_ -= static_cast<size_type>(skip) + 1u;
                   return *this;
                }
            }
            group_ = group_->prev_group;
            skipfield_type skip = group_->skipfield(group_->capacity - 1);
            elt_ = group_->addr_of_element(group_->capacity - skip - 1);
            skipf_ = group_->addr_of_skipfield(group_->capacity - skip - 1);
            return *this;
        }

        inline hive_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
        inline hive_iterator operator--(int) { auto copy = *this; --*this; return copy; }

    private:
        template<bool IsConst_ = IsConst, class = std::enable_if_t<IsConst_>>
        hive_iterator<false> unconst() const {
            hive_iterator<false> it;
            it.group_ = group_;
            it.elt_ = elt_;
            it.skipf_ = skipf_;
            return it;
        }

        explicit hive_iterator(GroupPtr g, size_t skip) :
            group_(g), elt_(g->addr_of_element(skip)), skipf_(g->addr_of_skipfield(skip)) {}

        void advance_forward(difference_type n) {
            // Code explanation:
            // For the initial state of the iterator, we don't know which elements have been erased before that element in that group.
            // So for the first group, we follow the following logic:
            // 1. If no elements have been erased in the group, we do simple pointer addition to progress, either to within the group
            // (if the distance is small enough) or the end of the group and subtract from distance accordingly.
            // 2. If any of the first group's elements have been erased, we manually iterate, as we don't know whether
            // the erased elements occur before or after the initial iterator position, and we subtract 1 from the distance
            // amount each time we iterate. Iteration continues until either distance becomes zero, or we reach the end of the group.

            // For all subsequent groups, we follow this logic:
            // 1. If distance is larger than the total number of non-erased elements in a group, we skip that group and subtract
            //    the number of elements in that group from distance.
            // 2. If distance is smaller than the total number of non-erased elements in a group, then:
            //   a. If there are no erased elements in the group we simply add distance to group->elements to find the new location for the iterator.
            //   b. If there are erased elements in the group, we manually iterate and subtract 1 from distance on each iteration,
            //      until the new iterator location is found ie. distance = 0.

            // Note: incrementing elt_ is avoided until necessary to avoid needless calculations.

            // Check that we're not already at end()
            assert(!(elt_ == group_->last_endpoint && group_->next_group == nullptr));

            // Special case for initial element pointer and initial group (we don't know how far into the group the element pointer is)
            if (elt_ != group_->addr_of_element(group_->skipfield(0))) {
                // ie. != first non-erased element in group
                const difference_type distance_from_end = static_cast<difference_type>(group_->last_endpoint - elt_);

                if (group_->size == static_cast<skipfield_type>(distance_from_end)) {
                    if (n < distance_from_end) {
                        elt_ += n;
                        skipf_ += n;
                        return;
                    } else if (group_->next_group == nullptr) {
                        // either we've reached end() or gone beyond it, so bound to end()
                        elt_ = group_->last_endpoint;
                        skipf_ += distance_from_end;
                        return;
                    } else {
                        n -= distance_from_end;
                    }
                } else {
                    const SkipfieldPtr endpoint = skipf_ + distance_from_end;

                    while (true) {
                        ++skipf_;
                        skipf_ += skipf_[0];
                        --n;

                        if (skipf_ == endpoint) {
                            break;
                        } else if (n == 0) {
                            elt_ = group_->addr_of_element(skipf_ - group_->addr_of_skipfield(0));
                            return;
                        }
                    }

                    if (group_->next_group == nullptr) {
                        // either we've reached end() or gone beyond it, so bound to end()
                        elt_ = group_->last_endpoint;
                        return;
                    }
                }

                group_ = group_->next_group;

                if (n == 0) {
                    elt_ = group_->addr_of_element(group_->skipfield(0));
                    skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
                    return;
                }
            }

            // Intermediary groups - at the start of this code block and the subsequent block, the position of the iterator is assumed to be the first non-erased element in the current group:
            while (static_cast<difference_type>(group_->size) <= n) {
                if (group_->next_group == nullptr) {
                    elt_ = group_->last_endpoint;
                    skipf_ = group_->addr_of_skipfield(group_->last_endpoint - group_->addr_of_element(0));
                    return;
                } else {
                    n -= group_->size;
                    group_ = group_->next_group;
                    if (n == 0) {
                        elt_ = group_->addr_of_element(group_->skipfield(0));
                        skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
                        return;
                    }
                }
            }

            // Final group (if not already reached):
            if (group_->is_packed()) {
                // No erasures in this group, use straight pointer addition
                elt_ = group_->addr_of_element(n);
                skipf_ = group_->addr_of_skipfield(n);
            } else {
                // ie. size > n - safe to ignore endpoint check condition while incrementing:
                skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
                do {
                    ++skipf_;
                    skipf_ += skipf_[0];
                } while (--n != 0);
                elt_ = group_->addr_of_element(skipf_ - group_->addr_of_skipfield(0));
            }
        }

        void advance_backward(difference_type n) {
            assert(n < 0);
            assert(!((elt_ == group_->addr_of_element(group_->skipfield(0))) && group_->prev_group == nullptr)); // check that we're not already at begin()

            // Special case for initial element pointer and initial group (we don't know how far into the group the element pointer is)
            if (elt_ != group_->last_endpoint) {
                if (group_->is_packed()) {
                    difference_type distance_from_beginning = static_cast<difference_type>(group_->addr_of_element(0) - elt_);

                    if (n >= distance_from_beginning) {
                        elt_ += n;
                        skipf_ += n;
                        return;
                    } else if (group_->prev_group == nullptr) {
                        // ie. we've gone before begin(), so bound to begin()
                        elt_ = group_->addr_of_element(0);
                        skipf_ = group_->addr_of_skipfield(0);
                        return;
                    } else {
                        n -= distance_from_beginning;
                    }
                } else {
                    SkipfieldPtr beginning_point = group_->addr_of_skipfield(group_->skipfield(0));
                    while (skipf_ != beginning_point) {
                        --skipf_;
                        skipf_ -= skipf_[0];
                        if (++n == 0) {
                            elt_ = group_->addr_of_element(skipf_ - group_->addr_of_skipfield(0));
                            return;
                        }
                    }

                    if (group_->prev_group == nullptr) {
                        elt_ = group_->addr_of_element(group_->skipfield(0));
                        skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
                        return;
                    }
                }
                group_ = group_->prev_group;
            }

            // Intermediary groups - at the start of this code block and the subsequent block,
            // the position of the iterator is assumed to be either the first non-erased element in the next group over, or end():
            while (n < -static_cast<difference_type>(group_->size)) {
                if (group_->prev_group == nullptr) {
                    // we've gone beyond begin(), so bound to it
                    elt_ = group_->addr_of_element(group_->skipfield(0));
                    skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
                    return;
                }
                n += group_->size;
                group_ = group_->prev_group;
            }

            // Final group (if not already reached):
            if (n == -static_cast<difference_type>(group_->size)) {
                elt_ = group_->addr_of_element(group_->skipfield(0));
                skipf_ = group_->addr_of_skipfield(group_->skipfield(0));
            } else if (group_->is_packed()) {
                elt_ = group_->addr_of_element(group_->size + n);
                skipf_ = group_->addr_of_skipfield(group_->size + n);
            } else {
                skipf_ = group_->addr_of_skipfield(group_->capacity);
                do {
                    --skipf_;
                    skipf_ -= skipf_[0];
                } while (++n != 0);
                elt_ = group_->addr_of_element(skipf_ - group_->addr_of_skipfield(0));
            }
        }

        inline difference_type index_in_group() const { return skipf_ - group_->addr_of_skipfield(0); }

        difference_type distance_from_start_of_group() const {
            assert(group_ != nullptr);
            if (group_->is_packed() || skipf_ == group_->addr_of_skipfield(0)) {
                return skipf_ - group_->addr_of_skipfield(0);
            } else {
                difference_type count = 0;
                SkipfieldPtr endpoint = group_->addr_of_skipfield(group_->last_endpoint - group_->addr_of_element(0));
                for (SkipfieldPtr sp = skipf_; sp != endpoint; ++count) {
                    ++sp;
                    sp += sp[0];
                }
                return group_->size - count;
            }
        }

        difference_type distance_from_end_of_group() const {
            assert(group_ != nullptr);
            if (group_->is_packed() || skipf_ == group_->addr_of_skipfield(0)) {
                return group_->size - (skipf_ - group_->addr_of_skipfield(0));
            } else {
                difference_type count = 0;
                SkipfieldPtr endpoint = group_->addr_of_skipfield(group_->last_endpoint - group_->addr_of_element(0));
                for (SkipfieldPtr sp = skipf_; sp != endpoint; ++count) {
                    ++sp;
                    sp += sp[0];
                }
                return count;
            }
        }

        difference_type distance_forward(hive_iterator last) const {
            if (last.group_ != group_) {
                difference_type count = last.distance_from_start_of_group();
                for (GroupPtr g = last.group_->prev_group; g != group_; g = g->prev_group) {
                    count += g->size;
                }
                return count + this->distance_from_end_of_group();
            } else if (skipf_ == last.skipf_) {
                return 0;
            } else if (group_->is_packed()) {
                return last.skipf_ - skipf_;
            } else {
                difference_type count = 0;
                while (last.skipf_ != skipf_) {
                    --last.skipf_;
                    last.skipf_ -= last.skipf_[0];
                    ++count;
                }
                return count;
            }
        }

    public:
        inline void advance(difference_type n) {
            if (n > 0) {
                advance_forward(n);
            } else if (n < 0) {
                advance_backward(n);
            }
        }

        inline hive_iterator next(difference_type n) const {
            auto copy = *this;
            copy.advance(n);
            return copy;
        }

        inline hive_iterator prev(difference_type n) const {
            auto copy = *this;
            copy.advance(-n);
            return copy;
        }

        difference_type distance(hive_iterator last) const {
#if PLF_HIVE_RELATIONAL_OPERATORS
            if (last < *this) {
                return -last.distance_forward(*this);
            }
#endif
            return distance_forward(last);
        }

#if PLF_HIVE_RANDOM_ACCESS_ITERATORS
        friend hive_iterator& operator+=(hive_iterator& a, difference_type n) { a.advance(n); return a; }
        friend hive_iterator& operator-=(hive_iterator& a, difference_type n) { a.advance(-n); return a; }
        friend hive_iterator operator+(const hive_iterator& a, difference_type n) { return a.next(n); }
        friend hive_iterator operator+(difference_type n, const hive_iterator& a) { return a.next(n); }
        friend hive_iterator operator-(const hive_iterator& a, difference_type n) { return a.prev(n); }
        friend difference_type operator-(const hive_iterator& a, const hive_iterator& b) { return b.distance(a); }
        reference operator[](difference_type n) const { return *next(n); }
#endif
    }; // class hive_iterator

    template <bool IsConst>
    class hive_reverse_iterator {
        hive_iterator<IsConst> it_;

    public:
#if PLF_HIVE_RANDOM_ACCESS_ITERATORS
        using iterator_category = std::random_access_iterator_tag;
#else
        using iterator_category = std::bidirectional_iterator_tag;
#endif
        using value_type = typename hive::value_type;
        using difference_type = typename hive::difference_type;
        using pointer = std::conditional_t<IsConst, typename hive::const_pointer, typename hive::pointer>;
        using reference = std::conditional_t<IsConst, typename hive::const_reference, typename hive::reference>;

        hive_reverse_iterator() = default;
        hive_reverse_iterator(hive_reverse_iterator&&) = default;
        hive_reverse_iterator(const hive_reverse_iterator&) = default;
        hive_reverse_iterator& operator=(hive_reverse_iterator&&) = default;
        hive_reverse_iterator& operator=(const hive_reverse_iterator&) = default;

        template<bool IsConst_ = IsConst, class = std::enable_if_t<IsConst_>>
        hive_reverse_iterator(const hive_reverse_iterator<false>& rhs) :
            it_(rhs.base())
        {}

        explicit hive_reverse_iterator(hive_iterator<IsConst>&& rhs) : it_(std::move(rhs)) {}
        explicit hive_reverse_iterator(const hive_iterator<IsConst>& rhs) : it_(rhs) {}

#if __cpp_impl_three_way_comparison >= 201907
        friend bool operator==(const hive_reverse_iterator& a, const hive_reverse_iterator& b) = default;
#else
        friend bool operator==(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ == a.it_; }
        friend bool operator!=(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ != a.it_; }
#endif

#if PLF_HIVE_RELATIONAL_OPERATORS
#if __cpp_impl_three_way_comparison >= 201907
        friend std::strong_ordering operator<=>(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return (b.it_ <=> a.it_); }
#else
        friend bool operator<(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ < a.it_; }
        friend bool operator>(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ > a.it_; }
        friend bool operator<=(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ <= a.it_; }
        friend bool operator>=(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.it_ >= a.it_; }
#endif
#endif

        inline reference operator*() const noexcept { auto jt = it_; --jt; return *jt; }
        inline pointer operator->() const noexcept { auto jt = it_; --jt; return jt.operator->(); }
        hive_reverse_iterator& operator++() { --it_; return *this; }
        hive_reverse_iterator operator++(int) { auto copy = *this; --it_; return copy; }
        hive_reverse_iterator& operator--() { ++it_; return *this; }
        hive_reverse_iterator operator--(int) { auto copy = *this; ++it_; return copy; }

        hive_iterator<IsConst> base() const noexcept { return it_; }

        hive_reverse_iterator next(difference_type n) const {
            auto copy = *this;
            copy.it_.advance(-n);
            return copy;
        }

        hive_reverse_iterator prev(difference_type n) const {
            auto copy = *this;
            copy.it_.advance(n);
            return copy;
        }

        difference_type distance(const hive_reverse_iterator &last) const {
            return last.it_.distance(it_);
        }

        void advance(difference_type n) {
            it_.advance(-n);
        }

#if PLF_HIVE_RANDOM_ACCESS_ITERATORS
        friend hive_reverse_iterator& operator+=(hive_reverse_iterator& a, difference_type n) { a.advance(n); return a; }
        friend hive_reverse_iterator& operator-=(hive_reverse_iterator& a, difference_type n) { a.advance(-n); return a; }
        friend hive_reverse_iterator operator+(const hive_reverse_iterator& a, difference_type n) { return a.next(n); }
        friend hive_reverse_iterator operator+(difference_type n, const hive_reverse_iterator& a) { return a.next(n); }
        friend hive_reverse_iterator operator-(const hive_reverse_iterator& a, difference_type n) { return a.prev(n); }
        friend difference_type operator-(const hive_reverse_iterator& a, const hive_reverse_iterator& b) { return b.distance(a); }
        reference operator[](difference_type n) const { return *next(n); }
#endif
    }; // hive_reverse_iterator

public:
    void assert_invariants() const {
#if PLF_HIVE_DEBUGGING
        assert(size_ <= capacity_);
        assert(min_group_capacity_ <= max_group_capacity_);
        if (size_ == 0) {
            assert(begin_ == end_);
            if (capacity_ == 0) {  // TODO FIXME BUG HACK: this should be `if (true)`
                assert(begin_.group_ == nullptr);
                assert(begin_.elt_ == nullptr);
                assert(begin_.skipf_ == nullptr);
                assert(end_.group_ == nullptr);
                assert(end_.elt_ == nullptr);
                assert(end_.skipf_ == nullptr);
            }
            assert(groups_with_erasures_ == nullptr);
            if (capacity_ == 0) {
                assert(unused_groups_ == nullptr);
            } else {
                if (begin_.group_ == nullptr) {
                    assert(unused_groups_ != nullptr);  // the capacity must be somewhere
                }
            }
        } else {
            assert(begin_.group_ != nullptr);
            assert(begin_.elt_ != nullptr);
            assert(begin_.skipf_ != nullptr);
            assert(begin_.group_->prev_group == nullptr);
            assert(end_.group_ != nullptr);
            assert(end_.elt_ != nullptr);
            assert(end_.elt_ == end_.group_->last_endpoint);
            assert(end_.skipf_ != nullptr);
            assert(end_.group_->next_group == nullptr);
            assert(begin_ != end_);
            if (capacity_ == size_) {
                assert(unused_groups_ == nullptr);
            }
        }
        size_type total_size = 0;
        size_type total_cap = 0;
        for (GroupPtr g = begin_.group_; g != nullptr; g = g->next_group) {
            assert(min_group_capacity_ <= g->capacity);
            assert(g->capacity <= max_group_capacity_);
            // assert(g->size >= 1); // TODO FIXME BUG HACK
            assert(g->size <= g->capacity);
            total_size += g->size;
            total_cap += g->capacity;
            if (g->is_packed()) {
                assert(g->last_endpoint == g->addr_of_element(g->size));
            } else {
                assert(g->size < g->capacity);
                assert(g->last_endpoint > g->addr_of_element(g->size));
                assert(g->skipfield(g->free_list_head) != 0);
            }
            if (g->last_endpoint != g->end_of_elements()) {
                assert(g == end_.group_);
                assert(g->next_group == nullptr);
            }
            assert(g->skipfield(g->last_endpoint - g->addr_of_element(0)) == 0);
            if (g->size != g->capacity && g->next_group != nullptr) {
                assert(!g->is_packed());
            }
#if PLF_HIVE_RELATIONAL_OPERATORS
            if (g->next_group != nullptr) {
                assert(g->group_number() < g->next_group->group_number());
            }
#endif
            skipfield_type total_skipped = 0;
            for (skipfield_type sb = g->free_list_head; sb != std::numeric_limits<skipfield_type>::max(); sb = g->element(sb).nextlink_) {
                skipfield_type skipblock_length = g->skipfield(sb);
                assert(skipblock_length != 0);
                assert(g->skipfield(sb + skipblock_length - 1) == skipblock_length);
                total_skipped += skipblock_length;
                if (sb == g->free_list_head) {
                    assert(g->element(sb).prevlink_ == std::numeric_limits<skipfield_type>::max());
                }
                if (g->element(sb).nextlink_ != std::numeric_limits<skipfield_type>::max()) {
                    assert(g->element(g->element(sb).nextlink_).prevlink_ == sb);
                }
            }
            if (g == end_.group_) {
                assert(g->capacity == g->size + total_skipped + (g->end_of_elements() - g->last_endpoint));
            } else {
                assert(g->capacity == g->size + total_skipped);
            }
        }
        assert(total_size == size_);
        assert((unused_groups_ != nullptr) == (unused_groups_tail_ != nullptr));
        for (GroupPtr g = unused_groups_; g != nullptr; g = g->next_group) {
            assert(min_group_capacity_ <= g->capacity);
            assert(g->capacity <= max_group_capacity_);
            total_cap += g->capacity;
            if (g->next_group == nullptr) {
                assert(unused_groups_tail_ == g);
            }
        }
        assert(total_cap == capacity_);
        if (size_ == capacity_) {
            assert(groups_with_erasures_ == nullptr);
            assert(unused_groups_ == nullptr);
        } else {
            size_type space_in_last_group = end_.group_ != nullptr ? (end_.group_->end_of_elements() - end_.elt_) : 0;
            assert(size_ + space_in_last_group == capacity_ || groups_with_erasures_ != nullptr || unused_groups_ != nullptr);
        }
        for (GroupPtr g = groups_with_erasures_; g != nullptr; g = g->next_erasure_) {
            assert(g->size < g->capacity);
        }
#endif
    }

    void debug_dump() const {
#if PLF_HIVE_DEBUGGING
        printf(
            "hive [%zu/%zu used] (erase=%p, unused=%p, mincap=%zu, maxcap=%zu)\n",
            size_, capacity_,
            groups_with_erasures_, unused_groups_, size_t(min_group_capacity_), size_t(max_group_capacity_)
        );
        printf("  begin="); begin_.debug_dump();
        size_t total = 0;
        group *prev = nullptr;
        for (auto *g = begin_.group_; g != nullptr; g = g->next_group) {
            g->debug_dump();
            total += g->size;
            assert(g->prev_group == prev);
            prev = g;
        }
        assert(total == size_);
        printf("  end="); end_.debug_dump();
        if (end_.group_) {
            assert(end_.group_->next_group == nullptr);
        }
        printf("UNUSED GROUPS:\n");
        for (auto *g = unused_groups_; g != nullptr; g = g->next_group) {
            g->debug_dump();
            assert(g != begin_.group_);
            assert(g != end_.group_);
        }
        printf("GROUPS WITH ERASURES:");
        for (auto *g = groups_with_erasures_; g != nullptr; g = g->next_erasure_) {
            printf(" %zu", g->group_number());
        }
        printf("\n");
#endif // PLF_HIVE_DEBUGGING
    }

private:
    iterator end_;
    iterator begin_;
    GroupPtr groups_with_erasures_ = GroupPtr();
    GroupPtr unused_groups_ = GroupPtr();
    GroupPtr unused_groups_tail_ = GroupPtr();
    size_type size_ = 0;
    size_type capacity_ = 0;
    allocator_type allocator_;
    skipfield_type min_group_capacity_ = block_capacity_hard_limits().min;
    skipfield_type max_group_capacity_ = block_capacity_hard_limits().max;

    static inline void check_limits(plf::hive_limits soft) {
        auto hard = block_capacity_hard_limits();
        if (!(hard.min <= soft.min && soft.min <= soft.max && soft.max <= hard.max)) {
            throw std::length_error("Supplied limits are outside the allowable range");
        }
    }

    size_type trailing_capacity() const {
        if (end_.group_ == nullptr) {
            return 0;
        } else {
            return (end_.group_->capacity - end_.index_in_group());
        }
    }

public:
    hive() = default;

    explicit hive(plf::hive_limits limits) :
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
    }

    explicit hive(const allocator_type &alloc) : allocator_(alloc) {}

    hive(plf::hive_limits limits, const allocator_type &alloc) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
    }

    hive(const hive& source) :
        allocator_(std::allocator_traits<allocator_type>::select_on_container_copy_construction(source.allocator_)),
        min_group_capacity_(static_cast<skipfield_type>((source.min_group_capacity_ > source.size_) ? source.min_group_capacity_ : ((source.size_ > source.max_group_capacity_) ? source.max_group_capacity_ : source.size_))),
            // min group size is set to value closest to total number of elements in source hive, in order to not create
            // unnecessary small groups in the range-insert below, then reverts to the original min group size afterwards.
            // This effectively saves a call to reserve.
        max_group_capacity_(source.max_group_capacity_)
    {
        // can skip checking for skipfield conformance here as the skipfields must be equal between the destination and source,
        // and source will have already had theirs checked. Same applies for other copy and move constructors below
        reserve(source.size());
        range_assign_impl(source.begin(), source.end());
        min_group_capacity_ = source.min_group_capacity_;
    }

    hive(const hive& source, const hive_identity_t<allocator_type>& alloc) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>((source.min_group_capacity_ > source.size_) ? source.min_group_capacity_ : ((source.size_ > source.max_group_capacity_) ? source.max_group_capacity_ : source.size_))),
        max_group_capacity_(source.max_group_capacity_)
    {
        reserve(source.size());
        range_assign_impl(source.begin(), source.end());
        min_group_capacity_ = source.min_group_capacity_;
    }

private:
    inline void blank() {
        end_ = iterator();
        begin_ = iterator();
        groups_with_erasures_ = nullptr;
        unused_groups_ = nullptr;
        unused_groups_tail_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }

public:
    hive(hive&& source) noexcept :
        end_(std::move(source.end_)),
        begin_(std::move(source.begin_)),
        groups_with_erasures_(std::move(source.groups_with_erasures_)),
        unused_groups_(std::move(source.unused_groups_)),
        size_(source.size_),
        capacity_(source.capacity_),
        allocator_(source.get_allocator()),
        min_group_capacity_(source.min_group_capacity_),
        max_group_capacity_(source.max_group_capacity_)
    {
        assert(&source != this);
        source.blank();
    }

    hive(hive&& source, const hive_identity_t<allocator_type>& alloc):
        end_(std::move(source.end_)),
        begin_(std::move(source.begin_)),
        groups_with_erasures_(std::move(source.groups_with_erasures_)),
        unused_groups_(std::move(source.unused_groups_)),
        size_(source.size_),
        capacity_(source.capacity_),
        allocator_(alloc),
        min_group_capacity_(source.min_group_capacity_),
        max_group_capacity_(source.max_group_capacity_)
    {
        assert(&source != this);
        source.blank();
    }

    hive(size_type n, const T& value, const allocator_type &alloc = allocator_type()) :
        allocator_(alloc)
    {
        assign(n, value);
    }

    hive(size_type n, const T& value, plf::hive_limits limits, const allocator_type &alloc = allocator_type()) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
        assign(n, value);
    }

    explicit hive(size_type n) { assign(n, T()); }

    hive(size_type n, const allocator_type &alloc) :
        allocator_(alloc)
    {
        assign(n, T());
    }

    hive(size_type n, plf::hive_limits limits, const allocator_type &alloc = allocator_type()) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
        assign(n, T());
    }

    template<class It, class = std::enable_if_t<!std::is_integral<It>::value>>
    hive(It first, It last)
    {
        assign(std::move(first), std::move(last));
    }

    template<class It, class = std::enable_if_t<!std::is_integral<It>::value>>
    hive(It first, It last, const allocator_type &alloc) :
        allocator_(alloc)
    {
        assign(std::move(first), std::move(last));
    }

    template<class It, class = std::enable_if_t<!std::is_integral<It>::value>>
    hive(It first, It last, plf::hive_limits limits, const allocator_type &alloc = allocator_type()) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
        assign(std::move(first), std::move(last));
    }

    hive(std::initializer_list<T> il, const hive_identity_t<allocator_type>& alloc = allocator_type()) :
        allocator_(alloc)
    {
        assign(il.begin(), il.end());
    }

    hive(std::initializer_list<T> il, plf::hive_limits limits, const allocator_type &alloc = allocator_type()):
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
        assign(il.begin(), il.end());
    }

#if __cpp_lib_ranges >= 201911 && __cpp_lib_ranges_to_container >= 202202
    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, T>
    hive(std::from_range_t, R&& rg)
    {
        assign_range(std::forward<R>(rg));
    }

    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, T>
    hive(std::from_range_t, R&& rg, const allocator_type &alloc) :
        allocator_(alloc)
    {
        assign_range(std::forward<R>(rg));
    }

    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, T>
    explicit hive(std::from_range_t, R&& rg, plf::hive_limits limits, const allocator_type &alloc = allocator_type()) :
        allocator_(alloc),
        min_group_capacity_(static_cast<skipfield_type>(limits.min)),
        max_group_capacity_(static_cast<skipfield_type>(limits.max))
    {
        check_limits(limits);
        assign_range(std::forward<R>(rg));
    }
#endif

    ~hive() {
        assert_invariants();
        destroy_all_data();
    }

private:
    struct GroupAllocHelper {
        union U {
            group g_;
            overaligned_elt elt_;
        };
        struct type {
            alignas(U) char dummy;
        };

        static inline AlignedEltPtr elements(GroupPtr g) {
            auto p = PtrOf<char>(g);
            p += sizeof(U);
            return AlignedEltPtr(p);
        }

        static inline SkipfieldPtr skipfield(GroupPtr g) {
            return SkipfieldPtr(elements(g) + g->capacity);
        }

        static GroupPtr allocate_group(allocator_type a, size_t cap) {
            auto ta = AllocOf<type>(a);
            size_t bytes_for_group = sizeof(U);
            size_t bytes_for_elts = sizeof(overaligned_elt) * cap;
            size_t bytes_for_skipfield = sizeof(skipfield_type) * (cap + 1);
            size_t n = (bytes_for_group + bytes_for_elts + bytes_for_skipfield + sizeof(type) - 1) / sizeof(type);
            PtrOf<type> p = std::allocator_traits<AllocOf<type>>::allocate(ta, n);
            GroupPtr g = PtrOf<group>(p);
            ::new (bitcast_pointer<void*>(g)) group(cap);
            return g;
        }
        static void deallocate_group(allocator_type a, GroupPtr g) {
            size_t cap = g->capacity;
            auto ta = AllocOf<type>(a);
            size_t bytes_for_group = sizeof(U);
            size_t bytes_for_elts = sizeof(overaligned_elt) * cap;
            size_t bytes_for_skipfield = sizeof(skipfield_type) * (cap + 1);
            size_t n = (bytes_for_group + bytes_for_elts + bytes_for_skipfield + sizeof(type) - 1) / sizeof(type);
            std::allocator_traits<AllocOf<type>>::deallocate(ta, PtrOf<type>(g), n);
        }
    };

    void allocate_unused_group(size_type cap) {
        GroupPtr g = GroupAllocHelper::allocate_group(get_allocator(), cap);
        unused_groups_push_front(g);
        capacity_ += cap;
    }

    inline void deallocate_group(GroupPtr g) {
        GroupAllocHelper::deallocate_group(get_allocator(), g);
    }

    void destroy_all_data() {
        if (GroupPtr g = begin_.group_) {
            end_.group_->next_group = unused_groups_;

            if constexpr (!std::is_trivially_destructible<T>::value) {
                if (size_ != 0) {
                    while (true) {
                        // Erase elements without bothering to update skipfield - much faster:
                        const AlignedEltPtr end_pointer = g->last_endpoint;
                        do {
                            std::allocator_traits<allocator_type>::destroy(allocator_, begin_.elt_[0].t());
                            ++begin_.skipf_;
                            begin_.elt_ += static_cast<size_type>(begin_.skipf_[0]) + 1;
                            begin_.skipf_ += begin_.skipf_[0];
                        } while (begin_.elt_ != end_pointer); // ie. beyond end of available data

                        GroupPtr next = g->next_group;
                        deallocate_group(g);
                        g = next;

                        if (next == unused_groups_) {
                            break;
                        }
                        begin_.elt_ = next->addr_of_element(next->skipfield(0));
                        begin_.skipf_ = next->addr_of_skipfield(next->skipfield(0));
                    }
                }
            }

            while (g != nullptr) {
                GroupPtr next = g->next_group;
                deallocate_group(g);
                g = next;
            }
        }
    }

public:
    template<class... Args>
    iterator emplace(Args&&... args) {
        allocator_type ea = get_allocator();
        if (trailing_capacity() != 0) {
            iterator result = end_;
            GroupPtr g = result.group_;
            std::allocator_traits<allocator_type>::construct(ea, result.operator->(), static_cast<Args&&>(args)...);
            assert(end_.skipf_[0] == 0);
            ++end_.elt_;
            ++end_.skipf_;
            g->last_endpoint = end_.elt_;
            g->size += 1;
            ++size_;
            assert_invariants();
            return result;
        } else if (groups_with_erasures_ != nullptr) {
            GroupPtr g = groups_with_erasures_;
            skipfield_type sb = g->free_list_head;
            assert(sb < g->capacity);
            auto result = iterator(g, sb);
            skipfield_type nextsb = g->element(sb).nextlink_;
            assert(g->element(sb).prevlink_ == std::numeric_limits<skipfield_type>::max());
            hive_try_rollback([&]() {
                std::allocator_traits<allocator_type>::construct(ea, result.operator->(), static_cast<Args&&>(args)...);
            }, [&]() {
                g->element(sb).prevlink_ = std::numeric_limits<skipfield_type>::max();
                g->element(sb).nextlink_ = nextsb;
            });
            g->size += 1;
            size_ += 1;
            if (g == begin_.group_ && sb == 0) {
                begin_ = result;
            }
            skipfield_type old_skipblock_length = std::exchange(g->skipfield(sb), 0);
            assert(1 <= old_skipblock_length && old_skipblock_length <= g->capacity);
            skipfield_type new_skipblock_length = (old_skipblock_length - 1);
            if (new_skipblock_length == 0) {
                g->free_list_head = nextsb;
                if (nextsb == std::numeric_limits<skipfield_type>::max()) {
                    groups_with_erasures_ = g->next_erasure_;
                } else {
                    g->element(nextsb).prevlink_ = std::numeric_limits<skipfield_type>::max();
                }
            } else {
                g->skipfield(sb + 1) = new_skipblock_length;
                g->skipfield(sb + old_skipblock_length - 1) = new_skipblock_length;
                g->free_list_head = sb + 1;
                g->element(sb + 1).prevlink_ = std::numeric_limits<skipfield_type>::max();
                g->element(sb + 1).nextlink_ = nextsb;
                if (nextsb != std::numeric_limits<skipfield_type>::max()) {
                    g->element(nextsb).prevlink_ = sb + 1;
                }
            }
            assert_invariants();
            return result;
        } else {
            if (unused_groups_ == nullptr) {
                allocate_unused_group(recommend_block_size());
            }
            GroupPtr g = unused_groups_;
            std::allocator_traits<allocator_type>::construct(ea, g->element(0).t(), static_cast<Args&&>(args)...);
            (void)unused_groups_pop_front();
            std::fill_n(g->addr_of_skipfield(0), g->capacity, skipfield_type());
            g->size = 1;
            g->last_endpoint = g->addr_of_element(1);
            g->free_list_head = std::numeric_limits<skipfield_type>::max();
            g->next_group = nullptr;
            g->prev_group = end_.group_;
            auto result = iterator(g, 0);
            if (end_.group_ != nullptr) {
                end_.group_->next_group = g;
                g->set_group_number(end_.group_->group_number() + 1);
            } else {
                begin_ = result;
                g->set_group_number(0);
            }
            end_ = iterator(g, 1);
            ++size_;
            return result;
        }
    }

    inline iterator insert(const T& value) { return emplace(value); }
    inline iterator insert(T&& value) { return emplace(std::move(value)); }

    void insert(size_type n, const T& value) {
        if (n == 0) {
            // do nothing
        } else if (n == 1) {
            insert(value);
        } else {
            callback_insert_impl(n, make_value_callback(n, value));
        }
    }

    template<class It, std::enable_if_t<!std::is_integral<It>::value>* = nullptr>
    inline void insert(It first, It last) {
        range_insert_impl(std::move(first), std::move(last));
    }

#if __cpp_lib_ranges >= 201911
    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, T>
    inline void insert_range(R&& rg) {
        if constexpr (std::ranges::sized_range<R&>) {
            reserve(size() + std::ranges::size(rg));
        }
        range_insert_impl(std::ranges::begin(rg), std::ranges::end(rg));
    }
#endif

    inline void insert(std::initializer_list<T> il) {
        range_insert_impl(il.begin(), il.end());
    }

    iterator erase(const_iterator it) {
        assert(size_ != 0);
        assert(it.group_ != nullptr); // ie. not uninitialized iterator
        assert(it.elt_ != it.group_->last_endpoint); // ie. != end()
        assert(it.skipf_[0] == 0); // ie. element pointed to by iterator has not been erased previously

        if constexpr (!std::is_trivially_destructible<T>::value) {
            allocator_type ea = get_allocator();
            std::allocator_traits<allocator_type>::destroy(ea, it.operator->());
        }

        --size_;
        --(it.group_->size);

        if (it.group_->size != 0) {
            const char prev_skipfield = (it.skipf_[-(it.skipf_ != it.group_->addr_of_skipfield(0))] != 0);
            const char after_skipfield = (it.skipf_[1] != 0);
            skipfield_type update_value = 1;

            if (!(prev_skipfield | after_skipfield)) {
                // no consecutive erased elements
                it.skipf_[0] = 1; // solo skipped node
                const skipfield_type index = static_cast<skipfield_type>(it.index_in_group());

                if (it.group_->is_packed()) {
                    it.group_->next_erasure_ = std::exchange(groups_with_erasures_, it.group_);
                } else {
                    // ie. if this group already has some erased elements
                    it.group_->element(it.group_->free_list_head).prevlink_ = index;
                }

                it.elt_[0].nextlink_ = it.group_->free_list_head;
                it.elt_[0].prevlink_ = std::numeric_limits<skipfield_type>::max();
                it.group_->free_list_head = index;
            } else if (prev_skipfield & (!after_skipfield)) {
                // previous erased consecutive elements, none following
                it.skipf_[-it.skipf_[-1]] = it.skipf_[0] = static_cast<skipfield_type>(it.skipf_[-1] + 1);
            } else if ((!prev_skipfield) & after_skipfield) {
                // following erased consecutive elements, none preceding
                const skipfield_type following_value = static_cast<skipfield_type>(it.skipf_[1] + 1);
                it.skipf_[following_value - 1] = it.skipf_[0] = following_value;

                const skipfield_type following_previous = it.elt_[1].nextlink_;
                const skipfield_type following_next = it.elt_[1].prevlink_;
                it.elt_[0].nextlink_ = following_previous;
                it.elt_[0].prevlink_ = following_next;

                const skipfield_type index = static_cast<skipfield_type>(it.index_in_group());

                if (following_previous != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->element(following_previous).prevlink_ = index;
                }

                if (following_next != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->element(following_next).nextlink_ = index;
                } else {
                    it.group_->free_list_head = index;
                }
                update_value = following_value;
            } else {
                // both preceding and following consecutive erased elements - erased element is between two skipblocks
                const skipfield_type preceding_value = it.skipf_[-1];
                const skipfield_type following_value = it.skipf_[1] + 1;

                // Join the skipblocks
                it.skipf_[-preceding_value] = it.skipf_[following_value - 1] = static_cast<skipfield_type>(preceding_value + following_value);

                // Remove the following skipblock's entry from the free list
                const skipfield_type following_previous = it.elt_[1].nextlink_;
                const skipfield_type following_next = it.elt_[1].prevlink_;

                if (following_previous != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->element(following_previous).prevlink_ = following_next;
                }

                if (following_next != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->element(following_next).nextlink_ = following_previous;
                } else {
                    it.group_->free_list_head = following_previous;
                }
                update_value = following_value;
            }

            auto result = it.unconst();
            result.elt_ += update_value;
            result.skipf_ += update_value;

            if (result.elt_ == it.group_->last_endpoint && it.group_->next_group != nullptr) {
                result = iterator(it.group_->next_group, it.group_->next_group->skipfield(0));
            }

            if (it.elt_ == begin_.elt_) {
                begin_ = result;
            }
            // assert_invariants();
            return result;
        }

        // else: group is empty, consolidate groups
        const bool in_back_block = (it.group_->next_group == nullptr);
        const bool in_front_block = (it.group_ == begin_.group_);

        if (in_back_block & in_front_block) {
            // ie. only group in hive
            // Reset skipfield and free list rather than clearing - leads to fewer allocations/deallocations:
            reset_only_group_left(it.group_);
            assert_invariants();
            return end_;
        } else if ((!in_back_block) & in_front_block) {
            // ie. Remove first group, change first group to next group
            it.group_->next_group->prev_group = nullptr; // Cut off this group from the chain
            begin_.group_ = it.group_->next_group; // Make the next group the first group

            update_subsequent_group_numbers(begin_.group_);

            if (!it.group_->is_packed()) {
                // Erasures present within the group, ie. was part of the linked list of groups with erasures.
                remove_from_groups_with_erasures_list(it.group_);
            }

            capacity_ -= it.group_->capacity;
            deallocate_group(it.group_);

            // note: end iterator only needs to be changed if the deleted group was the final group in the chain ie. not in this case
            begin_.elt_ = begin_.group_->addr_of_element(begin_.group_->skipfield(0)); // If the beginning index has been erased (ie. skipfield != 0), skip to next non-erased element
            begin_.skipf_ = begin_.group_->addr_of_skipfield(begin_.group_->skipfield(0));

            assert_invariants();
            return begin_;
        } else if (!(in_back_block | in_front_block)) {
            // this is a non-first group but not final group in chain: delete the group, then link previous group to the next group in the chain:
            it.group_->next_group->prev_group = it.group_->prev_group;
            const GroupPtr g = it.group_->prev_group->next_group = it.group_->next_group; // close the chain, removing this group from it

            update_subsequent_group_numbers(g);

            if (!it.group_->is_packed()) {
                remove_from_groups_with_erasures_list(it.group_);
            }

            if (it.group_->next_group != end_.group_) {
                capacity_ -= it.group_->capacity;
                deallocate_group(it.group_);
            } else {
                unused_groups_push_front(it.group_);
            }

            // Return next group's first non-erased element:
            assert_invariants();
            return iterator(g, g->skipfield(0));
        } else {
            // this is a non-first group and the final group in the chain
            if (!it.group_->is_packed()) {
                remove_from_groups_with_erasures_list(it.group_);
            }
            it.group_->prev_group->next_group = nullptr;
            end_.group_ = it.group_->prev_group; // end iterator needs to be changed as element supplied was the back element of the hive
            end_.elt_ = end_.group_->end_of_elements();
            end_.skipf_ = end_.group_->addr_of_skipfield(end_.group_->capacity);
            unused_groups_push_front(it.group_);
            assert_invariants();
            return end_;
        }
    }

    iterator erase(const_iterator first, const_iterator last) {
        allocator_type ea = get_allocator();
        const_iterator current = first;
        if (current.group_ != last.group_) {
            if (current.elt_ != current.group_->addr_of_element(current.group_->skipfield(0))) {
                // if first is not the first non-erased element in its group - most common case
                size_type number_of_group_erasures = 0;

                const AlignedEltPtr end = first.group_->last_endpoint;

                // Schema: first erase all non-erased elements until end of group & remove all skipblocks post-first from the free_list.
                // Then, either update preceding skipblock or create new one:

                if (std::is_trivially_destructible<T>::value && current.group_->is_packed()) {
                    number_of_group_erasures += static_cast<size_type>(end - current.elt_);
                } else {
                    while (current.elt_ != end) {
                        if (current.skipf_[0] == 0) {
                            if constexpr (!std::is_trivially_destructible<T>::value) {
                                std::allocator_traits<allocator_type>::destroy(ea, current.operator->());
                            }
                            ++number_of_group_erasures;
                            ++current.elt_;
                            ++current.skipf_;
                        } else {
                            // remove skipblock from group:
                            const skipfield_type prev_free_list_index = current.elt_[0].nextlink_;
                            const skipfield_type next_free_list_index = current.elt_[0].prevlink_;

                            current.elt_ += current.skipf_[0];
                            current.skipf_ += current.skipf_[0];

                            if (next_free_list_index == std::numeric_limits<skipfield_type>::max() && prev_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                                // if this is the last skipblock in the free list
                                // remove group from list of free-list groups - will be added back in down below, but not worth optimizing for
                                remove_from_groups_with_erasures_list(first.group_);
                                first.group_->free_list_head = std::numeric_limits<skipfield_type>::max();
                                number_of_group_erasures += static_cast<size_type>(end - current.elt_);
                                if constexpr (!std::is_trivially_destructible<T>::value) {
                                    while (current.elt_ != end) {
                                        std::allocator_traits<allocator_type>::destroy(ea, current.operator->());
                                        ++current.elt_;
                                    }
                                }
                                break; // end overall while loop
                            } else if (next_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                                current.group_->free_list_head = prev_free_list_index; // make free list head equal to next free list node
                                current.group_->element(prev_free_list_index).prevlink_ = std::numeric_limits<skipfield_type>::max();
                            } else {
                                current.group_->element(next_free_list_index).nextlink_ = prev_free_list_index;
                                if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                                    current.group_->element(prev_free_list_index).prevlink_ = next_free_list_index;
                                }
                            }
                        }
                    }
                }

                const skipfield_type previous_node_value = first.skipf_[-1];
                const skipfield_type distance_to_end = static_cast<skipfield_type>(end - first.elt_);

                if (previous_node_value == 0) {
                    // no previous skipblock
                    first.skipf_[0] = distance_to_end; // set start node value
                    first.skipf_[distance_to_end - 1] = distance_to_end; // set end node value

                    const skipfield_type index = static_cast<skipfield_type>(first.elt_ - first.group_->addr_of_element(0));

                    if (first.group_->is_packed()) {
                        first.group_->next_erasure_ = std::exchange(groups_with_erasures_, first.group_);
                    } else {
                        first.group_->element(first.group_->free_list_head).prevlink_ = index;
                    }

                    first.elt_[0].nextlink_ = first.group_->free_list_head;
                    first.elt_[0].prevlink_ = std::numeric_limits<skipfield_type>::max();
                    first.group_->free_list_head = index;
                } else {
                    // update previous skipblock, no need to update free list:
                    first.skipf_[-previous_node_value] = first.skipf_[distance_to_end - 1] = static_cast<skipfield_type>(previous_node_value + distance_to_end);
                }
                first.group_->size = static_cast<skipfield_type>(first.group_->size - number_of_group_erasures);
                size_ -= number_of_group_erasures;
                current.group_ = current.group_->next_group;
            }

            // Intermediate groups:
            const GroupPtr prev = current.group_->prev_group;
            while (current.group_ != last.group_) {
                if constexpr (!std::is_trivially_destructible<T>::value) {
                    current.elt_ = current.group_->addr_of_element(current.group_->skipfield(0));
                    current.skipf_ = current.group_->addr_of_skipfield(current.group_->skipfield(0));
                    const AlignedEltPtr end = current.group_->last_endpoint;
                    do {
                        std::allocator_traits<allocator_type>::destroy(ea, current.operator->());
                        ++current.skipf_;
                        const skipfield_type skip = current.skipf_[0];
                        current.elt_ += static_cast<size_type>(skip) + 1u;
                        current.skipf_ += skip;
                    } while (current.elt_ != end);
                }
                if (!current.group_->is_packed()) {
                    remove_from_groups_with_erasures_list(current.group_);
                }
                size_ -= current.group_->size;
                const GroupPtr current_group = std::exchange(current.group_, current.group_->next_group);
                if (current_group != end_.group_ && current_group->next_group != end_.group_) {
                    capacity_ -= current_group->capacity;
                    deallocate_group(current_group);
                } else {
                    unused_groups_push_front(current_group);
                }
            }

            current.elt_ = current.group_->addr_of_element(current.group_->skipfield(0));
            current.skipf_ = current.group_->addr_of_skipfield(current.group_->skipfield(0));
            current.group_->prev_group = prev;

            if (prev != nullptr) {
                prev->next_group = current.group_;
            } else {
                // This line is included here primarily to avoid a secondary if statement within the if block below - it will not be needed in any other situation
                begin_ = last.unconst();
            }
        }

        if (current.elt_ == last.elt_) {
            // in case last was at beginning of its group - also covers empty range case (first == last)
            assert_invariants();
            return last.unconst();
        }

        // Final group:
        // Code explanation:
        // If not erasing entire final group, 1. Destruct elements (if non-trivial destructor) and add locations to group free list. 2. process skipfield.
        // If erasing entire group, 1. Destruct elements (if non-trivial destructor), 2. if no elements left in hive, reset the group 3. otherwise reset end_ and remove group from groups-with-erasures list (if free list of erasures present)

        if (last.elt_ != end_.elt_ || current.elt_ != current.group_->addr_of_element(current.group_->skipfield(0))) {
            // ie. not erasing entire group
            size_type number_of_group_erasures = 0;
            // Schema: first erased all non-erased elements until end of group & remove all skipblocks post-last from the free_list.
            // Then, either update preceding skipblock or create new one:

            const_iterator current_saved = current;

            if (std::is_trivially_destructible<T>::value && current.group_->is_packed()) {
                number_of_group_erasures += static_cast<size_type>(last.elt_ - current.elt_);
            } else {
                while (current.elt_ != last.elt_) {
                    if (current.skipf_[0] == 0) {
                        if constexpr (!std::is_trivially_destructible<T>::value) {
                            std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
                        }
                        ++number_of_group_erasures;
                        ++current.elt_;
                        ++current.skipf_;
                    } else {
                        const skipfield_type prev_free_list_index = current.elt_[0].nextlink_;
                        const skipfield_type next_free_list_index = current.elt_[0].prevlink_;

                        current.elt_ += current.skipf_[0];
                        current.skipf_ += current.skipf_[0];

                        if (next_free_list_index == std::numeric_limits<skipfield_type>::max() && prev_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                            // if this is the last skipblock in the free list
                            remove_from_groups_with_erasures_list(last.group_); // remove group from list of free-list groups - will be added back in down below, but not worth optimizing for
                            last.group_->free_list_head = std::numeric_limits<skipfield_type>::max();
                            number_of_group_erasures += static_cast<size_type>(last.elt_ - current.elt_);
                            if constexpr (!std::is_trivially_destructible<T>::value) {
                                while (current.elt_ != last.elt_) {
                                    std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
                                    ++current.elt_;
                                }
                            }
                            break; // end overall while loop
                        } else if (next_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                            // if this is the head of the free list
                            current.group_->free_list_head = prev_free_list_index;
                            current.group_->element(prev_free_list_index).prevlink_ = std::numeric_limits<skipfield_type>::max();
                        } else {
                            current.group_->element(next_free_list_index).nextlink_ = prev_free_list_index;
                            if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                                current.group_->element(prev_free_list_index).prevlink_ = next_free_list_index;
                            }
                        }
                    }
                }
            }

            const skipfield_type distance_to_last = static_cast<skipfield_type>(last.elt_ - current_saved.elt_);
            const skipfield_type index = static_cast<skipfield_type>(current_saved.elt_ - last.group_->addr_of_element(0));

            if (index == 0 || current_saved.skipf_[-1] == 0) {
                current_saved.skipf_[0] = distance_to_last;
                last.skipf_[-1] = distance_to_last;

                if (last.group_->is_packed()) {
                    last.group_->next_erasure_ = std::exchange(groups_with_erasures_, last.group_);
                } else {
                    last.group_->element(last.group_->free_list_head).prevlink_ = index;
                }

                current_saved.elt_[0].nextlink_ = last.group_->free_list_head;
                current_saved.elt_[0].prevlink_ = std::numeric_limits<skipfield_type>::max();
                last.group_->free_list_head = index;
            } else {
                // If iterator 1 & 2 are in same group, but iterator 1 was not at start of group, and previous skipfield node is an end node in a skipblock:
                // Just update existing skipblock, no need to create new free list node:
                const skipfield_type prev_node_value = current_saved.skipf_[-1];
                current_saved.skipf_[-prev_node_value] = static_cast<skipfield_type>(prev_node_value + distance_to_last);
                last.skipf_[-1] = static_cast<skipfield_type>(prev_node_value + distance_to_last);
            }

            if (first.elt_ == begin_.elt_) {
                begin_ = last.unconst();
            }
            last.group_->size = static_cast<skipfield_type>(last.group_->size - number_of_group_erasures);
            size_ -= number_of_group_erasures;
        } else {
            if constexpr (!std::is_trivially_destructible<T>::value) {
                while (current.elt_ != last.elt_) {
                    std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
                    ++current.skipf_;
                    current.elt_ += current.skipf_[0] + 1;
                    current.skipf_ += current.skipf_[0];
                }
            }

            size_ -= current.group_->size;
            if (size_ != 0) {
                // ie. either prev_group != nullptr or next_group != nullptr
                if (!current.group_->is_packed()) {
                    remove_from_groups_with_erasures_list(current.group_);
                }

                current.group_->prev_group->next_group = current.group_->next_group;

                if (current.group_ == end_.group_) {
                    GroupPtr g = current.group_->prev_group;
                    end_ = iterator(g, g->last_endpoint - g->addr_of_element(0));
                    unused_groups_push_front(current.group_);
                    return end_;
                } else if (current.group_ == begin_.group_) {
                    GroupPtr g = current.group_->next_group;
                    begin_ = iterator(g, g->skipfield(0));
                }

                if (current.group_->next_group != end_.group_) {
                    capacity_ -= current.group_->capacity;
                } else {
                    unused_groups_push_front(current.group_);
                    return last.unconst();
                }
            } else {
                // Reset skipfield and free list rather than clearing - leads to fewer allocations/deallocations:
                reset_only_group_left(current.group_);
                return end_;
            }

            deallocate_group(current.group_);
        }

        return last.unconst();
    }

    void swap(hive &source)
        noexcept(std::allocator_traits<allocator_type>::propagate_on_container_swap::value || std::allocator_traits<allocator_type>::is_always_equal::value)
    {
        using std::swap;
        swap(end_, source.end_);
        swap(begin_, source.begin_);
        swap(groups_with_erasures_, source.groups_with_erasures_);
        swap(unused_groups_, source.unused_groups_);
        swap(unused_groups_tail_, source.unused_groups_tail_);
        swap(size_, source.size_);
        swap(capacity_, source.capacity_);
        swap(min_group_capacity_, source.min_group_capacity_);
        swap(max_group_capacity_, source.max_group_capacity_);
        if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_swap::value && !std::allocator_traits<allocator_type>::is_always_equal::value) {
            swap(allocator_, source.allocator_);
        }
    }

    friend void swap(hive& a, hive& b) noexcept(noexcept(a.swap(b))) { a.swap(b); }

    void clear() noexcept {
        if (size_ != 0) {
            if constexpr (!std::is_trivially_destructible<T>::value) {
                allocator_type ea = get_allocator();
                for (iterator it = begin_; it != end_; ++it) {
                    std::allocator_traits<allocator_type>::destroy(ea, it.operator->());
                }
            }
            if (begin_.group_ != end_.group_) {
                // Move all other groups onto the unused_groups list
                end_.group_->next_group = unused_groups_;
                if (unused_groups_ == nullptr) {
                    unused_groups_tail_ = end_.group_;
                }
                unused_groups_ = begin_.group_->next_group;
            }
            reset_only_group_left(begin_.group_);
            groups_with_erasures_ = nullptr;
            size_ = 0;
        }
    }

    void splice(hive& source) {
        assert_invariants();
        source.assert_invariants();
        assert(&source != this);

        if (capacity_ + source.capacity_ > max_size()) {
            throw std::length_error("Result of splice would exceed max_size()");
        }

        if (source.min_group_capacity_ < min_group_capacity_ || source.max_group_capacity_ > max_group_capacity_) {
            for (GroupPtr it = source.begin_.group_; it != nullptr; it = it->next_group) {
                if (it->capacity < min_group_capacity_ || it->capacity > max_group_capacity_) {
                    throw std::length_error("Cannot splice: source hive contains blocks that do not match the block limits of the destination hive");
                }
            }
        }

        size_type trailing = trailing_capacity();
        if (trailing > source.trailing_capacity()) {
            source.splice(*this);
            swap(source);
        } else {
            if (source.groups_with_erasures_ != nullptr) {
                if (groups_with_erasures_ == nullptr) {
                    groups_with_erasures_ = source.groups_with_erasures_;
                } else {
                    GroupPtr tail = groups_with_erasures_;
                    while (tail->next_erasure_ != nullptr) {
                        tail = tail->next_erasure_;
                    }
                    tail->next_erasure_ = source.groups_with_erasures_;
                }
            }
            if (source.unused_groups_ != nullptr) {
                if (unused_groups_ == nullptr) {
                    unused_groups_ = std::exchange(source.unused_groups_, nullptr);
                } else {
                    unused_groups_tail_->next_group = std::exchange(source.unused_groups_, nullptr);
                }
                unused_groups_tail_ = std::exchange(source.unused_groups_tail_, nullptr);
            }
            if (trailing != 0 && source.begin_.group_ != nullptr) {
                GroupPtr g = end_.group_;
                size_type n = g->capacity - trailing;
                if (n > 0) {
                    assert(g->skipfield(n - 1) == 0);
                }
                g->skipfield(n) = trailing;
                g->skipfield(g->capacity - 1) = trailing;
                if (g->free_list_head == std::numeric_limits<skipfield_type>::max()) {
                    g->next_erasure_ = std::exchange(groups_with_erasures_, g);
                }
                g->element(n).nextlink_ = std::exchange(g->free_list_head, n);
                g->element(n).prevlink_ = std::numeric_limits<skipfield_type>::max();
                g->last_endpoint = g->end_of_elements();
            }

            if (source.begin_.group_ != nullptr) {
                source.begin_.group_->prev_group = end_.group_;
                if (end_.group_ != nullptr) {
                    end_.group_->next_group = source.begin_.group_;
#if PLF_HIVE_RELATIONAL_OPERATORS
                    size_type groupno = end_.group_->group_number();
                    for (GroupPtr g = source.begin_.group_; g != nullptr; g = g->next_group) {
                        g->set_group_number(++groupno);
                    }
#endif
                } else {
                    assert(begin_.group_ == nullptr);
                    begin_ = std::move(source.begin_);
                }
            }
            end_ = std::move(source.end_);
            size_ += std::exchange(source.size_, 0);
            capacity_ += std::exchange(source.capacity_, 0);

            source.begin_ = iterator();
            source.end_ = iterator();
            source.groups_with_erasures_ = nullptr;
        }

        assert_invariants();
        source.assert_invariants();
        assert(source.size() == 0u);
        assert(source.capacity() == 0u);
    }

    inline void splice(hive&& source) { this->splice(source); }

private:
    template<bool MightFillIt, class CB>
    void callback_fill_skipblock(skipfield_type n, CB cb, GroupPtr g) {
        assert(g == groups_with_erasures_);
        allocator_type ea = get_allocator();
        skipfield_type sb = g->free_list_head;
        AlignedEltPtr d_first = g->addr_of_element(sb);
        AlignedEltPtr d_last = d_first + n;
        skipfield_type nextsb = d_first[0].nextlink_;
        skipfield_type old_skipblock_length = g->skipfield(sb);
        assert(1 <= n && n <= old_skipblock_length);
        assert(g->skipfield(sb + old_skipblock_length - 1) == old_skipblock_length);
        AlignedEltPtr p = d_first;
        hive_try_finally([&]() {
            while (p != d_last) {
                cb.construct_and_increment(ea, p);
            }
        }, [&]() {
            skipfield_type nadded = p - d_first;
            g->size += nadded;
            size_ += nadded;
            if (nadded != 0 && d_first == begin_.group_->addr_of_element(0)) {
                begin_ = iterator(g, 0);
            }
            std::fill_n(g->addr_of_skipfield(sb), nadded, skipfield_type());
            skipfield_type new_skipblock_length = (old_skipblock_length - nadded);
            if (MightFillIt && new_skipblock_length == 0) {
                g->free_list_head = nextsb;
                if (nextsb == std::numeric_limits<skipfield_type>::max()) {
                    groups_with_erasures_ = g->next_erasure_;
                }
            } else {
                g->skipfield(sb + nadded) = new_skipblock_length;
                g->skipfield(sb + old_skipblock_length - 1) = new_skipblock_length;
                g->free_list_head = sb + nadded;
                g->element(sb + nadded).prevlink_ = std::numeric_limits<skipfield_type>::max();
                g->element(sb + nadded).nextlink_ = nextsb;
                if (nextsb != std::numeric_limits<skipfield_type>::max()) {
                    g->element(nextsb).prevlink_ = sb + nadded;
                }
            }
        });
    }

    template<class CB>
    void callback_fill_trailing_capacity(skipfield_type n, CB cb, GroupPtr g) {
        allocator_type ea = get_allocator();
        assert(g == end_.group_);
        assert(g->is_packed());
        assert(1 <= n && n <= g->capacity - g->size);
        assert(g->next_group == nullptr);
        AlignedEltPtr d_first = g->last_endpoint;
        AlignedEltPtr d_last = g->last_endpoint + n;
        AlignedEltPtr p = d_first;
        hive_try_finally([&]() {
            while (p != d_last) {
                cb.construct_and_increment(ea, p);
            }
        }, [&]() {
            skipfield_type nadded = p - d_first;
            g->last_endpoint = p;
            g->size += nadded;
            size_ += nadded;
            end_ = iterator(g, p - g->addr_of_element(0));
        });
    }

    template<class CB>
    void callback_fill_unused_group(skipfield_type n, CB cb, GroupPtr g) {
        allocator_type ea = get_allocator();
        assert(g == unused_groups_);
        assert(1 <= n && n <= g->capacity);
        AlignedEltPtr d_first = g->addr_of_element(0);
        AlignedEltPtr d_last = g->addr_of_element(n);
        AlignedEltPtr p = d_first;
        hive_try_finally([&]() {
            while (p != d_last) {
                cb.construct_and_increment(ea, p);
            }
        }, [&]() {
            skipfield_type nadded = p - d_first;
            if (nadded != 0) {
                (void)unused_groups_pop_front();
                std::fill_n(g->addr_of_skipfield(0), g->capacity, skipfield_type());
                g->free_list_head = std::numeric_limits<skipfield_type>::max();
                g->last_endpoint = p;
                g->size = nadded;
                size_ += nadded;
                g->next_group = nullptr;
                if (end_.group_ != nullptr) {
                    end_.group_->next_group = g;
                    g->prev_group = end_.group_;
                    g->set_group_number(end_.group_->group_number() + 1);
                } else {
                    g->prev_group = nullptr;
                    g->set_group_number(0);
                }
                end_ = iterator(g, nadded);
                if (begin_.group_ == nullptr) {
                    begin_ = iterator(g, 0);
                }
            } else {
                assert(g == unused_groups_);
                assert(g != end_.group_);
            }
        });
    }

    template<class CB>
    void callback_insert_impl(size_type n, CB cb) {
        reserve(size_ + n);
        assert_invariants();
        while (groups_with_erasures_ != nullptr) {
            GroupPtr g = groups_with_erasures_;
            assert(g->free_list_head != std::numeric_limits<skipfield_type>::max());
            skipfield_type skipblock_length = g->skipfield(g->free_list_head);
            if (skipblock_length >= n) {
                callback_fill_skipblock<false>(n, cb, g);
                assert_invariants();
                return;
            } else {
                callback_fill_skipblock<true>(skipblock_length, cb, g);
                n -= skipblock_length;
            }
        }
        assert_invariants();
        if (n != 0 && end_.group_ != nullptr) {
            GroupPtr g = end_.group_;
            assert(g->is_packed());
            size_type space = g->capacity - g->size;
            if (space >= n) {
                callback_fill_trailing_capacity(n, cb, g);
                assert_invariants();
                return;
            } else if (space != 0) {
                callback_fill_trailing_capacity(space, cb, g);
                n -= space;
            }
        }
        assert_invariants();
        while (n != 0) {
            GroupPtr g = unused_groups_;
            if (g->capacity >= n) {
                callback_fill_unused_group(n, cb, g);
                assert_invariants();
                return;
            } else {
                callback_fill_unused_group(g->capacity, cb, g);
                n -= g->capacity;
            }
        }
        assert_invariants();
    }

    template<class It, class Sent>
    inline void range_assign_impl(It first, Sent last) {
        if constexpr (!hive_fwd_iterator<It>::value) {
            clear();
            for ( ; first != last; ++first) {
                emplace(*first);
            }
        } else if (first == last) {
            clear();
        } else {
#if __cpp_lib_ranges >= 201911
            size_type n = std::ranges::distance(first, last);
#else
            size_type n = std::distance(first, last);
#endif
            clear();
            callback_insert_impl(n, make_itpair_callback(first, last));
            assert_invariants();
        }
    }

    template<class It, class Sent>
    void range_insert_impl(It first, Sent last) {
        if constexpr (!hive_fwd_iterator<It>::value) {
            for ( ; first != last; ++first) {
                emplace(*first);
            }
        } else if (first == last) {
            return;
        } else {
#if __cpp_lib_ranges >= 201911
            size_type n = std::ranges::distance(first, last);
#else
            size_type n = std::distance(first, last);
#endif
            callback_insert_impl(n, make_itpair_callback(first, last));
        }
    }

    inline void update_subsequent_group_numbers(GroupPtr g) {
#if PLF_HIVE_RELATIONAL_OPERATORS
        do {
            g->groupno_ -= 1;
            g = g->next_group;
        } while (g != nullptr);
#endif
        (void)g;
    }

    void remove_from_groups_with_erasures_list(GroupPtr g) {
        assert(groups_with_erasures_ != nullptr);
        if (g == groups_with_erasures_) {
            groups_with_erasures_ = groups_with_erasures_->next_erasure_;
        } else {
            GroupPtr prev = groups_with_erasures_;
            GroupPtr curr = groups_with_erasures_->next_erasure_;
            while (g != curr) {
                prev = curr;
                curr = curr->next_erasure_;
            }
            prev->next_erasure_ = curr->next_erasure_;
        }
    }

    inline void reset_only_group_left(GroupPtr g) {
        groups_with_erasures_ = nullptr;
        g->reset(0, nullptr, nullptr, 0);
        begin_ = iterator(g, 0);
        end_ = begin_;
    }

    inline void unused_groups_push_front(GroupPtr g) {
        g->next_group = std::exchange(unused_groups_, g);
        if (unused_groups_tail_ == nullptr) {
            unused_groups_tail_ = g;
        }
    }

    inline GroupPtr unused_groups_pop_front() {
        GroupPtr g = std::exchange(unused_groups_, unused_groups_->next_group);
        assert(g != nullptr);
        if (unused_groups_tail_ == g) {
            unused_groups_tail_ = nullptr;
        }
        return g;
    }

public:
    template <class It, class = std::enable_if_t<!std::is_integral<It>::value>>
    inline void assign(It first, It last) {
        range_assign_impl(std::move(first), std::move(last));
    }

#if __cpp_lib_ranges >= 201911
    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, T> &&
                 std::assignable_from<T&, std::ranges::range_reference_t<R>>
    inline void assign_range(R&& rg) {
        range_assign_impl(std::ranges::begin(rg), std::ranges::end(rg));
    }
#endif

    inline void assign(size_type n, const T& value) {
        clear();
        insert(n, value);
    }

    inline void assign(std::initializer_list<T> il) {
        range_assign_impl(il.begin(), il.end());
    }

    inline allocator_type get_allocator() const noexcept { return allocator_; }

    inline iterator begin() noexcept { return begin_; }
    inline const_iterator begin() const noexcept { return begin_; }
    inline iterator end() noexcept { return end_; }
    inline const_iterator end() const noexcept { return end_; }
    inline reverse_iterator rbegin() noexcept { return reverse_iterator(end_); }
    inline const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end_); }
    inline reverse_iterator rend() noexcept { return reverse_iterator(begin_); }
    inline const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin_); }

    inline const_iterator cbegin() const noexcept { return begin_; }
    inline const_iterator cend() const noexcept { return end_; }
    inline const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end_); }
    inline const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin_); }

    [[nodiscard]] inline bool empty() const noexcept { return size_ == 0; }
    inline size_type size() const noexcept { return size_; }
    inline size_type max_size() const noexcept { return std::allocator_traits<allocator_type>::max_size(get_allocator()); }
    inline size_type capacity() const noexcept { return capacity_; }

private:
    // get all elements contiguous in memory and shrink to fit, remove erasures and erasure free lists. Invalidates all iterators and pointers to elements.
    void consolidate() {
        hive temp(plf::hive_limits(min_group_capacity_, max_group_capacity_), get_allocator());
        temp.range_assign_impl(std::make_move_iterator(begin()), std::make_move_iterator(end()));
        this->swap(temp);
        temp.min_group_capacity_ = block_capacity_hard_limits().min;  // for the benefit of assert_invariants in the dtor
        temp.max_group_capacity_ = block_capacity_hard_limits().max;
    }

    inline size_type recommend_block_size() const {
        size_type r = size_;
        if (r < 8) r = 8;
        if (r < min_group_capacity_) r = min_group_capacity_;
        if (r > max_group_capacity_) r = max_group_capacity_;
        return r;
    }

public:
    void reshape(plf::hive_limits limits) {
        static_assert(std::is_move_constructible<T>::value, "");
        check_limits(limits);

        assert_invariants();

        min_group_capacity_ = static_cast<skipfield_type>(limits.min);
        max_group_capacity_ = static_cast<skipfield_type>(limits.max);
        for (GroupPtr g = begin_.group_; g != nullptr; g = g->next_group) {
            if (g->capacity < min_group_capacity_ || g->capacity > max_group_capacity_) {
                consolidate();
                assert_invariants();
                return;
            }
        }
    }

    inline plf::hive_limits block_capacity_limits() const noexcept {
        return plf::hive_limits(min_group_capacity_, max_group_capacity_);
    }

    static constexpr plf::hive_limits block_capacity_hard_limits() noexcept {
        return plf::hive_limits(3, std::numeric_limits<skipfield_type>::max());
    }

    hive& operator=(const hive& source) {
        if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_copy_assignment::value) {
            allocator_type source_allocator(source);
            if (!std::allocator_traits<allocator_type>::is_always_equal::value && get_allocator() != source.get_allocator()) {
                // Deallocate existing blocks as source allocator is not necessarily able to do so
                destroy_all_data();
                blank();
            }
            allocator_ = source.get_allocator();
        }
        range_assign_impl(source.begin(), source.end());
        return *this;
    }

    hive& operator=(hive&& source)
        noexcept(std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value || std::allocator_traits<allocator_type>::is_always_equal::value)
    {
        assert(&source != this);
        destroy_all_data();

        bool should_use_source_allocator = (
            std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value ||
            std::allocator_traits<allocator_type>::is_always_equal::value ||
            this->get_allocator() == source.get_allocator()
        );
        if (should_use_source_allocator) {
            constexpr bool can_just_memcpy = (
                std::is_trivially_copyable<allocator_type>::value &&
                std::is_trivial<GroupPtr>::value &&
                std::is_trivial<AlignedEltPtr>::value &&
                std::is_trivial<SkipfieldPtr>::value
            );
            if constexpr (can_just_memcpy) {
                std::memcpy(static_cast<void *>(this), &source, sizeof(hive));
            } else {
                end_ = std::move(source.end_);
                begin_ = std::move(source.begin_);
                groups_with_erasures_ = std::move(source.groups_with_erasures_);
                unused_groups_ = std::move(source.unused_groups_);
                unused_groups_tail_ = std::move(source.unused_groups_tail_);
                size_ = source.size_;
                capacity_ = source.capacity_;
                min_group_capacity_ = source.min_group_capacity_;
                max_group_capacity_ = source.max_group_capacity_;

                if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value) {
                    allocator_ = std::move(source.allocator_);
                }
            }
        } else {
            reserve(source.size());
            range_assign_impl(std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
            source.destroy_all_data();
        }

        source.blank();
        assert_invariants();
        return *this;
    }

    inline hive& operator=(std::initializer_list<T> il) {
        range_assign_impl(il.begin(), il.end());
        assert_invariants();
        return *this;
    }

    void shrink_to_fit() {
        static_assert(std::is_move_constructible<T>::value, "");
        if (size_ == 0) {
            destroy_all_data();
            blank();
        } else if (size_ != capacity_) {
            consolidate();
        }
        assert_invariants();
    }

    void trim_capacity() noexcept {
        if (size_ == 0) {
            destroy_all_data();
            blank();
        } else {
            for (GroupPtr g = unused_groups_; g != nullptr; ) {
                GroupPtr next = g->next_group;
                capacity_ -= g->capacity;
                deallocate_group(g);
                g = next;
            }
            unused_groups_ = nullptr;
            unused_groups_tail_ = nullptr;
        }
        assert_invariants();
    }

    void reserve(size_type n) {
        if (n <= capacity_) {
            return;
        } else if (n > max_size()) {
            throw std::length_error("n must be at most max_size()");
        }
        size_type needed = n - capacity_;
        while (needed >= max_group_capacity_) {
            allocate_unused_group(max_group_capacity_);
            needed -= max_group_capacity_;
        }
        if (needed != 0) {
            if (needed < min_group_capacity_) {
                needed = min_group_capacity_;
            }
            bool should_move_to_back_of_list = (unused_groups_ != nullptr && unused_groups_->capacity > needed);
            allocate_unused_group(needed);
            if (should_move_to_back_of_list) {
                GroupPtr g = unused_groups_pop_front();
                std::exchange(unused_groups_tail_, g)->next_group = g;
                g->next_group = nullptr;
            }
        }
        assert(capacity_ >= n);
        assert_invariants();
    }

private:
    struct item_index_tuple {
        pointer original_location;
        size_type original_index;

        explicit item_index_tuple(pointer _item, size_type _index) :
            original_location(_item),
            original_index(_index)
        {}
    };

public:
    template <class Comp>
    void sort(Comp less) {
        if (size_ <= 1) {
            return;
        }

        struct ItemT {
            T *ptr_;
            size_type idx_;
        };

        std::unique_ptr<ItemT[]> a = std::make_unique<ItemT[]>(size_);
        auto it = begin_;
        for (size_type i = 0; i < size_; ++i) {
            a[i] = ItemT{std::addressof(*it), i};
            ++it;
        }
        assert(it == end_);
        std::sort(a.get(), a.get() + size_, [&](const ItemT& a, const ItemT& b) { return less(*a.ptr_, *b.ptr_); });

        for (size_type i = 0; i < size_; ++i) {
            size_type src = a[i].idx_;
            size_type dest = i;
            if (src != dest) {
                T temp = std::move(*a[i].ptr_);
                do {
                    *a[dest].ptr_ = std::move(*a[src].ptr_);
                    dest = src;
                    src = a[dest].idx_;
                    a[dest].idx_ = dest;
                } while (src != i);
                *a[dest].ptr_ = std::move(temp);
            }
        }
        assert_invariants();
    }

    inline void sort() { sort(std::less<T>()); }

    template<class Comp>
    size_type unique(Comp eq) {
        size_type count = 0;
        auto end = cend();
        for (auto it = cbegin(); it != end; ) {
            auto previous = it++;
            if (it == end) {
                break;
            }
            if (eq(*it, *previous)) {
                auto orig = ++count;
                auto last = it;
                while (++last != end && eq(*last, *previous)) {
                    ++count;
                }
                if (count != orig) {
                    it = erase(it, last);
                } else {
                    it = erase(it);
                }
                end = cend();
            }
        }
        assert_invariants();
        return count;
    }

    inline size_type unique() { return unique(std::equal_to<T>()); }
}; // hive

} // namespace plf

namespace std {

    template<class T, class A, class P, class Pred>
    typename plf::hive<T, A, P>::size_type erase_if(plf::hive<T, A, P>& h, Pred pred) {
        typename plf::hive<T, A, P>::size_type count = 0;
        auto end = h.end();
        for (auto it = h.begin(); it != end; ++it) {
            if (pred(*it)) {
                auto orig = ++count;
                auto last = it;
                while (++last != end && pred(*last)) {
                    ++count;
                }
                if (count != orig) {
                    it = h.erase(it, last);
                } else {
                    it = h.erase(it);
                }
                end = h.end();
                if (it == end) {
                    break;
                }
            }
        }
        return count;
    }

    template<class T, class A, class P>
    inline typename plf::hive<T, A, P>::size_type erase(plf::hive<T, A, P>& h, const T& value) {
        return std::erase_if(h, [&](const T &x) { return x == value; });
    }
} // namespace std

#endif // PLF_HIVE_H
