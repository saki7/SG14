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
 #define PLF_HIVE_DEBUGGING 1
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
            const T& deref() { return value_; }
            void increment() { }
        };
        return CB{value};
    }

    template<class It, class Sent>
    inline auto make_itpair_callback(It& first, Sent&) {
        struct CB {
            It& first_;
            decltype(auto) deref() { return *first_; }
            void increment() { ++first_; }
        };
        return CB{first};
    }

    template<class U> using AllocOf = typename std::allocator_traits<allocator_type>::template rebind_alloc<U>;
    template<class U> using PtrOf = typename std::allocator_traits<AllocOf<U>>::pointer;

    struct overaligned_elt {
        union {
            char dummy_;
            skipfield_type links_[2];
            T t_;
        };
        PtrOf<T> t() { return bitcast_pointer<PtrOf<T>>(std::addressof(t_)); }
    };
    struct lcm_aligned {
        static constexpr size_t alignment = alignof(overaligned_elt);
        alignas(alignment) char a[alignment];

        static constexpr size_t roundup(size_t x) { return (x + alignment - 1) & ~(alignment - 1); }

        static constexpr size_t group_allocation_size(size_t cap) {
            size_t bytes_for_elements = roundup(cap * sizeof(overaligned_elt));
            size_t bytes_for_skipfields = roundup((cap + 1) * sizeof(skipfield_type));
            return (bytes_for_elements + bytes_for_skipfields) / alignment;
        }

        static constexpr size_t end_of_elements(size_t cap) {
            size_t bytes_for_elements = roundup(cap * sizeof(overaligned_elt));
            return bytes_for_elements / sizeof(overaligned_elt);
        }
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
    using group_allocator_type = AllocOf<group>;
    using aligned_allocator_type = AllocOf<lcm_aligned>;

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
        const AlignedEltPtr elements;      // Element storage.
        const SkipfieldPtr skipfield;
            // Skipfield storage. The element and skipfield arrays are allocated contiguously, in a single allocation,
            // hence the skipfield pointer also functions as a 'one-past-end' pointer for the elements array.
            // There will always be one additional skipfield node allocated compared to the number of elements.
            // This is to ensure a faster ++ iterator operation (fewer checks are required when this is present).
            // The extra node is unused and always zero, but checked, and not having it will result in out-of-bounds memory errors.
        GroupPtr previous_group;
        skipfield_type free_list_head = std::numeric_limits<skipfield_type>::max();
            // The index of the last erased element in the group. The last erased element will, in turn, contain
            // the number of the index of the next erased element, and so on. If this is == maximum skipfield_type value
            // then free_list is empty ie. no erasures have occurred in the group.
        const skipfield_type capacity;
            // The element capacity of this particular group - can also be calculated from reinterpret_cast<AlignedEltPtr>(group->skipfield) - group->elements,
            // however this space is effectively free due to struct padding and the sizeof(skipfield_type), and calculating it once is faster in benchmarking.
        skipfield_type size;
            // The total number of active elements in group - changes with insert and erase commands - used to check for empty group in erase function,
            // as an indication to remove the group. Also used in combination with capacity to check if group is full.
        GroupPtr next_erasure_;
            // The next group in the singly-linked list of groups with erasures ie. with active erased-element free lists. nullptr if no next group.
#if PLF_HIVE_RELATIONAL_OPERATORS
        size_type groupno_ = 0;
            // Used for comparison (> < >= <= <=>) iterator operators (used by distance function and user).
#endif

        // Group elements allocation explanation: memory has to be allocated as an aligned type in order to align with memory boundaries correctly
        // (as opposed to being allocated as char or uint_8). Unfortunately this makes combining the element memory block and the skipfield memory block
        // into one allocation (which increases performance) a little more tricky. Specifically it means in many cases the allocation will amass more
        // memory than is needed, particularly if the element type is large.

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
                (void*)last_endpoint, (void*)elements, (void*)skipfield, size_t(free_list_head), (void*)next_erasure_
            );
            if (next_group) {
                printf(" next: #%zu", next_group->group_number());
            } else {
                printf(" next: null");
            }
            if (previous_group) {
                printf(" prev: #%zu\n", previous_group->group_number());
            } else {
                printf(" prev: null\n");
            }
            printf("  skipfield[] =");
            for (int i = 0; i < capacity; ++i) {
                if (skipfield[i] == 0) {
                    printf(" _");
                } else {
                    printf(" %d", int(skipfield[i]));
                }
            }
            if (skipfield[capacity] == 0) {
                printf(" [_]\n");
            } else {
                printf(" [%d]\n", int(skipfield[capacity]));
            }
        }
