# SG14
[![Build Status](https://travis-ci.org/Quuxplusone/SG14.svg?branch=master)](https://travis-ci.org/Quuxplusone/SG14)

A library of containers and algorithms pioneered by the ISO C++ Committee's
"Low-Latency and Embedded" study group (SG14). For more information on
SG14, see [the mailing list](http://lists.isocpp.org/mailman/listinfo.cgi/sg14).


## What's included

### Efficient algorithms

```
#include <sg14/algorithm_ext.h>

FwdIt sg14::unstable_remove(FwdIt first, FwdIt last, const T& value);
FwdIt sg14::unstable_remove_if(FwdIt first, FwdIt last, Pred pred);
```

`sg14::unstable_remove_if` is like `std::remove_if`, but doesn't preserve the relative order
of the non-removed elements. It doesn't "shuffle elements leftward"; it simply "replaces each
removed element with `*--last`." These algorithms were proposed in
[P0041](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/p0041r0.html).

Note that `std::erase_if(lst, Pred)` is more efficient than
`lst.erase(sg14::unstable_remove_if(lst, Pred), lst.end())` when
`lst` is a `std::list`; therefore P0041 also proposed a set of
`sg14::unstable_erase_if` overloads, which we don't provide, yet.

```
FwdIt sg14::uninitialized_move(It first, Sent last, FwdIt d_first);
FwdIt sg14::uninitialized_value_construct(FwdIt first, Sent last);
FwdIt sg14::uninitialized_default_construct(FwdIt first, Sent last);
void sg14::destroy(FwdIt, FwdIt);
```

These algorithms were proposed in
[P0040](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0040r3.html)
and adopted into C++17. The `sg14` versions are slightly more general (they take
iterator-sentinel pairs) and portable back to C++11, but in C++17 you should
use the `std` versions, please.

### Flat associative container adaptors

```
#include <sg14/flat_set.h>
#include <sg14/flat_map.h>

template<class K, class Comp = less<K>, class Cont = vector<K>>
class sg14::flat_set;

template<class K, class V, class Comp = less<K>, class KCont = vector<K>, class VCont = vector<V>>
class sg14::flat_map;
```

`sg14::flat_set<int>` is a drop-in replacement for `std::set<int>`, but under the hood it uses
a vector to store its data in sorted order, instead of being a node-based tree data structure.
These container adaptors were proposed in
[P1222](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1222r4.pdf) (`flat_set`) and
[P0429](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0429r9.pdf) (`flat_map`),
and adopted into C++23. The `sg14` versions are portable back to C++14.

C++23 also provides `flat_multimap` and `flat_multiset`, which we don't provide.

Boost also provides all four adaptors; see [`boost::container::flat_set`](https://www.boost.org/doc/libs/1_81_0/doc/html/container/non_standard_containers.html#container.non_standard_containers.flat_xxx).

### In-place type-erased types

```
#include <sg14/inplace_function.h>

template<class Signature, size_t Cap, size_t Align>
class sg14::inplace_function;
```

The standard `std::function<int()>` has a "small buffer optimization" so that small callables
(such as lambdas that capture only a few things) can be stored inside the memory footprint of
the `function` object instead of on the heap.

`sg14::inplace_function<int()>` is a drop-in replacement for `std::function<int()>`,
except that it is compile-time-constrained to hold _only_ callables that can fit into its
small buffer. Types that are too large, or too aligned, are simply not convertible to
`sg14::inplace_function<int()>` — you'll get a compile-time error instead.

The size and alignment of the small buffer are configurable via template parameters.

The C++11 `std::function` has a const-correctness issue:

```
    auto lam = [i=0]() mutable { return ++i; };
    const std::function<int()> f = lam;
    assert(f() == 1);
    assert(f() == 2);
```

C++23's `std::move_only_function` and the proposed `std::function_ref` fix this const-correctness issue
by making `operator()` take on the cvref-qualifiers of the `Signature` type; and `sg14::inplace_function`
follows that same design. For example:

```
    auto lam = [i=0]() mutable { return ++i; };
    const sg14::inplace_function<int()> f1 = lam;
    f1(); // does not compile: operator() is a non-const member function

    sg14::function<int()> f2 = lam;
    f2(); // OK: operator() mutates f2, as expected

    const sg14::function<int() const> f3 = lam; // does not compile: lam is not const-callable
    f3(); // would be OK: operator() is a const member function
```

In practice, most of your uses of `std::function<T(U)>` should be replaced not with
`sg14::inplace_function<T(U)>` but rather with `sg14::inplace_function<T(U) const>`.

### Circular `ring_span`

```
#include <sg14/ring_span.h>

template<class T, class Popper = default_popper<T>>
class sg14::ring_span;
```

`sg14::ring_span` is a "view plus metadata" type — not a pure container adaptor like `std::queue`,
but not a pure view type like C++20 `std::span`. It sits on top of a fixed-size contiguous range
(such as a C array), treats the range as a circular buffer, and exposes a `deque`-like API.

`sg14::ring_span` is not a _concurrent queue_; it is only as thread-safe as `std::vector`,
which is to say, it is not thread-safe at all.

Copying a `sg14::ring_span` gives you a second `ring_span` object, with its own head and tail pointers,
but referencing the same underlying range. You probably don't want to do this; pass `ring_span` by
reference or by move, if you pass them around at all.

The `Popper` policy parameter controls whether `r.pop_front()` should return a copy of the popped
element; a move of the popped element; `void` (thus simulating `std::queue::pop()`); or something more
complicated. The default behavior is to move-from the popped element.

This adaptor was proposed in
[P0059](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0059r4.pdf).
Martin Moene has another implementation at [martinmoene/ring-span-lite](https://github.com/martinmoene/ring-span-lite).

### `slot_map`

```
#include <sg14/slot_map.h>

template<class T, class Key = pair<unsigned, unsigned>, template<class...> class Cont = vector>
class sg14::slot_map;
```

The objects in a `slot_map<T>` are stored in a random-access sequence container of type `Cont<T>`,
and use that container's iterators, just like `sg14::flat_set` does.
`slot_map::erase` uses the equivalent of `sg14::unstable_remove` on that container, so it takes O(1) time
and invalidates pointers and iterators.
But `slot_map::insert` returns a value of type `key_type`, not `iterator`, and keys (unlike iterators)
remain stable over insertion and erasure. Use `slot_map::find` to convert a `key_type`
back into a dereferenceable `iterator`:

```
    sg14::slot_map<int> sm = {1, 2, 3, 4, 5};
    sg14::slot_map<int>::key_type key = sm.insert(6);
    assert(sm.find(key) == sm.begin() + 5);
    sm.erase(sm.begin() + 2); // invalidates pointers and iterators, 6 moves to the middle
    assert(sm.find(key) == sm.begin() + 2);
    assert(sm.at(key) == 6); // keys are not invalidated
    sm.erase(sm.begin() + 2);
    assert(sm.find(key) == sm.end());
```

This container adaptor was proposed in
[P0661](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0661r0.pdf).

### `hive` (C++17 and later)

```
#include <sg14/hive.h>

template<class T, class Allocator = allocator<T>>
class sg14::hive;
```

A `hive<T>` is superficially similar to a `std::deque<T>`: it stores its elements in
piecewise contiguous blocks. But where a `deque` manages an array of fixed-size blocks
that never contain "holes" (except for spare capacity at either end), a `hive` manages
a linked list of variable-sized blocks that can contain "holes." Inserting into a `hive`
will go back and fill holes before it allocates new blocks. This means that `hive`,
like `sg14::slot_map`, is not a sequence container (you cannot choose where to insert
elements) but also not an associative container (you cannot quickly find the element with
a specific value). Its value proposition is that pointers remain stable over insertion
and erasure.

`sg14::hive` is directly derived from Matt Bentley's
[`plf::hive`](https://github.com/mattreecebentley/plf_hive),
formerly known as
[`plf::colony`](https://github.com/mattreecebentley/plf_colony). `hive` was proposed in
[P0447](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0447r20.html).

## How to build

```
git clone git@github.com:Quuxplusone/SG14.git
cd SG14
mkdir build
cd build
cmake .. && make && bin/utest
```

To test a particular C++ standard version, set `CMAKE_CXX_STANDARD` at build time:

```
cmake .. -DCMAKE_CXX_STANDARD=17 && make && bin/utest
```

Each individual header file is deliberately standalone; you can copy just the one `.h` file
into your project and it should work fine with no other dependencies (except the C++ standard library).
