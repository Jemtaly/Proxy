# Facet

**Non-intrusive, reflection-driven type erasure for C++26.**

Facet (namespace `fct`) is a header-only library that exposes ordinary objects through declarative interfaces. You describe an interface as an ordinary annotated `struct` that declares member functions. Any type with matching operations can then be adapted into a small type-erased handle. The implementation doesn't inherit from the interface, doesn't declare `virtual` functions and doesn't need to know the interface exists.

At compile time, C++26 static reflection generates the function tables, dispatch thunks, inheritance links, runtime-query metadata and call-site facades. Because interface declarations and implementation types are kept separate, their authors can evolve them independently.

```cpp
struct [[=fct::spec]] INamed {
    std::string name() const;
};

struct Dog {
    std::string name() const {
        return "Rex";
    }
};

Dog dog;
fct::facets<INamed> named = fct::as_facets<INamed>(&dog);
assert(named->name() == "Rex");
```

---

## Table of contents

- [Facet](#facet)
  - [Table of contents](#table-of-contents)
  - [1. Highlights](#1-highlights)
  - [2. Requirements](#2-requirements)
  - [3. Installation](#3-installation)
  - [4. Quick start](#4-quick-start)
  - [5. Core concepts](#5-core-concepts)
    - [5.1 Cost model](#51-cost-model)
  - [6. User guide](#6-user-guide)
    - [6.1 Declaring interfaces](#61-declaring-interfaces)
    - [6.2 Binding implementations](#62-binding-implementations)
    - [6.3 Calling methods](#63-calling-methods)
    - [6.4 Implementation lookup rules](#64-implementation-lookup-rules)
    - [6.5 Custom dispatchers](#65-custom-dispatchers)
    - [6.6 Interface templates](#66-interface-templates)
    - [6.7 Interface inheritance and upcasts](#67-interface-inheritance-and-upcasts)
    - [6.8 Interface identity and runtime queries](#68-interface-identity-and-runtime-queries)
      - [6.8.1 Choosing an id](#681-choosing-an-id)
      - [6.8.2 Query semantics](#682-query-semantics)
    - [6.9 Crossing binary boundaries](#69-crossing-binary-boundaries)
    - [6.10 Interface evolution and versioning](#610-interface-evolution-and-versioning)
    - [6.11 Manually defined interfaces](#611-manually-defined-interfaces)
    - [6.12 Ownership: unique, shared and borrowed handles](#612-ownership-unique-shared-and-borrowed-handles)
      - [6.12.1 Shared from self](#6121-shared-from-self)
  - [7. API reference](#7-api-reference)
    - [7.1 `<facet.hpp>`](#71-facethpp)
      - [7.1.1 Handles](#711-handles)
      - [7.1.2 Handle members](#712-handle-members)
      - [7.1.3 Interface protocol and facades](#713-interface-protocol-and-facades)
      - [7.1.4 Cast customization points](#714-cast-customization-points)
      - [7.1.5 Utilities](#715-utilities)
    - [7.2 `<facet/reflection.hpp>`](#72-facetreflectionhpp)
      - [7.2.1 Spec annotations](#721-spec-annotations)
      - [7.2.2 Generated protocol](#722-generated-protocol)
    - [7.3 `<facet/memory.hpp>`](#73-facetmemoryhpp)
      - [7.3.1 Wrappers](#731-wrappers)
      - [7.3.2 Factories and shared from self](#732-factories-and-shared-from-self)
  - [8. Design and implementation](#8-design-and-implementation)
    - [8.1 Handle layout](#81-handle-layout)
    - [8.2 Interface protocol](#82-interface-protocol)
    - [8.3 Generated vtables](#83-generated-vtables)
    - [8.4 Implementation lookup](#84-implementation-lookup)
    - [8.5 Facades](#85-facades)
    - [8.6 Upcasts](#86-upcasts)
    - [8.7 Queries and type information](#87-queries-and-type-information)
    - [8.8 Ownership control blocks](#88-ownership-control-blocks)
  - [9. Limitations and caveats](#9-limitations-and-caveats)
  - [10. Examples and verification](#10-examples-and-verification)
  - [11. License](#11-license)

---

## 1. Highlights

- **Non-intrusive.** Any type whose public member functions or static functions match an interface implements it. You don't need a base class, `virtual` functions, registration or annotations on the implementation.
- **Interfaces are plain declarations.** Write an annotated `struct` whose methods are declared but never defined.
- **Multiple interfaces per handle.** `fct::facets<IA, IB>` carries one function table per interface. Calls through `->` use normal overload resolution across all of them.
- **Interface inheritance.** Transitive, multiple and diamond inheritance are supported, with implicit upcasts checked at compile time.
- **Runtime cross-casts.** Interfaces identified with `[[=fct::uuid("...")]]` or `[[=fct::local_id]]` can be looked up at runtime from any handle to the same object, including a fully opaque `fct::opaque`.
- **Configurable dispatch.** Exact and compatible matches between member and static functions are picked in a defined order. You can override this per method with `[[=fct::dispatch_via(f)]]`.
- **ABI-oriented layout.** Handles and function tables are simple, trivially copyable structures of pointers that can be passed across shared-library boundaries.
- **Interface evolution.** `[[=fct::version(n)]]` and `[[=fct::since(n, fallback)]]` let newer clients safely use objects built against older interface revisions.
- **Optional ownership wrappers.** `unique_facets`, `shared_facets` (atomic reference counting) and `borrowed_facets` are available, along with `enable_shared_from_self`.
- **Open protocol.** The reflection path is only one source of the interface protocol. You can define any interface by hand with a small set of class-template specializations, and mix manual and reflected interfaces freely.
- **Header-only, with no build-system or binary dependencies.**

---

## 2. Requirements

Facet relies on C++26 language and library features. Choosing a C++23 or C++26 language mode is not enough by itself. The compiler and standard library must actually implement the following:

| Feature | Paper |
| --- | --- |
| Static reflection (`<meta>`, `^^`, `[:...:]`, `std::meta::*`) | P2996 |
| Annotations for reflection (`[[=expr]]`) | P3394 |
| Function parameter reflection | P3096 |
| `std::define_static_array` | P3491 |
| Expansion statements (`template for`) | P1306 |
| `consteval` blocks | P2996 / P3289 |
| Pack indexing (`Ts...[N]`) | P2662 |
| Placeholder variables named `_` | P2169 |

**Compiler:** should have C++26 static reflection support, such as GCC 16.1 or newer, invoked with `-std=c++26 -freflection`.

**Standard library:** must provide `<meta>`, `std::atomic_ref` and the C++23/26 ranges facilities.

---

## 3. Installation

Facet is header-only. Add the [`include/`](./include/) directory to your compiler's include paths.

| Header | Purpose |
| --- | --- |
| [`<facet.hpp>`](./include/facet.hpp) | Core: handles, interface protocol, facades, conversions, runtime queries, and the `uuid` and `local_id` annotations |
| [`<facet/reflection.hpp>`](./include/facet/reflection.hpp) | Protocol generated by reflection from `[[=fct::spec]]` interfaces, plus the `spec`, `version`, `since` and `dispatch_via` annotations |
| [`<facet/memory.hpp>`](./include/facet/memory.hpp) | Unique, shared and borrowed ownership wrappers and factories |

Both `<facet/reflection.hpp>` and `<facet/memory.hpp>` include `<facet.hpp>`.

Compile a translation unit:

```sh
g++-16 -std=c++26 -freflection -Iinclude main.cpp -o main
```

A minimal CMake integration might look like this:

```cmake
add_library(facet INTERFACE)
target_include_directories(facet INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(facet INTERFACE cxx_std_26)
target_compile_options(facet INTERFACE -freflection)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE facet)
```

---

## 4. Quick start

```cpp
#include <cassert>
#include <string>

#include <facet.hpp>
#include <facet/reflection.hpp>

// 1. Declare an interface. Methods are declared only, never defined.
struct [[=fct::spec]] IShape {
    double area() const;
    std::string name() const;
};

// 2. Write implementations. No base class and no virtual functions.
struct Square {
    double side;

    double area() const {
        return side * side;
    }

    std::string name() const {
        return "square";
    }
};

struct Circle {
    double radius;

    double area() const {
        return 3.14159 * radius * radius;
    }

    std::string name() const {
        return "circle";
    }
};

// 3. Consume the interface through a type-erased, non-owning handle.
double describe(fct::facets<IShape> shape) {
    return shape->area();
}

int main() {
    Square square{2.0};
    Circle circle{1.0};

    assert(describe(fct::as_facets<IShape>(&square)) == 4.0);
    assert(describe(fct::as_facets<IShape>(&circle)) > 3.0);

    // Whether a type implements an interface is checked at compile time.
    static_assert(fct::has_vtable_for<Square, IShape>);
    static_assert(!fct::has_vtable_for<int, IShape>);
}
```

---

## 5. Core concepts

| Term | Meaning |
| --- | --- |
| **Interface** (*Face*) | A tag type that describes a set of methods. It is usually a `struct` annotated with `[[=fct::spec]]`, but any type that satisfies the [interface protocol](#82-interface-protocol) qualifies. |
| **Implementation** (*Impl*) | A concrete type whose members satisfy an interface. |
| **Function table** (*VTable*) | A static, constant structure of function pointers, generated for each `(Impl, Face)` pair. |
| **Handle** (*Facets*) | A non-owning fat pointer holding an object pointer, a type-information pointer, and one vtable pointer per interface. |
| **Facade** | The object returned by `handle->` / `*handle`. It has one callable member per method name. |
| **Upcast** | A compile-time conversion from an interface to one of its direct or indirect bases. It is implicit. |
| **Query** | A runtime lookup of an interface by its id through the object's type information. It is explicit and may fail. |

### 5.1 Cost model

- **Creating a borrowed handle** with `as_facets` doesn't allocate. It only stores an object pointer, a type-information pointer and vtable pointers. All tables are static constants.
- **Reflection, metadata generation and method selection** all happen at compile time.
- **An interface call** is an indirect call through a function pointer in a generated vtable. It is not zero-overhead in the strict sense. Its cost is similar to a virtual call.
- **An upcast** follows generated vtable links: one pointer load per inheritance step. It doesn't perform a runtime lookup.
- **A runtime cross-cast** searches the object's interface metadata and compares interface ids. Its cost grows with the number of interfaces the object exposes.
- **`make_shared` and `make_unique`** perform one allocation each, for a control block that contains the object.

If call overhead is critical, measure these costs in your target build.

---

## 6. User guide

### 6.1 Declaring interfaces

An interface spec is a class type annotated with `fct::spec`. Types without the annotation don't get the generated protocol.

```cpp
struct [[=fct::spec]] ICounter {
    int count() const;
    void increment();
};
```

Rules for spec types:

- Only **non-static, non-template member functions** are allowed. Operators and conversion functions are not. Special member functions are ignored.
- Anything else, such as data members, nested types or static members, is rejected at compile time with the message `facet: interface spec contains invalid member function`.
- Methods are never defined. Only their name, return type and parameter types matter. Cv/ref qualifiers and `noexcept` on the method are not part of the generated function-pointer type.
- Methods with the same name are allowed. Each overload gets its own slot.
- Base classes of the spec become interface bases (see [Section 6.7](#67-interface-inheritance-and-upcasts)).

You can customize a spec with annotations:

| Annotation | Applies to | Effect | Guide |
| --- | --- | --- | --- |
| `[[=fct::uuid("id")]]` | Interface | Gives the interface a globally comparable id | [Section 6.8](#68-interface-identity-and-runtime-queries) |
| `[[=fct::local_id]]` | Interface | Gives the interface an id that's valid only within its translation unit | [Section 6.8](#68-interface-identity-and-runtime-queries) |
| `[[=fct::version(n)]]` | Interface | Sets the revision number stored in generated vtables (default 0) | [Section 6.10](#610-interface-evolution-and-versioning) |
| `[[=fct::since(n, fallback)]]` | Method | Marks the method as added in revision `n`, with a fallback for older tables | [Section 6.10](#610-interface-evolution-and-versioning) |
| `[[=fct::dispatch_via(f)]]` | Method | Replaces the automatic implementation lookup for this method | [Section 6.5](#65-custom-dispatchers) |

### 6.2 Binding implementations

`fct::as_facets<Faces...>(Impl* p)` creates a handle to an existing object:

```cpp
struct Counter {
    int value = 0;

    int count() const {
        return value;
    }

    void increment() {
        ++value;
    }
};

Counter counter{42};
auto typed = fct::as_facets<ICounter>(&counter); // typed_facets<Counter, ICounter>
Counter* raw = typed.get(); // still typed: Counter*
fct::facets<ICounter> erased = typed; // implicit: Impl -> void

assert(erased);
assert(erased->count() == 42);
erased->increment();
assert(erased->count() == 43);
```

- The result is a `typed_facets<Impl, Faces...>`. The concrete type stays known until you convert to `facets<...>`.
- `as_facets` compiles only if `fct::has_vtable_for<Impl, Face>` holds for every listed interface. You can check this yourself with `static_assert(fct::has_vtable_for<Counter, ICounter>);`.
- The interfaces listed in `as_facets` also decide what the object can later be **queried** for (see [Section 6.8](#68-interface-identity-and-runtime-queries)).
- Handles are **non-owning**, nullable and trivially copyable. The object must outlive every handle that refers to it. For ownership, see [Section 6.12](#612-ownership-unique-shared-and-borrowed-handles).
- A default-constructed handle is null. `explicit operator bool` returns `true` only when the object pointer and every vtable pointer are non-null.

One handle can expose several independent interfaces:

```cpp
struct [[=fct::spec]] ICountable {
    int count() const;
};

struct [[=fct::spec]] IResettable {
    void reset();
};

auto both = fct::as_facets<ICountable, IResettable>(&counter);
assert(both->count() == 42);
both->reset();
```

### 6.3 Calling methods

`handle->method(args...)` calls through the vtable. Every method name from every interface in the handle, including inherited bases, is available. Normal overload resolution picks among same-named methods:

```cpp
struct [[=fct::spec]] ICalculator {
    int calculate(int value) const;
    double calculate(double value) const;
};

struct Calculator {
    int calculate(int v) const {
        return v * 3;
    }

    double calculate(double v) const {
        return v * 2.5;
    }
};

Calculator calc;
auto h = fct::as_facets<ICalculator>(&calc);
assert(h->calculate(2) == 6);
assert(h->calculate(2.0) == 5.0);
```

Overloading also works **across** interfaces in the same handle:

```cpp
struct [[=fct::spec]] ITextChannel {
    std::string send(int index) const;
};

struct [[=fct::spec]] IIndexChannel {
    double send(double index) const;
};

auto h = fct::as_facets<ITextChannel, IIndexChannel>(&channel);
h->send(2); // ITextChannel::send
h->send(2.0); // IIndexChannel::send
```

If two interfaces declare methods with the **same signature**, a call through `->` is ambiguous. Use `as<Face>()` to choose the interface explicitly:

```cpp
struct [[=fct::spec]] ILeft {
    int value() const;
};

struct [[=fct::spec]] IRight {
    int value() const;
};

auto h = fct::as_facets<ILeft, IRight>(&model);
h.as<ILeft>()->value();
h.as<IRight>()->value();
```

`as<Face>()` exposes only the methods declared **directly** in `Face`, not inherited ones. `Face` must be one of the handle's interfaces or a base that can be reached from one at compile time.

### 6.4 Implementation lookup rules

For each method of a spec interface `Face`, Facet searches `Impl` and its **public bases** (recursively, without duplicates) for **public** functions with the same name. It considers two kinds of candidates:

| Kind | Called as | Exact if |
| --- | --- | --- |
| **Static Member Function** | `Impl::name(typed_facets<Impl, Face> self, args...)` | The first parameter is exactly `typed_facets<Impl, Face>` and the remaining parameter types are identical to the method's |
| **Non-Static Member Function** | `self.get()->name(args...)` | Parameter types are identical to the method's |

A candidate is usable when the call is well-formed and its result converts to the method's return type. The first usable candidate is chosen in this order:

1. An exact static member function
2. An exact non-static member function
3. A compatible (usable but not exact) static member function
4. A compatible non-static member function

So an exact static member function beats an exact non-static member function, and an exact non-static member function beats a merely compatible static member function. If no candidate is usable, `has_vtable_for<Impl, Face>` is `false` and `as_facets` fails to compile. A `[[=fct::dispatch_via(f)]]` annotation skips this search entirely.

```cpp
struct [[=fct::spec]] ISelect {
    int select(int value) const;
};

struct ExactStaticWins {
    int select(int v) const {
        return v + 10;
    }

    static int select(fct::typed_facets<ExactStaticWins, ISelect>, int v) {
        return v + 20;
    } // chosen
};

struct ExactMemberWins {
    int select(int v) const {
        return v + 30;
    } // chosen: exact beats compatible

    static int select(fct::typed_facets<ExactMemberWins, ISelect>, double v) {
        return int(v) + 40;
    }
};
```

Ordinary non-static member functions are the usual way to implement an interface. Static member functions are useful in three situations:

- **Calling other interface methods** through `self->...`.
- **Behavior that depends on the requested interface.** The interface is part of the `self` type, which resolves conflicts like the `ILeft`/`IRight` case above:

  ```cpp
  struct ConflictModel {
      static int value(fct::typed_facets<ConflictModel, ILeft>) {
          return 1;
      }

      static int value(fct::typed_facets<ConflictModel, IRight>) {
          return 2;
      }
  };
  ```

- **Access to the handle itself**, for example to return `fct::shared_from(self)` (see [Section 6.12](#612-ownership-unique-shared-and-borrowed-handles)).

> **Const-correctness:** non-static member candidates are called through a non-const `Impl*`. A `const` qualifier on the spec method is not enforced on the implementation.

### 6.5 Custom dispatchers

`[[=fct::dispatch_via(f)]]` replaces the lookup for one method. `f` is called as `f(typed_facets<Impl, Face> self, args...)`, usually as a generic lambda, and its result must convert to the method's return type. You can use it to supply defaults, write adapters or rename operations:

```cpp
struct [[=fct::spec]] IShippingQuote {
    double quote_dollars_per_kilogram(double kg) const;

    [[=fct::dispatch_via([](auto self, double grams) -> double {
        if constexpr (requires {
            { self.get()->quote_cents_per_gram(grams) } -> std::convertible_to<double>;
        }) {
            return self.get()->quote_cents_per_gram(grams); // native implementation
        } else {
            return self->quote_dollars_per_kilogram(grams / 1000) * 100; // fallback
        }
    })]]
    double quote_cents_per_gram(double grams) const;
};
```

If the dispatcher can't be called for a given `Impl`, for example because a `requires` clause fails, the method has no implementation and `has_vtable_for` is `false`. That makes the dispatcher a constraint as well as an adapter:

```cpp
template<typename Sig> struct IFunction;

template<typename Ret, typename... Args>
struct [[=fct::spec]] IFunction<Ret(Args...)> {
    [[=fct::dispatch_via([](auto self, Args... args) -> Ret
        requires requires {
            { (*self.get())(args...) } -> std::convertible_to<Ret>;
        }
    {
            return (*self.get())(args...);
    })]]
    Ret invoke(Args... args) const;
};

auto lambda = [](int x) {
    return x + 5;
};
auto f = fct::as_facets<IFunction<int(int)>>(&lambda);
assert(f->invoke(10) == 15);
static_assert(!fct::has_vtable_for<decltype(lambda), IFunction<void()>>);
```

### 6.6 Interface templates

Specs can be class templates or partial specializations. Each instantiation is a separate interface:

```cpp
template<class T>
struct [[=fct::spec]] IView {
    T get() const;
};

template<class T>
struct [[=fct::spec]] ICell : IView<T> {
    void set(T value);
};

template<class T> struct Cell {
    T value;

    T get() const {
        return value;
    }

    void set(T next) {
        value = next;
    }
};

Cell<std::string> text{"initial"};
fct::facets<ICell<std::string>> cell = fct::as_facets<ICell<std::string>>(&text);
fct::facets<IView<std::string>> view = cell;
cell->set("updated");
assert(view->get() == "updated");

static_assert(fct::has_vtable_for<Cell<int>, ICell<int>>);
static_assert(!fct::has_vtable_for<Cell<int>, ICell<std::string>>);
```

For how ids work with templates, see [Section 6.8](#68-interface-identity-and-runtime-queries) and [Section 6.11](#611-manually-defined-interfaces).

### 6.7 Interface inheritance and upcasts

Base classes of a spec are interface bases. Inheritance is declared on the
interface and does not depend on the implementation type, but an implementation
must satisfy the whole hierarchy:

```cpp
struct [[=fct::spec]] ITop {
    int top() const;
};

struct [[=fct::spec]] ILeft : ITop {
    int left() const;
};

struct [[=fct::spec]] IRight : ITop {
    int right() const;
};

struct [[=fct::spec]] IBottom : ILeft, IRight {
    int bottom() const;
};

Model model; // has top(), left(), right(), bottom()
fct::facets<IBottom> bottom = fct::as_facets<IBottom>(&model);

bottom->top(); // inherited methods are available directly

fct::facets<ILeft> left = bottom; // implicit upcast
fct::facets<IRight> right = bottom; // implicit upcast
fct::facets<ITop> top = bottom; // implicit upcast through a diamond
```

Upcasts are resolved at compile time and cost one pointer load per inheritance
step. They never perform a runtime query.

Conversion rules for `typed_facets<I, A...>` -> `typed_facets<J, B...>`:

- `J` must be `I` or `void`. The implementation type can be erased but not changed.
- The conversion is **implicit** when every `B` can be reached by upcast from some `A`.
- It is **explicit** when any `B` needs a runtime query (see [Section 6.8](#68-interface-identity-and-runtime-queries)).

A spec can also inherit from a [manually defined interface](#611-manually-defined-interfaces),
because both follow the same protocol.

### 6.8 Interface identity and runtime queries

An interface with an id can be found at runtime, even when it can't be reached at compile time from the handle's interfaces. Converting to a handle of that interface performs a query. The result is non-null if the object exposes the interface and null otherwise.

```cpp
struct [[=fct::spec]] [[=fct::local_id]] IRoot {
    int root() const;
};

struct [[=fct::spec]] [[=fct::local_id]] IExtra {
    int extra() const;
};

struct [[=fct::spec]] [[=fct::local_id]] IAbsent {
    int absent() const;
};

Model model; // has root() and extra()
fct::facets<IRoot> root = fct::as_facets<IRoot, IExtra>(&model);

static_assert(!std::is_convertible_v<fct::facets<IRoot>, fct::facets<IExtra>>); // implicit conversion is not allowed

fct::facets<IExtra> extra{root}; // explicit: runtime query
assert(extra && extra->extra() == 20);

fct::facets<IAbsent> absent{root}; // query fails, so the handle is null
assert(!absent);
```

`fct::opaque` carries no interfaces at all. It's useful when you want to pass an object around before choosing an interface:

```cpp
fct::opaque data = fct::as_facets<IExtra>(&model);
fct::facets<IExtra> extra{data};
```

#### 6.8.1 Choosing an id

| Source | Comparison | Use for |
| --- | --- | --- |
| `[[=fct::uuid("vendor.product.IFoo")]]` | Pointer equality, otherwise string equality | Interfaces shared between translation units or shared libraries |
| `[[=fct::local_id]]` | Pointer identity only (the id string is empty) | Interfaces queried within a single translation unit |
| A manual `fct::interface_id<I>` specialization | As for `uuid` | Manual interfaces and custom naming schemes ([Section 6.11](#611-manually-defined-interfaces)) |

Interfaces without an id still work through direct use and upcasts, but they can't be the target of a runtime query.

With templates, `local_id` gives each specialization its own id. For example, `ITemplate<double>` can't be found on an object that was registered as `ITemplate<int>`.

#### 6.8.2 Query semantics

- **The searchable set is fixed when the handle is created.** It contains the interfaces passed to `as_facets<...>` and all of their bases. An interface that the implementation happens to satisfy but that wasn't listed can't be found.
- **Query-based conversions are always explicit** (`T x{h};` or `static_cast`). Check the result with `operator bool`.
- **Queries use the object's own type information**, so any handle to the object can be queried, whatever interfaces it currently carries.
- **A query is a short linear search with id comparisons.** Keep it off hot paths: convert once and keep the resulting handle.

### 6.9 Crossing binary boundaries

A handle is a trivially copyable structure of pointers, and the vtable layout depends only on the interface declaration. That means handles can be passed between separately built modules through an `extern "C"` function. Interfaces that cross the boundary must use `uuid` ids so that both sides find the same interface at runtime.

```cpp
// interfaces.hpp, shared by both sides
struct [[=fct::spec]] [[=fct::uuid("facet.tests.abi.root")]] IRoot {
    int root_value() const;
};

struct [[=fct::spec]] [[=fct::uuid("facet.tests.abi.extra")]] IExtra {
    int extra_value() const;
};
```

```cpp
// plugin.cpp (shared library)
extern "C" FACET_TEST_EXPORT void facet_test_make_root(fct::facets<IRoot>* out) {
    auto handle = fct::as_facets<IRoot, IExtra>(&service); // registers both for queries
    *out = fct::facets<IRoot>(handle);
}
```

```cpp
// main.cpp (host)
fct::facets<IRoot> root;
facet_test_make_root(&root);
fct::facets<IExtra> extra{root}; // resolved by the plugin's type information, using the uuid
```

In this example, `FACET_TEST_EXPORT` expands to `__declspec(dllexport)` on Windows and `__attribute__((visibility("default")))` elsewhere. The host side uses the matching import macro.

Guidelines:

- **Use `uuid` ids.** `local_id` is valid only in the translation unit that declares it.
- **Check the types in method signatures.** Every parameter and return type must have a compatible ABI on both sides. Be especially careful with standard-library types across different toolchains or library builds.
- **Ownership crosses safely.** An owning wrapper from [Section 6.12](#612-ownership-unique-shared-and-borrowed-handles) always destroys its object through a function pointer stored in its control block. Destruction therefore runs code from the module that created the object.
- **`fct::type_info` carries its own version** (`FACET_TYPE_INFO_VERSION`, currently `0`), so the type-information record can evolve later.
- **Plan for change.** If the interfaces may change on one side but not the other, follow Section 10.

### 6.10 Interface evolution and versioning

Generated vtables start with a `version` field taken from `[[=fct::version(n)]]`. A method marked `[[=fct::since(n, fallback)]]` is called through the vtable only if `vtable->version >= n`. Otherwise the caller runs `fallback(self, args...)` instead and never reads the missing slot. Here `self` is the type-erased `facets<Face>`.

```cpp
struct [[=fct::spec]]
       [[=fct::uuid("facet.tests.versioned-api")]]
       [[=fct::version(FACET_TEST_API_LEVEL)]] IRevisioned {
    int value() const;

#if FACET_TEST_API_LEVEL >= 2
    [[=fct::since(2, [](auto self, int delta) {
        return self->value() + delta;
    })]]
    int value_after_upgrade(int delta) const;
#endif
};
```

Suppose a plugin is compiled with `FACET_TEST_API_LEVEL` set to `1`:

- Its vtables have `version == 1` and no `value_after_upgrade` slot.
- A host compiled with level `2` can still call `value_after_upgrade`, because Facet runs the fallback.
- Both sides compile the same header with different API levels.

Rules for compatible evolution:

1. **Append only.** Add new methods only at the end of the spec.
2. **Annotate new methods.** Mark each one with `since(n, fallback)` and raise `version(n)` to match.
3. **Don't change existing methods.** Don't remove, reorder or change the signature of any of them.
4. **Don't change the bases.** Don't add, remove or reorder them. Base pointers come before all methods in the vtable, so any change shifts every method slot.
5. **Remember that `since` only protects callers.** An implementation compiled against revision `n` must still implement every method of revision `n`. Combine `since` with `dispatch_via` if you want a default on the implementation side too.

### 6.11 Manually defined interfaces

Reflected specs are the recommended way to declare interfaces. When reflection is not available for an interface, or you want full control over the vtable layout, you can describe the interface explicitly. Specialize the protocol templates in namespace `fct`:

| Specialization | Provides |
| --- | --- |
| `vtable<I>` | `type`: the function-table type. You choose its layout. |
| `direct_bases<I>` | `value`: a `std::array` or span of `direct_base_t`, built with `direct_base<Base>(converter)`. The converter maps `vtable_t<I> const*` to `vtable_t<Base> const*`. |
| `direct_methods<I>` | `value`: a `std::array` or span of `direct_method_t`, built with `direct_method(name, invoker)`. The invoker takes `facets<I>` followed by the method's arguments. |
| `vtable_for<Impl, I>` | `static constexpr vtable_t<I> const* value`: the table used when `Impl` is adapted as `I`. |
| `interface_id<I>` *(optional)* | `static constexpr interface_id_t value`: enables runtime queries for `I`. |

Manual interfaces can be incomplete tag types. In the example below, base tables are embedded by value rather than referenced by pointer:

```cpp
struct [[=fct::uuid("facet.tests.manual.root")]] IManualRoot; // declaration only

namespace fct {
template<> struct vtable<IManualRoot> {
    struct type {
        int (*root_value)(void*);
    };
};

template<> struct direct_bases<IManualRoot> {
    static constexpr std::array<direct_base_t, 0> value = {};
};

template<> struct direct_methods<IManualRoot> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("root_value", [](facets<IManualRoot> h) {
            return fct::get<0>(h.vtables)->self->root_value(h.data);
        }),
    };
};

template<class Impl> struct vtable_for<Impl, IManualRoot> {
    static int root_value(void* p) {
        return static_cast<Impl*>(p)->value;
    }

    static constexpr vtable_t<IManualRoot> table = {
        .root_value = root_value,
    };
    static constexpr vtable_t<IManualRoot> const* value = &table;
};
}
```

A derived manual interface declares its bases with a converter:

```cpp
template<> struct direct_bases<IManualChild> {
    static constexpr std::array<direct_base_t, 1> value = {
        direct_base<IManualRoot>([](vtable_t<IManualChild> const* t) {
            return &t->root;
        }),
    };
};
```

For a manual interface template, you can derive an id for each specialization from the reflected type name:

```cpp
template<typename T>
struct interface_id<IManualValue<T>> {
    static constexpr interface_id_t value =
        static_string::make(std::meta::display_string_of(^^IManualValue<T>)).data;
};
```

The spelling produced by `display_string_of` depends on the compiler. Use explicit, stable ids for interfaces that cross a shared-library, compiler or independently versioned build boundary.

Guidelines:

- **Keep tables alive.** Tables, converters and invokers must stay valid for as long as any handle refers to them. Use static-storage `constexpr` tables.
- **Keep names and signatures consistent.** Invoker names and signatures should match the facade you intend to expose.
- **Use Facet's tuple helpers.** Handles store vtable pointers in `fct::tuple`, so access them with `fct::get<I>(handle.vtables)` or `fct::apply`, not `std::get` or `std::apply`.
- **Mix freely.** Manual and reflected interfaces combine without restrictions. For example, `struct [[=fct::spec]] IDerived : IManualBase { ... };` is valid.

---

### 6.12 Ownership: unique, shared and borrowed handles

`<facet/memory.hpp>` adds owning wrappers around handles. Each wrapper stores a handle and a pointer to a heap-allocated control block that contains the object.

| Wrapper | Aliases | Semantics |
| --- | --- | --- |
| `unique_typed_facets<Impl, Faces...>` | `unique_facets<Faces...>`, `unique_opaque` | Sole owner. Move-only. |
| `shared_typed_facets<Impl, Faces...>` | `shared_facets<Faces...>`, `shared_opaque` | Shared owner with an atomic reference count. Copyable. |
| `borrowed_typed_facets<Impl, Faces...>` | `borrowed_facets<Faces...>`, `borrowed_opaque` | Non-owning view of a shared object that can be turned back into a shared owner. |

```cpp
#include <facet/memory.hpp>

auto shared = fct::make_shared<Tracked, ICounter>(); // shared_typed_facets<Tracked, ICounter>
auto copy = shared; // refcount + 1
fct::borrowed_facets<ICounter> view = copy; // no refcount change
fct::shared_facets<ICounter> retained = view; // back to shared: refcount + 1
fct::shared_facets<ICounter> erased = std::move(shared); // move: shared becomes null

auto unique = fct::make_unique<Tracked, ICounter>();
fct::unique_facets<ICounter> owned = std::move(unique); // ownership transferred
```

Always write `fct::make_shared` and `fct::make_unique` with the namespace, so they are not confused with `std::make_shared` and `std::make_unique`.

Conversions between wrappers follow the same implicit/explicit rules as plain handles:

| From -> To | `unique` | `shared` | `borrowed` |
| --- | --- | --- | --- |
| `unique` | move only | - | - |
| `shared` | - | copy (adds a reference) or move | borrow |
| `borrowed` | - | copy (adds a reference) | borrow |

All wrappers forward `operator->`, `operator*`, `as<Face>()` and `get()`. `handle()` returns the underlying non-owning `typed_facets`. `operator bool` also requires a non-null control block.

> **`borrowed_facets` is not a weak pointer.** It doesn't keep the control block or the object alive, and it can't detect that the object has expired. Keep a shared owner alive while you use a borrowed view or turn it back into a shared handle. Otherwise the view dangles.

#### 6.12.1 Shared from self

An implementation that derives from `fct::enable_shared_from_self<Self>` and is created with `fct::make_shared` can get owning or borrowed handles to itself from a typed handle. This is usually done in a static interface-aware function:

```cpp
struct [[=fct::spec]] IRetainable {
    fct::shared_facets<IRetainable> retain() const;
};

struct Retainable : fct::enable_shared_from_self<Retainable> {
    static fct::shared_facets<IRetainable>
    retain(fct::typed_facets<Retainable, IRetainable> self) {
        return fct::shared_from(self); // or fct::borrowed_from(self)
    }
};

auto obj = fct::make_shared<Retainable, IRetainable>();
fct::shared_facets<IRetainable> again = obj->retain();
```

`shared_from` adds a reference. `borrowed_from` returns a `borrowed_typed_facets` without adding one. Don't use either for objects created with `as_facets` or `make_unique`: their control link is null, so the resulting wrappers test `false`.

## 7. API reference

All entities are in namespace `fct`.

### 7.1 `<facet.hpp>`

#### 7.1.1 Handles

| Entity | Description |
| --- | --- |
| `typed_facets<Impl, Faces...>` | Non-owning handle. Members: `Impl* data`, `type_info const* rtti`, `tuple<vtable_t<Faces> const*...> vtables`. |
| `facets<Faces...>` | `typed_facets<void, Faces...>` |
| `opaque` | `facets<>` |
| `as_facets<Faces...>(Impl*)` | Creates a handle with generated vtables and type information. |

#### 7.1.2 Handle members

| Member | Description |
| --- | --- |
| `typed_facets(Impl*, type_info const*, vtable_tuple)`, `explicit typed_facets(Impl*, type_info const*, vtable_t<Faces> const*...)` | Raw constructors. Prefer `as_facets`. |
| `operator typed_facets<Base, Tos...>()` | Implicit if every `To` can be upcast to and `Base` is `Impl` or `void`. Explicit if any needs a query or `Base` is not `void` but `Impl` is. |
| `h->m(args...)`, `(*h).m(args...)` | Calls through the combined facade of all `Faces` and their bases. |
| `h.as<Face>()` | Facade limited to the methods declared directly in `Face`. |
| `h.get()` | Returns `data`, typed as `Impl*`. |
| `h.vtable_at<I>()` | Returns the vtable pointer at index `I` in the tuple of vtables. |
| `explicit operator bool` | True when `data` and every vtable pointer are non-null. |
| `vtable_upcast<To>()`, `vtable_query<To>()`, `vtable_upcast_or_query<To>()` | Low-level vtable access. |
| `is_upcastable<To>`, `is_queryable<To>`, `is_upcastable_or_queryable<To>` | Static capability checks. |

#### 7.1.3 Interface protocol and facades

| Entity | Description |
| --- | --- |
| `vtable<Face>::type`, `vtable_t<Face>` | Function-table type. |
| `vtable_for<Impl, Face>::value`, `vtable_for_v`, `has_vtable_for` | The vtable for an implementation, and a concept that tests whether it exists. |
| `direct_method(name, invoker)` / `direct_method_t` | Represents a direct method entry for the interface. |
| `direct_methods<Face>::value`, `direct_methods_v<Face>` | Range of `direct_method_t`, representing all direct methods of `Face`. |
| `direct_base<Base>(converter)` / `direct_base_t` | Represents a direct base entry for the interface. |
| `direct_bases<Face>::value`, `direct_bases_v<Face>` | Range of `direct_base_t`, representing all direct bases of `Face`. |
| `interface_id<Face>::value`, `interface_id_v`, `has_interface_id` | Interface id (`interface_id_t`, which is `char const*`). |
| `uuid("id")` / `uuid_t` | Stable interface id, compared by pointer or by string. |
| `local_id` / `local_id_t` | Interface id valid within one translation unit, compared by address only. |
| `interface_id_equal(a, b)` | Pointer equality, otherwise equality of non-empty strings. |
| `type_info` | Type information record for an object, containing the version and query function. |
| `type_info_for<Impl, Faces...>`, `type_info_for_v`, `has_type_info_for` | Generated type information for the closure of `Faces`. |
| `facade<Self, Face>` | Generated facade for the methods declared directly in `Face`. |
| `combined_facade<Self, Faces...>` | Generated facade for the closure of all `Faces`. |

#### 7.1.4 Cast customization points

| Entity | Description |
| --- | --- |
| `vtable_upcast_customizer<To, From>` | Upcast implementation. The default walks `direct_bases`. |
| `upcastable_from<To, From>`, `vtable_upcast_from<To, From>(vt)` | Concept and null-safe helper for upcasts. |
| `vtable_query_customizer<To>` | Query implementation. The default calls `rtti->query(data, interface_id_v<To>)`. |
| `queryable<To>`, `vtable_query_with<To>(rtti, data)` | Concept and helper for queries. The helper returns `nullptr` when `data` is null. |

#### 7.1.5 Utilities

| Entity | Description |
| --- | --- |
| `tuple<Ts...>` | Aggregate tuple with a predictable layout, defined through reflection. Used to store vtables. |
| `get<I>(t)`, `apply(f, t)` | Element access and application for `fct::tuple`. Don't use `std::get` or `std::apply`. |
| `static_string` | Null-terminated string with static storage. Build one with `static_string::make(sv)` (`consteval`) and read it with `view()`. |

### 7.2 `<facet/reflection.hpp>`

#### 7.2.1 Spec annotations

| Entity | Applies to | Description |
| --- | --- | --- |
| `spec` / `spec_t` | Interface | Marks a class as a reflected interface spec. |
| `version(n)` / `version_t` | Interface | Interface revision (`interface_version_t`, which is `std::uint64_t`). Default 0. |
| `since(n, fallback)` / `since_t` | Method | Method added in revision `n`. Callers run `fallback(facets<Face>, args...)` when the vtable is older. |
| `dispatch_via(f)` / `dispatch_via_t` | Method | Custom dispatcher, called as `f(typed_facets<Impl, Face>, args...)`. |

#### 7.2.2 Generated protocol

For every `[[=fct::spec]]` type `Face`, this header provides `vtable<Face>`, `direct_methods<Face>` and `direct_bases<Face>`. It also provides `vtable_for<Impl, Face>` (constrained so that it exists only when every base and method can be satisfied). See [Generated vtables](#83-generated-vtables).

### 7.3 `<facet/memory.hpp>`

#### 7.3.1 Wrappers

| Entity | Description |
| --- | --- |
| `unique_typed_facets<Impl, Faces...>`, `unique_facets<Faces...>`, `unique_opaque` | Unique ownership. Move-only. |
| `shared_typed_facets<Impl, Faces...>`, `shared_facets<Faces...>`, `shared_opaque` | Shared ownership with an atomic reference count. |
| `borrowed_typed_facets<Impl, Faces...>`, `borrowed_facets<Faces...>`, `borrowed_opaque` | Borrowed view of a shared object. |

Every wrapper provides `operator->`, `operator*`, `as<Face>()`, `get()`, `vtable_at<I>()`, `explicit operator bool` and `handle()`. `unique` and `shared` also provide `swap()`. For the conversions between wrappers, see [Section 6.12](#612-ownership-unique-shared-and-borrowed-handles).

#### 7.3.2 Factories and shared from self

| Entity | Description |
| --- | --- |
| `make_unique<Impl, Faces...>(args...)` | Allocates a unique control block that contains `Impl(args...)`. |
| `make_shared<Impl, Faces...>(args...)` | Allocates a shared control block that contains `Impl(args...)`. |
| `enable_shared_from_self<Self>` | Base class that receives the control-block link from `make_shared`. |
| `shared_from(typed_facets<Impl, Faces...>)` | Returns a `shared_typed_facets` and adds a reference. |
| `borrowed_from(typed_facets<Impl, Faces...>)` | Returns a `borrowed_typed_facets`. |

---

## 8. Design and implementation

### 8.1 Handle layout

```text
typed_facets<Impl, F1, ..., Fn>
+-----------------------+
| Impl* data            | ----> object pointer
| type_info const* rtti | ----> version + query(void*, id)
| vtable_t<F1> const*   | --+
| ...                   | --+-> fct::tuple (aggregate defined through reflection)
| vtable_t<Fn> const*   | --+
+-----------------------+
```

A handle is `n + 2` pointers wide, standard-layout and trivially copyable. `fct::tuple` is an aggregate generated with `std::meta::define_aggregate`. Unlike `std::tuple`, its layout is predictable and stable, which is why it is used for storage that crosses binary boundaries.

### 8.2 Interface protocol

The core header knows nothing about specs. It reads only these specializations:

- `vtable<Face>::type`
- `direct_methods<Face>::value` (name and invoker)
- `direct_bases<Face>::value` (base type and vtable converter)
- `vtable_for<Impl, Face>::value`
- optionally, `interface_id<Face>::value`

`reflection.hpp` is one source of these specializations, constrained on `is_spec`. Hand-written specializations are another. The *interface closure* of a face is the face plus all of its direct and indirect bases, collected depth-first without duplicates. It's computed only through `direct_bases`, so manual and reflected interfaces combine without special cases.

### 8.3 Generated vtables

For the following spec:

```cpp
struct [[=fct::spec]] [[=fct::version(2)]] IDerived : IBase {
    int get() const;
    void set(int);
};
```

`from_spec_def<Face>` reflects over the class and defines an aggregate equivalent to this:

```cpp
struct vtable_type {
    interface_version_t version; // interface_version (2)
    vtable_t<IBase> const* _; // one pointer per direct base, in declaration order
    int (*_)(fct::facets<IDerived>); // one function pointer per method, in declaration order
    void (*_)(fct::facets<IDerived>, int);
};
```

The fields use the placeholder name `_` and are accessed only through reflection.

Each `(Impl, Face)` pair gets one `static constexpr` table instance. Its method slots point to thunks. Each thunk rebuilds a `typed_facets<Impl, Face>` from the erased handle and calls the selected dispatcher: either an implementation function or a `dispatch_via` callable. `has_vtable_for` is true only if every base has a vtable for `Impl` and every method resolves to a dispatcher.

On the calling side, each method has an *invoker* that loads the handle's first vtable. If the method carries `since(n, fallback)` and the table's `version` is less than `n`, the invoker calls the fallback. Otherwise it calls the slot.

### 8.4 Implementation lookup

For each `Impl`, Facet collects the public static and public non-static member functions of `Impl` and its public bases once (`static_candidates_v` and `member_candidates_v`). For each method, it then:

1. Keeps only the candidates with the method's name.
2. Tests each one with `std::meta::can_substitute` against constrained dispatcher variable templates. This detects unusable candidates without causing hard errors.
3. Checks whether the candidate is exact by comparing parameter types through reflection.

The precedence is described in [Section 6.4](#64-implementation-lookup-rules).

### 8.5 Facades

`combined_facade<Self, Faces...>` is an aggregate generated in a `consteval` block. For each distinct method name in the interface closure, it has one empty `[[no_unique_address]]` member called a *slot*. Each slot is a class template parameterized by all the invokers that share that name. Its `operator()`:

1. Finds the handle's address from its own address, using `std::meta::offset_of`.
2. Calls an overload set built from all of those invokers, passing the handle and the forwarded arguments.

Each invoker takes `facets<Face>` as its first parameter, and the handle converts to that type by implicit upcast. Normal C++ overload resolution then picks the right interface method. `typed_facets::operator->` simply reinterprets the handle's address as a pointer to this facade. `as<Face>()` does the same with a single-interface facade.

### 8.6 Upcasts

The default `vtable_upcast_customizer<To, From>` returns the table unchanged when `To == From`. Otherwise it picks the **first** direct base, in declaration order, from which `To` can be reached. It applies that base's converter and recurses. All of this is decided at compile time. In a diamond the leftmost path is used. For reflected specs, every path leads to equivalent tables for the same `Impl`.

### 8.7 Queries and type information

`type_info_for<Impl, Faces...>` generates a static `type_info`. Its `query(void*, id)` function walks the closure of every face listed in `as_facets`. It compares ids with `interface_id_equal` and returns the upcast vtable, or `nullptr` if nothing matches. The query function belongs to the module that created the handle. That's why a host can query objects from a plugin without knowing their implementation types.

`local_id` gives an interface an empty string as its id. Empty strings never match by content, so only the address can match. `uuid` ids can also match by content, which makes them reliable across module boundaries.

### 8.8 Ownership control blocks

`make_unique` and `make_shared` allocate one block that holds a small header followed by the `Impl` object:

- **Unique header:** a `destroy` function pointer.
- **Shared header:** a 32-bit reference count and `destroy`. The count is updated with `std::atomic_ref`, using a relaxed increment and an acquire-release decrement.

Because `destroy` is stored in the block, destruction always runs code from the module that created the object. For `enable_shared_from_self` types, the control block also writes its own address into the object during construction.

---

## 9. Limitations and caveats

- **Compiler support.** Facet requires a compiler and standard library that implement C++26 static reflection, such as GCC 16+ with `-freflection`.
- **Spec contents.** Specs can't contain data members, nested types, static functions, operators, conversion functions or member templates.
- **Method qualifiers are not modelled.** `const`, ref-qualifiers and `noexcept` on spec methods don't affect the vtable or the lookup, and constness is not enforced on implementations.
- **Default arguments** on spec methods are not available through the facade.
- **Same-signature methods from different interfaces** are ambiguous through `->`. Use `as<Face>()` to choose one.
- **Queries only find registered interfaces:** those passed to `as_facets` and their bases.
- **`local_id` is translation-unit-local.** Use `uuid` across modules. `display_string_of`-based ids depend on the compiler.
- **Handles are non-owning.** You're responsible for lifetimes unless you use the `memory.hpp` wrappers. `borrowed_*` handles don't keep objects alive and can't detect expiry.
- **Handles built with the raw constructors** need a valid `rtti` pointer before you query them. Prefer `as_facets`.
- **Exceptions during construction.** `make_unique` and `make_shared` construct `Impl` in a `noexcept` context, so an exception thrown by its constructor calls `std::terminate`.
- **`enable_shared_from_self`** works only for objects created with `fct::make_shared`.
- **No weak references** are provided.
- **Implementation-defined technique.** The facade relies on `reinterpret_cast` between a handle and an empty facade aggregate. This works on supported compilers, but it is an implementation technique rather than a portable language guarantee.

---

## 10. Examples and verification

The [`examples/`](./examples/) directory contains programs that check themselves with `assert`. Don't build them with `NDEBUG`.

| Example | Demonstrates |
| --- | --- |
| [`basic`](./examples/basic/) | Specs, `as_facets`, inheritance, diamond upcasts, interface templates, `has_vtable_for` |
| [`query`](./examples/query/) | Runtime queries with `local_id` from interface and opaque handles, failing queries, template interfaces |
| [`overload`](./examples/overload/) | Overloads within one interface and across interfaces, interface-specific static dispatch, `as<Face>()` |
| [`dispatch`](./examples/dispatch/) | Lookup precedence, defaults with `dispatch_via`, constrained dispatchers |
| [`manual`](./examples/manual/) | Hand-written protocol specializations, embedded base tables, ids for template interfaces, mixing manual and reflected interfaces |
| [`memory`](./examples/memory/) | `make_unique`, `make_shared`, borrowed views, `enable_shared_from_self` |
| [`abi`](./examples/abi/) | Passing handles across a shared-library boundary and querying with `uuid` |
| [`evolution`](./examples/evolution/) | `version` and `since`: a level-2 host using a level-1 plugin |

To build and run every example, run one of these from the repository root:

```bash
./scripts/run-tests
```

```powershell
.\scripts\run-tests.ps1
```

What the scripts do:

- **Build location:** everything is built in a temporary directory that's removed afterwards.
- **Standalone examples:** compiled and run directly.
- **Shared-library examples:** the plugin is built as a shared library first. That's a `.so` with an rpath on Unix-like systems, or a DLL with an import library on Windows.
- **Result:** `All tests passed.` is printed when every program succeeds.

The runners use `g++-16` by default on Unix-like platforms and `g++` on Windows (a MinGW-style GCC, since `--out-implib` is used). Set `CXX` to use a different compiler:

```bash
CXX=/path/to/compiler ./scripts/run-tests
```

```powershell
$env:CXX = "C:\path\to\compiler.exe"
.\scripts\run-tests.ps1
```

---

## 11. License

Facet is released under the [MIT License](./LICENSE). Copyright (c) 2026 Jemtaly.