#endif // PLF_HIVE_DEBUGGING

        explicit group(PtrOf<lcm_aligned> p, skipfield_type cap, GroupPtr prevg):
            last_endpoint(bitcast_pointer<AlignedEltPtr>(p)),
            elements(last_endpoint++),
            skipfield(bitcast_pointer<SkipfieldPtr>(elements + lcm_aligned::end_of_elements(cap))),
            previous_group(prevg),
            capacity(cap),
            size(1),
            next_erasure_(nullptr)
        {
            set_group_number(prevg == nullptr ? 0 : prevg->group_number() + 1);
            std::fill_n(skipfield, cap + 1, skipfield_type());
        }

        bool is_packed() const {
            return free_list_head == std::numeric_limits<skipfield_type>::max();
        }

        void reset(skipfield_type increment, GroupPtr next, GroupPtr prev, size_type groupno) {
            last_endpoint = elements + increment;
            next_group = next;
            free_list_head = std::numeric_limits<skipfield_type>::max();
            previous_group = prev;
            size = increment;
            next_erasure_ = nullptr;
            set_group_number(groupno);
            std::fill_n(skipfield, capacity, skipfield_type());
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
                elt_ = group_->elements + group_->skipfield[0];
                skipf_ = group_->skipfield + group_->skipfield[0];
            } else {
                skipf_ += skip;
            }
            return *this;
        }

        hive_iterator& operator--() {
            assert(group_ != nullptr);
            if (elt_ != group_->elements) {
                skipfield_type skip = skipf_[-1];
                if (index_in_group() != static_cast<difference_type>(skip)) {
                   skipf_ -= skip + 1u;
                   elt_ -= static_cast<size_type>(skip) + 1u;
                   return *this;
                }
            }
            group_ = group_->previous_group;
            skipfield_type skip = group_->skipfield[group_->capacity - 1];
            elt_ = bitcast_pointer<AlignedEltPtr>(group_->skipfield) - skip - 1;
            skipf_ = group_->skipfield + group_->capacity - skip - 1;
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
            group_(g), elt_(g->elements + skip), skipf_(g->skipfield + skip) {}

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
            if (elt_ != group_->elements + group_->skipfield[0]) {
                // ie. != first non-erased element in group
                const difference_type distance_from_end = static_cast<difference_type>(group_->last_endpoint - elt_);

                if (group_->size == static_cast<skipfield_type>(distance_from_end)) {
                    // ie. if there are no erasures in the group (using endpoint - elements_start to determine number of elements in group just in case this is the last group of the hive, in which case group->last_endpoint != group->elements + group->capacity)
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
                            elt_ = group_->elements + (skipf_ - group_->skipfield);
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
                    elt_ = group_->elements + group_->skipfield[0];
                    skipf_ = group_->skipfield + group_->skipfield[0];
                    return;
                }
            }

            // Intermediary groups - at the start of this code block and the subsequent block, the position of the iterator is assumed to be the first non-erased element in the current group:
            while (static_cast<difference_type>(group_->size) <= n) {
                if (group_->next_group == nullptr) {
                    // either we've reached end() or gone beyond it, so bound to end()
                    elt_ = group_->last_endpoint;
                    skipf_ = group_->skipfield + (group_->last_endpoint - group_->elements);
                    return;
                } else {
                    n -= group_->size;
                    group_ = group_->next_group;
                    if (n == 0) {
                        elt_ = group_->elements + group_->skipfield[0];
                        skipf_ = group_->skipfield + group_->skipfield[0];
                        return;
                    }
                }
            }

            // Final group (if not already reached):
            if (group_->is_packed()) {
                // No erasures in this group, use straight pointer addition
                elt_ = group_->elements + n;
                skipf_ = group_->skipfield + n;
            } else {
                // ie. size > n - safe to ignore endpoint check condition while incrementing:
                skipf_ = group_->skipfield + group_->skipfield[0];
                do {
                    ++skipf_;
                    skipf_ += skipf_[0];
                } while (--n != 0);
                elt_ = group_->elements + (skipf_ - group_->skipfield);
            }
        }

        void advance_backward(difference_type n) {
            assert(n < 0);
            assert(!((elt_ == group_->elements + group_->skipfield[0]) && group_->previous_group == nullptr)); // check that we're not already at begin()

            // Special case for initial element pointer and initial group (we don't know how far into the group the element pointer is)
            if (elt_ != group_->last_endpoint) {
                if (group_->is_packed()) {
                    difference_type distance_from_beginning = static_cast<difference_type>(group_->elements - elt_);

                    if (n >= distance_from_beginning) {
                        elt_ += n;
                        skipf_ += n;
                        return;
                    } else if (group_->previous_group == nullptr) {
                        // ie. we've gone before begin(), so bound to begin()
                        elt_ = group_->elements;
                        skipf_ = group_->skipfield;
                        return;
                    } else {
                        n -= distance_from_beginning;
                    }
                } else {
                    const SkipfieldPtr beginning_point = group_->skipfield + group_->skipfield[0];
                    while (skipf_ != beginning_point) {
                        --skipf_;
                        skipf_ -= skipf_[0];
                        if (++n == 0) {
                            elt_ = group_->elements + (skipf_ - group_->skipfield);
                            return;
                        }
                    }

                    if (group_->previous_group == nullptr) {
                        elt_ = group_->elements + group_->skipfield[0]; // This is first group, so bound to begin() (just in case final decrement took us before begin())
                        skipf_ = group_->skipfield + group_->skipfield[0];
                        return;
                    }
                }
                group_ = group_->previous_group;
            }

            // Intermediary groups - at the start of this code block and the subsequent block,
            // the position of the iterator is assumed to be either the first non-erased element in the next group over, or end():
            while (n < -static_cast<difference_type>(group_->size)) {
                if (group_->previous_group == nullptr) {
                    // we've gone beyond begin(), so bound to it
                    elt_ = group_->elements + group_->skipfield[0];
                    skipf_ = group_->skipfield + group_->skipfield[0];
                    return;
                }
                n += group_->size;
                group_ = group_->previous_group;
            }

            // Final group (if not already reached):
            if (n == -static_cast<difference_type>(group_->size)) {
                elt_ = group_->elements + group_->skipfield[0];
                skipf_ = group_->skipfield + group_->skipfield[0];
            } else if (group_->is_packed()) {
                elt_ = group_->elements + group_->size + n;
                skipf_ = group_->skipfield + group_->size + n;
            } else {
                skipf_ = group_->skipfield + group_->capacity;
                do {
                    --skipf_;
                    skipf_ -= skipf_[0];
                } while (++n != 0);
                elt_ = group_->elements + (skipf_ - group_->skipfield);
            }
        }

        inline difference_type index_in_group() const { return skipf_ - group_->skipfield; }

        difference_type distance_from_start_of_group() const {
            assert(group_ != nullptr);
            if (group_->is_packed() || skipf_ == group_->skipfield) {
                return skipf_ - group_->skipfield;
            } else {
                difference_type count = 0;
                SkipfieldPtr endpoint = &group_->skipfield[group_->last_endpoint - group_->elements];
                for (SkipfieldPtr sp = skipf_; sp != endpoint; ++count) {
                    ++sp;
                    sp += sp[0];
                }
                return group_->size - count;
            }
        }

        difference_type distance_from_end_of_group() const {
            assert(group_ != nullptr);
            if (group_->is_packed() || skipf_ == group_->skipfield) {
                return group_->size - (skipf_ - group_->skipfield);
            } else {
                difference_type count = 0;
                SkipfieldPtr endpoint = &group_->skipfield[group_->last_endpoint - group_->elements];
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
                for (GroupPtr g = last.group_->previous_group; g != group_; g = g->previous_group) {
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
#endif
    }; // hive_reverse_iterator

public:
    void assert_invariants() const {
#if PLF_HIVE_DEBUGGING
        assert(size_ <= capacity_);
        assert(min_group_capacity_ <= max_group_capacity_);
        if (size_ == 0) {
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
            assert(begin_.group_->previous_group == nullptr);
            assert(end_.group_ != nullptr);
            assert(end_.elt_ != nullptr);
            assert(end_.elt_ == end_.group_->last_endpoint);
            assert(end_.skipf_ != nullptr);
            assert(end_.group_->next_group == nullptr);
            if (capacity_ == size_) {
                assert(unused_groups_ == nullptr);
            }
        }
        size_type total_size = 0;
        size_type total_cap = 0;
        for (GroupPtr g = begin_.group_; g != nullptr; g = g->next_group) {
            assert(min_group_capacity_ <= g->capacity);
            assert(g->capacity <= max_group_capacity_);
            // assert(g->size >= 1); TODO FIXME BUG HACK
            assert(g->size <= g->capacity);
            total_size += g->size;
            total_cap += g->capacity;
            if (g->is_packed()) {
                assert(g->last_endpoint == g->elements + g->size);
            } else {
                assert(g->size < g->capacity);
                assert(g->last_endpoint > g->elements + g->size);
                assert(g->skipfield[g->free_list_head] != 0);
            }
#if PLF_HIVE_RELATIONAL_OPERATORS
            if (g->next_group != nullptr) {
                assert(g->group_number() < g->next_group->group_number());
            }
#endif
        }
        assert(total_size == size_);
        for (GroupPtr g = unused_groups_; g != nullptr; g = g->next_group) {
            assert(min_group_capacity_ <= g->capacity);
            assert(g->capacity <= max_group_capacity_);
            total_cap += g->capacity;
        }
        assert(total_cap == capacity_);
        if (size_ == capacity_) {
            assert(groups_with_erasures_ == nullptr);
        } else {
            // assert(groups_with_erasures_ != nullptr); TODO FIXME BUG HACK
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
        group *prevg = nullptr;
        for (auto *g = begin_.group_; g != nullptr; g = g->next_group) {
            g->debug_dump();
            total += g->size;
            assert(g->previous_group == prevg);
            prevg = g;
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
        // Head of the singly-linked list of groups which have erased-element memory locations available for re-use
    GroupPtr unused_groups_ = GroupPtr();
       // Head of singly-linked list of groups retained by erase()/clear() or created by reserve()
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
        end_.group_ = nullptr;
        end_.elt_ = nullptr;
        end_.skipf_ = nullptr;
        begin_.group_ = nullptr;
        begin_.elt_ = nullptr;
        begin_.skipf_ = nullptr;
        groups_with_erasures_ = nullptr;
        unused_groups_ = nullptr;
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
    template<std::ranges::range R>
    hive(std::from_range_t, R&& rg)
    {
        assign_range(std::forward<R>(rg));
    }

    template<std::ranges::range R>
    hive(std::from_range_t, R&& rg, const allocator_type &alloc) :
        allocator_(alloc)
    {
        assign_range(std::forward<R>(rg));
    }

    template<std::ranges::range R>
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
        destroy_all_data();
    }

private:
    GroupPtr allocate_group(skipfield_type cap, GroupPtr prevg) {
        auto ga = group_allocator_type(get_allocator());
        auto aa = aligned_allocator_type(get_allocator());
        GroupPtr g = std::allocator_traits<group_allocator_type>::allocate(ga, 1);
        PtrOf<lcm_aligned> p;
        hive_try_rollback([&]() {
            p = std::allocator_traits<aligned_allocator_type>::allocate(aa, lcm_aligned::group_allocation_size(cap));
        }, [&]() {
            std::allocator_traits<group_allocator_type>::deallocate(ga, g, 1);
        });
        return ::new (bitcast_pointer<void*>(g)) group(p, cap, prevg);
    }

    inline void deallocate_group(GroupPtr g) {
        auto p = PtrOf<lcm_aligned>(g->elements);
        auto ga = group_allocator_type(get_allocator());
        auto aa = aligned_allocator_type(get_allocator());
        std::allocator_traits<aligned_allocator_type>::deallocate(aa, p, lcm_aligned::group_allocation_size(g->capacity));
        std::allocator_traits<group_allocator_type>::deallocate(ga, g, 1);
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

                        GroupPtr next_group = g->next_group;
                        deallocate_group(g);
                        g = next_group;

                        if (next_group == unused_groups_) {
                            break;
                        }
                        begin_.elt_ = next_group->elements + next_group->skipfield[0];
                        begin_.skipf_ = next_group->skipfield + next_group->skipfield[0];
                    }
                }
            }

            while (g != nullptr) {
                GroupPtr next_group = g->next_group;
                deallocate_group(g);
                g = next_group;
            }
        }
    }

    void initialize(skipfield_type cap) {
        begin_ = iterator(allocate_group(cap, nullptr), 0);
        end_ = begin_;
        capacity_ = cap;
    }

    void update_skipblock(const iterator& new_location, skipfield_type prev_free_list_index) {
        const skipfield_type new_value = static_cast<skipfield_type>(new_location.skipf_[0] - 1);

        if (new_value != 0) {
            // ie. skipfield was not 1, ie. a single-node skipblock, with no additional nodes to update
            // set (new) start and (original) end of skipblock to new value:
            new_location.skipf_[new_value] = new_location.skipf_[1] = new_value;

            // transfer free list node to new start node:
            ++(groups_with_erasures_->free_list_head);

            if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                new_location.group_->elements[prev_free_list_index].links_[1] = groups_with_erasures_->free_list_head;
            }
            new_location.elt_[1].links_[0] = prev_free_list_index;
            new_location.elt_[1].links_[1] = std::numeric_limits<skipfield_type>::max();
        } else {
            groups_with_erasures_->free_list_head = prev_free_list_index;
            if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                // ie. not the last free list node
                new_location.group_->elements[prev_free_list_index].links_[1] = std::numeric_limits<skipfield_type>::max();
            } else {
                // remove this group from the list of groups with erasures
                groups_with_erasures_ = groups_with_erasures_->next_erasure_;
            }
        }

        new_location.skipf_[0] = 0;
        ++(new_location.group_->size);

        if (new_location.group_ == begin_.group_ && new_location.elt_ < begin_.elt_) {
            // ie. begin_ was moved forwards as the result of an erasure at some point, this erased element is before the current begin,
            // hence, set current begin iterator to this element
            begin_ = new_location;
        }
        ++size_;
    }

public:
    template<class... Args>
    iterator emplace(Args&&... args) {
        allocator_type ea = get_allocator();
        if (end_.elt_ != nullptr) {
            if (groups_with_erasures_ == nullptr) {
                if (end_.elt_ != bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield)) {
                    const iterator result = end_;
                    std::allocator_traits<allocator_type>::construct(ea, end_.elt_[0].t(), static_cast<Args&&>(args)...);
                    ++end_.elt_;
                    end_.group_->last_endpoint = end_.elt_;
                    ++(end_.group_->size);
                    ++end_.skipf_;
                    ++size_;
                    assert_invariants();
                    return result;
                }
                GroupPtr next_group;
                if (unused_groups_ == nullptr) {
                    const skipfield_type newcap = (size_ < static_cast<size_type>(max_group_capacity_)) ? static_cast<skipfield_type>(size_) : max_group_capacity_;
                    next_group = allocate_group(newcap, end_.group_);
                    hive_try_rollback([&]() {
                        std::allocator_traits<allocator_type>::construct(ea, next_group->elements[0].t(), static_cast<Args&&>(args)...);
                    }, [&]() {
                        deallocate_group(next_group);
                    });
                    capacity_ += newcap;
                } else {
                    next_group = unused_groups_;
                    std::allocator_traits<allocator_type>::construct(ea, next_group->elements[0].t(), static_cast<Args&&>(args)...);
                    unused_groups_ = next_group->next_group;
                    next_group->reset(1, nullptr, end_.group_, end_.group_->group_number() + 1);
                }

                end_.group_->next_group = next_group;
                end_.group_ = next_group;
                end_.elt_ = next_group->last_endpoint;
                end_.skipf_ = next_group->skipfield + 1;
                ++size_;

                assert_invariants();
                return iterator(next_group, 0);
            } else {
                auto new_location = iterator(groups_with_erasures_, groups_with_erasures_->free_list_head);

                skipfield_type prev_free_list_index = new_location.elt_[0].links_[0];
                std::allocator_traits<allocator_type>::construct(ea, new_location.elt_[0].t(), static_cast<Args&&>(args)...);
                update_skipblock(new_location, prev_free_list_index);

                assert_invariants();
                return new_location;
            }
        } else {
            initialize(min_group_capacity_);
            hive_try_rollback([&]() {
                std::allocator_traits<allocator_type>::construct(ea, end_.elt_[0].t(), static_cast<Args&&>(args)...);
            }, [&]() {
                destroy_all_data();  // TODO FIXME BUG HACK: why not leave the allocation be?
                blank();
            });
            ++end_.elt_;
            ++end_.skipf_;
            size_ = 1;
            assert_invariants();
            return begin_;
        }
    }

    inline iterator insert(const T& value) { return emplace(value); }
    inline iterator insert(T&& value) { return emplace(std::move(value)); }

    void insert(size_type n, const T& value) {
        if (n == 0) {
            // do nothing
        } else if (n == 1) {
            insert(value);
        } else if (size_ == 0) {
            assign(n, value);
        } else {
            callback_insert_impl(n, make_value_callback(n, value));
        }
    }

    template<class It, std::enable_if_t<!std::is_integral<It>::value>* = nullptr>
    inline void insert(It first, It last) {
        range_insert_impl(std::move(first), std::move(last));
    }

#if __cpp_lib_ranges >= 201911
    template<std::ranges::range R>
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
            std::allocator_traits<allocator_type>::destroy(ea, it.elt_[0].t());
        }

        --size_;

        if (it.group_->size-- != 1) {
            const char prev_skipfield = (it.skipf_[-(it.skipf_ != it.group_->skipfield)] != 0);
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
                    it.group_->elements[it.group_->free_list_head].links_[1] = index;
                }

                it.elt_[0].links_[0] = it.group_->free_list_head;
                it.elt_[0].links_[1] = std::numeric_limits<skipfield_type>::max();
                it.group_->free_list_head = index;
            } else if (prev_skipfield & (!after_skipfield)) {
                // previous erased consecutive elements, none following
                it.skipf_[-it.skipf_[-1]] = it.skipf_[0] = static_cast<skipfield_type>(it.skipf_[-1] + 1);
            } else if ((!prev_skipfield) & after_skipfield) {
                // following erased consecutive elements, none preceding
                const skipfield_type following_value = static_cast<skipfield_type>(it.skipf_[1] + 1);
                it.skipf_[following_value - 1] = it.skipf_[0] = following_value;

                const skipfield_type following_previous = it.elt_[1].links_[0];
                const skipfield_type following_next = it.elt_[1].links_[1];
                it.elt_[0].links_[0] = following_previous;
                it.elt_[0].links_[1] = following_next;

                const skipfield_type index = static_cast<skipfield_type>(it.index_in_group());

                if (following_previous != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->elements[following_previous].links_[1] = index;
                }

                if (following_next != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->elements[following_next].links_[0] = index;
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
                const skipfield_type following_previous = it.elt_[1].links_[0];
                const skipfield_type following_next = it.elt_[1].links_[1];

                if (following_previous != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->elements[following_previous].links_[1] = following_next;
                }

                if (following_next != std::numeric_limits<skipfield_type>::max()) {
                    it.group_->elements[following_next].links_[0] = following_previous;
                } else {
                    it.group_->free_list_head = following_previous;
                }
                update_value = following_value;
            }

            auto result = it.unconst();
            result.elt_ += update_value;
            result.skipf_ += update_value;

            if (result.elt_ == it.group_->last_endpoint && it.group_->next_group != nullptr) {
                result = iterator(it.group_->next_group, it.group_->next_group->skipfield[0]);
            }

            if (it.elt_ == begin_.elt_) {
                begin_ = result;
            }
            assert_invariants();
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
            it.group_->next_group->previous_group = nullptr; // Cut off this group from the chain
            begin_.group_ = it.group_->next_group; // Make the next group the first group

            update_subsequent_group_numbers(begin_.group_);

            if (!it.group_->is_packed()) {
                // Erasures present within the group, ie. was part of the linked list of groups with erasures.
                remove_from_groups_with_erasures_list(it.group_);
            }

            capacity_ -= it.group_->capacity;
            deallocate_group(it.group_);

            // note: end iterator only needs to be changed if the deleted group was the final group in the chain ie. not in this case
            begin_.elt_ = begin_.group_->elements + begin_.group_->skipfield[0]; // If the beginning index has been erased (ie. skipfield != 0), skip to next non-erased element
            begin_.skipf_ = begin_.group_->skipfield + begin_.group_->skipfield[0];

            assert_invariants();
            return begin_;
        } else if (!(in_back_block | in_front_block)) {
            // this is a non-first group but not final group in chain: delete the group, then link previous group to the next group in the chain:
            it.group_->next_group->previous_group = it.group_->previous_group;
            const GroupPtr g = it.group_->previous_group->next_group = it.group_->next_group; // close the chain, removing this group from it

            update_subsequent_group_numbers(g);

            if (!it.group_->is_packed()) {
                remove_from_groups_with_erasures_list(it.group_);
            }

            if (it.group_->next_group != end_.group_) {
                capacity_ -= it.group_->capacity;
                deallocate_group(it.group_);
            } else {
                add_group_to_unused_groups_list(it.group_);
            }

            // Return next group's first non-erased element:
            assert_invariants();
            return iterator(g, g->skipfield[0]);
        } else {
            // this is a non-first group and the final group in the chain
            if (!it.group_->is_packed()) {
                remove_from_groups_with_erasures_list(it.group_);
            }
            it.group_->previous_group->next_group = nullptr;
            end_.group_ = it.group_->previous_group; // end iterator needs to be changed as element supplied was the back element of the hive
            end_.elt_ = bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield);
            end_.skipf_ = end_.group_->skipfield + end_.group_->capacity;
            add_group_to_unused_groups_list(it.group_);
            assert_invariants();
            return end_;
        }
    }

    iterator erase(const_iterator first, const_iterator last) {
        allocator_type ea = get_allocator();
        const_iterator current = first;
        if (current.group_ != last.group_) {
            if (current.elt_ != current.group_->elements + current.group_->skipfield[0]) {
                // if first is not the first non-erased element in its group - most common case
                size_type number_of_group_erasures = 0;

                // Now update skipfield:
                const AlignedEltPtr end = first.group_->last_endpoint;

                // Schema: first erase all non-erased elements until end of group & remove all skipblocks post-first from the free_list. Then, either update preceding skipblock or create new one:

                if (std::is_trivially_destructible<T>::value && current.group_->is_packed()) {
                    number_of_group_erasures += static_cast<size_type>(end - current.elt_);
                } else {
                    while (current.elt_ != end) {
                        if (current.skipf_[0] == 0) {
                            if constexpr (!std::is_trivially_destructible<T>::value) {
                                std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
                            }
                            ++number_of_group_erasures;
                            ++current.elt_;
                            ++current.skipf_;
                        } else {
                            // remove skipblock from group:
                            const skipfield_type prev_free_list_index = current.elt_[0].links_[0];
                            const skipfield_type next_free_list_index = current.elt_[0].links_[1];

                            current.elt_ += current.skipf_[0];
                            current.skipf_ += current.skipf_[0];

                            if (next_free_list_index == std::numeric_limits<skipfield_type>::max() && prev_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                                // if this is the last skipblock in the free list
                                remove_from_groups_with_erasures_list(first.group_); // remove group from list of free-list groups - will be added back in down below, but not worth optimizing for
                                first.group_->free_list_head = std::numeric_limits<skipfield_type>::max();
                                number_of_group_erasures += static_cast<size_type>(end - current.elt_);
                                if constexpr (!std::is_trivially_destructible<T>::value) {
                                    while (current.elt_ != end) {
                                        std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
                                        ++current.elt_;
                                    }
                                }
                                break; // end overall while loop
                            } else if (next_free_list_index == std::numeric_limits<skipfield_type>::max()) {
                                current.group_->free_list_head = prev_free_list_index; // make free list head equal to next free list node
                                current.group_->elements[prev_free_list_index].links_[1] = std::numeric_limits<skipfield_type>::max();
                            } else {
                                current.group_->elements[next_free_list_index].links_[0] = prev_free_list_index;
                                if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                                    current.group_->elements[prev_free_list_index].links_[1] = next_free_list_index;
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

                    const skipfield_type index = static_cast<skipfield_type>(first.elt_ - first.group_->elements);

                    if (first.group_->is_packed()) {
                        first.group_->next_erasure_ = std::exchange(groups_with_erasures_, first.group_);
                    } else {
                        first.group_->elements[first.group_->free_list_head].links_[1] = index;
                    }

                    first.elt_[0].links_[0] = first.group_->free_list_head;
                    first.elt_[0].links_[1] = std::numeric_limits<skipfield_type>::max();
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
            const GroupPtr previous_group = current.group_->previous_group;
            while (current.group_ != last.group_) {
                if constexpr (!std::is_trivially_destructible<T>::value) {
                    current.elt_ = current.group_->elements + current.group_->skipfield[0];
                    current.skipf_ = current.group_->skipfield + current.group_->skipfield[0];
                    const AlignedEltPtr end = current.group_->last_endpoint;
                    do {
                        std::allocator_traits<allocator_type>::destroy(ea, current.elt_[0].t());
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
                    add_group_to_unused_groups_list(current_group);
                }
            }

            current.elt_ = current.group_->elements + *(current.group_->skipfield);
            current.skipf_ = current.group_->skipfield + *(current.group_->skipfield);
            current.group_->previous_group = previous_group;

            if (previous_group != nullptr) {
                previous_group->next_group = current.group_;
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

        if (last.elt_ != end_.elt_ || current.elt_ != current.group_->elements + *(current.group_->skipfield)) {
            // ie. not erasing entire group
            size_type number_of_group_erasures = 0;
            // Schema: first erased all non-erased elements until end of group & remove all skipblocks post-last from the free_list.
            // Then, either update preceding skipblock or create new one:

            const const_iterator current_saved = current;

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
                        const skipfield_type prev_free_list_index = current.elt_[0].links_[0];
                        const skipfield_type next_free_list_index = current.elt_[0].links_[1];

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
                            current.group_->elements[prev_free_list_index].links_[1] = std::numeric_limits<skipfield_type>::max();
                        } else {
                            current.group_->elements[next_free_list_index].links_[0] = prev_free_list_index;
                            if (prev_free_list_index != std::numeric_limits<skipfield_type>::max()) {
                                current.group_->elements[prev_free_list_index].links_[1] = next_free_list_index;
                            }
                        }
                    }
                }
            }

            const skipfield_type distance_to_last = static_cast<skipfield_type>(last.elt_ - current_saved.elt_);
            const skipfield_type index = static_cast<skipfield_type>(current_saved.elt_ - last.group_->elements);

            if (index == 0 || current_saved.skipf_[-1] == 0) {
                current_saved.skipf_[0] = distance_to_last;
                last.skipf_[-1] = distance_to_last;

                if (last.group_->is_packed()) {
                    last.group_->next_erasure_ = std::exchange(groups_with_erasures_, last.group_);
                } else {
                    last.group_->elements[last.group_->free_list_head].links_[1] = index;
                }

                current_saved.elt_[0].links_[0] = last.group_->free_list_head;
                current_saved.elt_[0].links_[1] = std::numeric_limits<skipfield_type>::max();
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
                    current.elt_ += static_cast<size_type>(current.skipf_[0]) + 1u;
                    current.skipf_ += current.skipf_[0];
                }
            }


            if ((size_ -= current.group_->size) != 0) {
                // ie. either previous_group != nullptr or next_group != nullptr
                if (!current.group_->is_packed()) {
                    remove_from_groups_with_erasures_list(current.group_);
                }

                current.group_->previous_group->next_group = current.group_->next_group;

                if (current.group_ == end_.group_) {
                    end_.group_ = current.group_->previous_group;
                    end_.elt_ = end_.group_->last_endpoint;
                    end_.skipf_ = end_.group_->skipfield + end_.group_->capacity;
                    add_group_to_unused_groups_list(current.group_);
                    return end_;
                } else if (current.group_ == begin_.group_) {
                    begin_.group_ = current.group_->next_group;
                    const skipfield_type skip = begin_.group_->skipfield[0];
                    begin_.elt_ = begin_.group_->elements + skip;
                    begin_.skipf_ = begin_.group_->skipfield + skip;
                }

                if (current.group_->next_group != end_.group_) {
                    capacity_ -= current.group_->capacity;
                } else {
                    add_group_to_unused_groups_list(current.group_);
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
        if constexpr (std::allocator_traits<allocator_type>::is_always_equal::value && std::is_trivial<GroupPtr>::value && std::is_trivial<AlignedEltPtr>::value && std::is_trivial<SkipfieldPtr>::value) {
            // if all pointer types are trivial we can just copy using memcpy - avoids constructors/destructors etc and is faster
            char temp[sizeof(hive)];
            std::memcpy(&temp, static_cast<void *>(this), sizeof(hive));
            std::memcpy(static_cast<void *>(this), static_cast<void *>(&source), sizeof(hive));
            std::memcpy(static_cast<void *>(&source), &temp, sizeof(hive));
        } else if constexpr (std::is_move_assignable<GroupPtr>::value && std::is_move_assignable<AlignedEltPtr>::value && std::is_move_assignable<SkipfieldPtr>::value && std::is_move_constructible<GroupPtr>::value && std::is_move_constructible<AlignedEltPtr>::value && std::is_move_constructible<SkipfieldPtr>::value) {
            hive temp = std::move(source);
            source = std::move(*this);
            *this = std::move(temp);
        } else {
            const iterator swap_end = end_;
            const iterator swap_begin = begin_;
            const GroupPtr swap_groups_with_erasures = groups_with_erasures_;
            const GroupPtr swap_unused_groups = unused_groups_;
            const size_type swap_size = size_;
            const size_type swap_capacity = capacity_;
            const skipfield_type swap_min_group_capacity = min_group_capacity_;
            const skipfield_type swap_max_group_capacity = max_group_capacity_;

            end_ = source.end_;
            begin_ = source.begin_;
            groups_with_erasures_ = source.groups_with_erasures_;
            unused_groups_ = source.unused_groups_;
            size_ = source.size_;
            capacity_ = source.capacity_;
            min_group_capacity_ = source.min_group_capacity_;
            max_group_capacity_ = source.max_group_capacity_;

            source.end_ = swap_end;
            source.begin_ = swap_begin;
            source.groups_with_erasures_ = swap_groups_with_erasures;
            source.unused_groups_ = swap_unused_groups;
            source.size_ = swap_size;
            source.capacity_ = swap_capacity;
            source.min_group_capacity_ = swap_min_group_capacity;
            source.max_group_capacity_ = swap_max_group_capacity;

            if constexpr (std::allocator_traits<allocator_type>::propagate_on_container_swap::value && !std::allocator_traits<allocator_type>::is_always_equal::value) {
                using std::swap;
                swap(allocator_, source.allocator_);
            }
        }
    }

    friend void swap(hive& a, hive& b) noexcept(noexcept(a.swap(b))) { a.swap(b); }

    void clear() noexcept {
        if (size_ != 0) {
            allocator_type ea = get_allocator();
            if constexpr (!std::is_trivially_destructible<T>::value) {
                for (iterator it = begin_; it != end_; ++it) {
                    std::allocator_traits<allocator_type>::destroy(ea, it.operator->());
                }
            }
            if (begin_.group_ != end_.group_) {
                // Move all other groups onto the unused_groups list
                end_.group_->next_group = unused_groups_;
                unused_groups_ = begin_.group_->next_group;
                end_.group_ = begin_.group_; // other parts of iterator reset in the function below
            }
            reset_only_group_left(begin_.group_);
            groups_with_erasures_ = nullptr;
            size_ = 0;
        }
    }

    void splice(hive& source) {
        // Process: if there are unused memory spaces at the end of the current back group of the chain, convert them
        // to skipped elements and add the locations to the group's free list.
        // Then link the destination's groups to the source's groups and nullify the source.
        // If the source has more unused memory spaces in the back group than the destination,
        // swap them before processing to reduce the number of locations added to a free list and also subsequent jumps during iteration.

        assert_invariants();
        source.assert_invariants();
        assert(&source != this);

        if (source.size_ == 0) {
            return;
        } else if (size_ == 0) {
            *this = std::move(source);
            return;
        }

        // If there's more unused element locations in back memory block of destination than in back memory block of source,
        // swap with source to reduce number of skipped elements during iteration, and reduce size of free-list:
        if ((bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield) - end_.elt_) > (bitcast_pointer<AlignedEltPtr>(source.end_.group_->skipfield) - source.end_.elt_)) {
            swap(source);
        }

        // Throw if incompatible group capacity found:
        if (source.min_group_capacity_ < min_group_capacity_ || source.max_group_capacity_ > max_group_capacity_) {
            for (GroupPtr current_group = source.begin_.group_; current_group != nullptr; current_group = current_group->next_group) {
                if (current_group->capacity < min_group_capacity_ || current_group->capacity > max_group_capacity_) {
                    throw std::length_error("A source memory block capacity is outside of the destination's minimum or maximum memory block capacity limits - please change either the source or the destination's min/max block capacity limits using reshape() before calling splice() in this case");
                }
            }
        }

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
                unused_groups_ = source.unused_groups_;
            } else {
                GroupPtr tail = unused_groups_;
                while (tail->next_group != nullptr) {
                    tail = tail->next_group;
                }
                tail->next_group = source.unused_groups_;
            }
        }

        skipfield_type distance_to_end = static_cast<skipfield_type>(bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield) - end_.elt_);

        if (distance_to_end != 0) {
            // Mark unused element memory locations from back group as skipped/erased:
            // Update skipfield:
            skipfield_type previous_node_value = end_.skipf_[-1];
            end_.group_->last_endpoint = bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield);

            if (previous_node_value == 0) {
                end_.skipf_[0] = distance_to_end;
                end_.skipf_[distance_to_end - 1] = distance_to_end;

                const skipfield_type index = static_cast<skipfield_type>(end_.elt_ - end_.group_->elements);

                if (end_.group_->is_packed()) {
                    end_.group_->next_erasure_ = groups_with_erasures_; // add it to the groups-with-erasures free list
                    groups_with_erasures_ = end_.group_;
                } else {
                    // ie. if this group already has some erased elements
                    bitcast_pointer<SkipfieldPtr>(end_.group_->elements + end_.group_->free_list_head)[1] = index;
                }

                bitcast_pointer<SkipfieldPtr>(end_.elt_)[0] = end_.group_->free_list_head;
                bitcast_pointer<SkipfieldPtr>(end_.elt_)[1] = std::numeric_limits<skipfield_type>::max();
                end_.group_->free_list_head = index;
            } else {
                // update previous skipblock, no need to update free list:
                end_.skipf_[-previous_node_value] = end_.skipf_[distance_to_end - 1] = static_cast<skipfield_type>(previous_node_value + distance_to_end);
            }
        }

#if PLF_HIVE_RELATIONAL_OPERATORS
        size_type groupno = end_.group_->group_number();
        for (GroupPtr g = source.begin_.group_; g != nullptr; g = g->next_group) {
            g->set_group_number(++groupno);
        }
#endif

        // Join the destination and source group chains:
        end_.group_->next_group = source.begin_.group_;
        source.begin_.group_->previous_group = end_.group_;
        end_ = std::move(source.end_);
        size_ += source.size_;
        capacity_ += source.capacity_;

        source.blank();
        assert_invariants();
    }

    inline void splice(hive&& source) { this->splice(source); }

private:
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
            prepare_groups_for_assign(n);
            callback_fill_unused_groups(n, make_itpair_callback(first, last), 0, nullptr, begin_.group_);
        }
    }

    void recover_from_partial_fill() {
        end_.group_->last_endpoint = end_.elt_;
        auto n = static_cast<skipfield_type>(end_.elt_ - end_.group_->elements);
        end_.group_->size = n;
        end_.skipf_ = end_.group_->skipfield + n;
        size_ += n;
        unused_groups_ = end_.group_->next_group;
        end_.group_->next_group = nullptr;
    }

    void recover_from_partial_skipblock_fill(AlignedEltPtr d_first, AlignedEltPtr p, SkipfieldPtr skipf, skipfield_type prev_free_list_node) {
        skipfield_type n = static_cast<skipfield_type>(p - d_first);
        groups_with_erasures_->size = static_cast<skipfield_type>(groups_with_erasures_->size + n);
        size_ += n;
        std::fill_n(skipf, n, skipfield_type());
        d_first[n].links_[0] = prev_free_list_node;
        d_first[n].links_[1] = std::numeric_limits<skipfield_type>::max();

        skipfield_type new_skipblock_head_index = static_cast<skipfield_type>((d_first - groups_with_erasures_->elements) + n);
        groups_with_erasures_->free_list_head = new_skipblock_head_index;

        if (prev_free_list_node != std::numeric_limits<skipfield_type>::max()) {
            groups_with_erasures_->elements[prev_free_list_node].links_[1] = new_skipblock_head_index;
        }
    }

    template<class CB>
    void callback_fill_skipblock(skipfield_type n, CB cb, AlignedEltPtr d_first, SkipfieldPtr skipf) {
        allocator_type ea = get_allocator();
        AlignedEltPtr d_last = d_first + n;
        skipfield_type prev_free_list_node = d_first[0].links_[0];
        for (AlignedEltPtr p = d_first; p != d_last; ++p) {
            hive_try_rollback([&]() {
                std::allocator_traits<allocator_type>::construct(ea, p[0].t(), cb.deref());
                cb.increment();
            }, [&]() {
                recover_from_partial_skipblock_fill(d_first, p, skipf, prev_free_list_node);
            });
        }
        std::fill_n(skipf, n, skipfield_type());
        groups_with_erasures_->size = static_cast<skipfield_type>(groups_with_erasures_->size + n);
        size_ += n;
    }

    template<class CB>
    void callback_fill_empty_group(skipfield_type n, CB cb) {
        allocator_type ea = get_allocator();
        AlignedEltPtr d_last = end_.elt_ + n;
        do {
            hive_try_rollback([&]() {
                std::allocator_traits<allocator_type>::construct(ea, end_.elt_[0].t(), cb.deref());
                cb.increment();
            }, [&]() {
                recover_from_partial_fill();
            });
            ++end_.elt_;
        } while (end_.elt_ != d_last);
        size_ += n;
    }

    template<class CB>
    void callback_fill_unused_groups(size_type n, CB cb, size_type groupno, GroupPtr previous_group, GroupPtr current_group) {
        end_.group_ = current_group;
        for (; end_.group_->capacity < n; end_.group_ = end_.group_->next_group) {
            skipfield_type cap = end_.group_->capacity;
            end_.group_->reset(cap, end_.group_->next_group, previous_group, groupno++);
            previous_group = end_.group_;
            n -= static_cast<size_type>(cap);
            end_.elt_ = end_.group_->elements;
            callback_fill_empty_group(cap, cb);
        }

        // Deal with final group (partial fill)
        unused_groups_ = end_.group_->next_group;
        end_.group_->reset(static_cast<skipfield_type>(n), nullptr, previous_group, groupno);
        end_.elt_ = end_.group_->elements;
        end_.skipf_ = end_.group_->skipfield + n;
        callback_fill_empty_group(n, cb);
    }

    template<class CB>
    void callback_insert_impl(size_type n, CB cb) {
        reserve(size_ + n);
        while (groups_with_erasures_ != nullptr) {
            AlignedEltPtr elt = groups_with_erasures_->elements + groups_with_erasures_->free_list_head;
            SkipfieldPtr skipf = groups_with_erasures_->skipfield + groups_with_erasures_->free_list_head;
            skipfield_type skipblock_length = skipf[0];

            if (groups_with_erasures_ == begin_.group_ && elt < begin_.elt_) {
                begin_.elt_ = elt;
                begin_.skipf_ = skipf;
            }

            if (skipblock_length <= n) {
                groups_with_erasures_->free_list_head = elt[0].links_[0];
                callback_fill_skipblock(skipblock_length, cb, elt, skipf);
                n -= skipblock_length;
                if (groups_with_erasures_->is_packed()) {
                    groups_with_erasures_ = groups_with_erasures_->next_erasure_;
                } else {
                    groups_with_erasures_->elements[groups_with_erasures_->free_list_head].links_[1] = std::numeric_limits<skipfield_type>::max();
                }
                if (n == 0) {
                    return;
                }
            } else {
                skipfield_type prev_index = elt[0].links_[0];
                callback_fill_skipblock(n, cb, elt, skipf);
                skipfield_type new_skipblock_length = static_cast<skipfield_type>(skipblock_length - n);
                skipf[n] = new_skipblock_length;
                skipf[skipblock_length - 1] = new_skipblock_length;
                groups_with_erasures_->free_list_head = static_cast<skipfield_type>(groups_with_erasures_->free_list_head + n);
                elt[n].links_[0] = prev_index;
                elt[n].links_[1] = std::numeric_limits<skipfield_type>::max();
                if (prev_index != std::numeric_limits<skipfield_type>::max()) {
                    groups_with_erasures_->elements[prev_index].links_[1] = groups_with_erasures_->free_list_head;
                }
                return;
            }
        }
        const skipfield_type group_remainder = (static_cast<skipfield_type>(
            bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield) - end_.elt_) >= n) ?
            static_cast<skipfield_type>(n) :
            static_cast<skipfield_type>(bitcast_pointer<AlignedEltPtr>(end_.group_->skipfield) - end_.elt_);

        if (group_remainder != 0) {
            callback_fill_empty_group(group_remainder, cb);
            end_.group_->last_endpoint = end_.elt_;
            end_.group_->size = static_cast<skipfield_type>(end_.group_->size + group_remainder);
            if (n == group_remainder) {
                end_.skipf_ = end_.group_->skipfield + end_.group_->size;
                return;
            }
            n -= group_remainder;
        }
        end_.group_->next_group = unused_groups_;
        callback_fill_unused_groups(n, cb, end_.group_->group_number() + 1, end_.group_, unused_groups_);
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

#if PLF_HIVE_RELATIONAL_OPERATORS
    inline void update_subsequent_group_numbers(GroupPtr g) {
        do {
            g->groupno_ -= 1;
            g = g->next_group;
        } while (g != nullptr);
    }
#else
    inline void update_subsequent_group_numbers(GroupPtr) { }
#endif

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

    inline void reset_only_group_left(GroupPtr const group_) {
        groups_with_erasures_ = nullptr;
        group_->reset(0, nullptr, nullptr, 0);

        // Reset begin and end iterators:
        end_.elt_ = begin_.elt_ = group_->last_endpoint;
        end_.skipf_ = begin_.skipf_ = group_->skipfield;
    }

    inline void add_group_to_unused_groups_list(GroupPtr group_) {
        group_->next_group = std::exchange(unused_groups_, group_);
    }

    void prepare_groups_for_assign(size_type size) {
        allocator_type ea = get_allocator();
        if constexpr (!std::is_trivially_destructible<T>::value) {
            for (iterator current = begin_; current != end_; ++current) {
                std::allocator_traits<allocator_type>::destroy(ea, bitcast_pointer<pointer>(current.elt_)); // Destruct element
            }
        }

        if (size < capacity_ && (capacity_ - size) >= min_group_capacity_) {
            size_type difference = capacity_ - size;
            end_.group_->next_group = unused_groups_;

            // Remove surplus groups which're under the difference limit:
            GroupPtr current_group = begin_.group_;
            GroupPtr previous_group = nullptr;

            do {
                const GroupPtr next_group = current_group->next_group;

                if (current_group->capacity <= difference) {
                    // Remove group:
                    difference -= current_group->capacity;
                    capacity_ -= current_group->capacity;
                    deallocate_group(current_group);
                    if (current_group == begin_.group_) {
                        begin_.group_ = next_group;
                    }
                } else {
                    if (previous_group != nullptr) {
                        previous_group->next_group = current_group;
                    }
                    previous_group = current_group;
                }
                current_group = next_group;
            } while (current_group != nullptr);

            previous_group->next_group = nullptr;
        } else {
            if (size > capacity_) {
                reserve(size);
            }

            // Join all unused_groups to main chain:
            end_.group_->next_group = unused_groups_;
        }

        begin_.elt_ = begin_.group_->elements;
        begin_.skipf_ = begin_.group_->skipfield;
        groups_with_erasures_ = nullptr;
        size_ = 0;
    }

public:
    template <class It, class = std::enable_if_t<!std::is_integral<It>::value>>
    inline void assign(It first, It last) {
        range_assign_impl(std::move(first), std::move(last));
    }

#if __cpp_lib_ranges >= 201911
    template<std::ranges::range R>
    inline void assign_range(R&& rg) {
        range_assign_impl(std::ranges::begin(rg), std::ranges::end(rg));
    }
#endif

    inline void assign(size_type n, const T& value) {
        if (n == 0) {
            clear();
        } else {
            prepare_groups_for_assign(n);
            callback_fill_unused_groups(n, make_value_callback(n, value), 0, nullptr, begin_.group_);
        }
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
        hive temp(plf::hive_limits(min_group_capacity_, max_group_capacity_));
        temp.range_assign_impl(std::make_move_iterator(begin()), std::make_move_iterator(end()));
        this->swap(temp);
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
            while (unused_groups_ != nullptr) {
                capacity_ -= unused_groups_->capacity;
                const GroupPtr next_group = unused_groups_->next_group;
                deallocate_group(unused_groups_);
                unused_groups_ = next_group;
            }
        }
        assert_invariants();
    }

    void reserve(size_type new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }

        if (new_capacity > max_size()) {
            throw std::length_error("Capacity requested via reserve() greater than max_size()");
        }

        new_capacity -= capacity_;

        size_type number_of_max_groups = new_capacity / max_group_capacity_;
        skipfield_type remainder = static_cast<skipfield_type>(new_capacity - (number_of_max_groups * max_group_capacity_));

        if (remainder == 0) {
            remainder = max_group_capacity_;
            --number_of_max_groups;
        } else if (remainder < min_group_capacity_) {
            remainder = min_group_capacity_;
        }

        GroupPtr current_group;
        GroupPtr first_unused_group;

        if (begin_.group_ == nullptr) {
            initialize(remainder);
            begin_.group_->last_endpoint = begin_.group_->elements;
            begin_.group_->size = 0;

            if (number_of_max_groups == 0) {
                return;
            } else {
                first_unused_group = current_group = allocate_group(max_group_capacity_, begin_.group_);
                capacity_ += max_group_capacity_;
                --number_of_max_groups;
            }
        } else {
            first_unused_group = current_group = allocate_group(remainder, end_.group_);
            capacity_ += remainder;
        }

        while (number_of_max_groups != 0) {
            hive_try_rollback([&]() {
                current_group->next_group = allocate_group(max_group_capacity_, current_group);
            }, [&]() {
                deallocate_group(current_group->next_group);
                current_group->next_group = unused_groups_;
                unused_groups_ = first_unused_group;
            });
            current_group = current_group->next_group;
            capacity_ += max_group_capacity_;
            --number_of_max_groups;
        }
        current_group->next_group = unused_groups_;
        unused_groups_ = first_unused_group;
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
        using tuple_allocator_type = AllocOf<item_index_tuple>;
        using tuple_pointer_type = PtrOf<item_index_tuple>;

        if (size_ <= 1) {
            return;
        }

        auto tuple_allocator = tuple_allocator_type(get_allocator());
        tuple_pointer_type sort_array = std::allocator_traits<tuple_allocator_type>::allocate(tuple_allocator, size_, nullptr);
        tuple_pointer_type tuple_pointer = sort_array;

        // Construct pointers to all elements in the sequence:
        size_type index = 0;

        for (auto it = begin_; it != end_; ++it, ++tuple_pointer, ++index) {
            std::allocator_traits<tuple_allocator_type>::construct(tuple_allocator, tuple_pointer, std::addressof(*it), index);
        }

        // Now, sort the pointers by the values they point to:
        std::sort(sort_array, tuple_pointer, [&](const auto& a, const auto& b) { return less(*a.original_location, *b.original_location); });

        // Sort the actual elements via the tuple array:
        index = 0;

        for (tuple_pointer_type current_tuple = sort_array; current_tuple != tuple_pointer; ++current_tuple, ++index) {
            if (current_tuple->original_index != index) {
                T end_value = std::move(*(current_tuple->original_location));
                size_type destination_index = index;
                size_type source_index = current_tuple->original_index;

                do {
                    *(sort_array[destination_index].original_location) = std::move(*(sort_array[source_index].original_location));
                    destination_index = source_index;
                    source_index = sort_array[destination_index].original_index;
                    sort_array[destination_index].original_index = destination_index;
                } while (source_index != index);

                *(sort_array[destination_index].original_location) = std::move(end_value);
            }
        }
        std::allocator_traits<tuple_allocator_type>::deallocate(tuple_allocator, sort_array, size_);
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
