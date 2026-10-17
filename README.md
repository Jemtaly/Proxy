# Proxy

Proxy is a header-only C++ library for exposing ordinary objects through declarative interfaces. It uses C++26 static reflection to generate interface metadata, function tables, inheritance conversions, and method dispatch at compile time.

An interface is a class containing member-function declarations. An implementation is any type that provides compatible operations. Proxy binds the two without requiring the implementation to inherit from, or otherwise depend on, the interface. This separation lets interface authors and implementation authors evolve independently.

## Requirements

- A compiler with C++26 static-reflection support.
- **Compiler version 16.2.0 or later.**
- The `-std=c++26` and `-freflection` compiler options.

Proxy has no build-system or binary dependency. Add the [`include/`](./include/) directory to the compiler's include paths.

The headers rely on C++ compile-time reflection facilities including `<meta>`, reflection operators, splicing, and `template for`. Selecting an ordinary C++23 or C++26 language mode without an implementation of those facilities is insufficient.

The test runners default to `g++-16` on Unix-like platforms and `g++` on Windows. Set `CXX` when the compiler executable has another name or location:

```bash
CXX=/path/to/compiler ./scripts/run-tests
```

```powershell
$env:CXX = "C:\path\to\compiler.exe"
.\scripts\run-tests.ps1
```

## Headers

| Header | Purpose |
| --- | --- |
| [`<proxy.hpp>`](./include/proxy.hpp) | Core handles, interface protocol types, generated facade dispatch, conversions, and implementation lookup |
| [`<proxy/reflection.hpp>`](./include/proxy/reflection.hpp) | Reflection-generated protocol and the `uuid`, `version`, and `since` annotations |
| [`<proxy/memory.hpp>`](./include/proxy/memory.hpp) | Shared and unique ownership wrappers and factories; includes `<proxy.hpp>` |

## Quick start

Include the core library and reflection support, declare an interface, and create a non-owning handle for an implementation object:

```cpp
#include <proxy.hpp>
#include <proxy/reflection.hpp>

#include <string>

struct IGreeter {
    std::string greet() const;
};

struct Greeter {
    std::string name;

    std::string greet() const {
        return "Hello, " + name;
    }
};

int main() {
    Greeter implementation{"Proxy"};
    proxy::interface_handle<IGreeter> greeter = proxy::as_handle<IGreeter>(&implementation);

    return greeter->greet() == "Hello, Proxy" ? 0 : 1;
}
```

Compile it as follows:

```bash
g++-16 -std=c++26 -freflection -Iinclude example.cpp -o example
```

`proxy::as_handle` borrows the supplied object. The caller must keep that object alive while any handle that refers to it is used.

## Capabilities at a glance

1. **Separated, reflection-generated architecture** — interface declarations are separate from implementation types. Static reflection turns the interface declarations into vtables, invokers, inheritance metadata, and dispatch code at compile time.
2. **Non-intrusive adaptation and composition** — `proxy::as_handle` adapts an object without base classes, virtual members, registration, or implementation annotations. One object can expose multiple independent interfaces.
3. **Predictable method resolution** — public members of an implementation and its public bases participate in method resolution. Static interface-aware functions and ordinary member functions can coexist under explicit precedence rules.
4. **Interface inheritance and conversions** — interfaces may have transitive or multiple inheritance; handles convert through that hierarchy and can query other interfaces at runtime.
5. **Ownership-aware type erasure** — borrowed, shared, and unique wrappers preserve interface access while making lifetime semantics explicit.
6. **Extensible protocol** — applications can replace reflection-generated protocol definitions with manually supplied identifiers, vtables, inheritance metadata, method invokers, and implementation tables.
7. **Shared-library and versioning support** — UUID annotations provide stable interface identifiers across dynamic-library boundaries; version and method annotations support compatibility fallbacks for evolving APIs.

### Cost model

Proxy does not allocate when creating a borrowed handle with `as_handle`; it stores an object pointer, type information, and vtable pointers. Reflection generation, interface metadata construction, and method-selection analysis occur at compile time.

An interface call is not generally a zero-overhead abstraction in the strict sense: it dispatches through a generated vtable function pointer. An inheritance upcast uses generated vtable links and does not perform a runtime interface lookup. A runtime cross-cast queries the object's interface metadata and compares interface identifiers, so its cost depends on the exposed interface closure. Shared and unique factories allocate their respective control block and object. These properties should be measured in the target build when call overhead is performance-critical.

## Guide

### 1. Declare a reflective interface

With `<proxy/reflection.hpp>` included, Proxy uses static reflection to turn an interface declaration into its protocol definition. An interface must be a class type whose non-special members are non-static, non-template, non-operator member functions. The declarations describe the callable surface; they do not need definitions.

```cpp
struct ICounter {
    int count() const;
    void reset();
};
```

The generated protocol contains a vtable, method invokers, and metadata for `ICounter`. No base class, virtual member function, registration step, or implementation annotation is required.

### 2. Adapt an implementation and call it

An implementation can satisfy an interface by defining ordinary compatible public member functions:

```cpp
struct Counter {
    int value = 0;

    int count() const {
        return value;
    }

    void reset() {
        value = 0;
    }
};

Counter counter{42};
proxy::interface_handle<ICounter> erased = proxy::as_handle<ICounter>(&counter);

assert(erased);
assert(erased->count() == 42);
erased->reset();
```

`proxy::as_handle<I...>(pointer)` returns an `object_handle` that exposes the listed interfaces. `proxy::interface_handle<I...>` is its type-erased alias. A handle is nullable, supports `operator bool`, and is non-owning: use it for objects owned elsewhere, including stack objects, and do not access it after the original object is destroyed.

An implementation can expose more than one interface in a single handle:

```cpp
struct ICountable {
    int count() const;
};

struct IResettable {
    void reset();
};

auto interfaces = proxy::as_handle<ICountable, IResettable>(&counter);
assert(interfaces->count() == 42);
interfaces->reset();
```

### 3. Control method resolution

For every interface method, Proxy searches public functions with the same name on the implementation type and its public bases. Both ordinary non-static member functions and static member functions are valid implementations.

A static implementation receives an `object_handle<Implementation, Interface>` as its first parameter. This makes the selected interface explicit and allows the implementation to access the original object through `self.get()`:

```cpp
struct ILeft {
    int value() const;
};

struct IRight {
    int value() const;
};

struct ConflictModel {
    static int value(proxy::object_handle<ConflictModel, ILeft>) {
        return 1;
    }

    static int value(proxy::object_handle<ConflictModel, IRight>) {
        return 2;
    }
};

ConflictModel model;
auto handle = proxy::as_handle<ILeft, IRight>(&model);
assert(handle.as<ILeft>()->value() == 1);
assert(handle.as<IRight>()->value() == 2);
```

Regular overloads are also supported:

```cpp
struct ICalculator {
    int calculate() const;
    int calculate(int value) const;
};

struct Calculator {
    int calculate() const {
        return 3;
    }

    int calculate(int value) const {
        return value * 4;
    }
};

Calculator implementation;
auto calculator = proxy::as_handle<ICalculator>(&implementation);
assert(calculator->calculate() == 3);
assert(calculator->calculate(5) == 20);
```

Resolution is deterministic. Proxy considers candidates in this order:

1. A static function whose first parameter is exactly `proxy::object_handle<Implementation, Interface>` and whose remaining parameter types exactly match the interface method.
2. A non-static member function whose parameter types exactly match the interface method.
3. A static function callable with the interface arguments after the interface handle.
4. A non-static member function callable with the interface arguments.

Each candidate's result must be convertible to the interface method's result. Consequently, an exact static function overrides an exact member function, while an exact member function overrides a merely compatible static function. Use a static function to deliberately specialize behavior by requested interface; use ordinary members for the usual object-oriented implementation.

If Proxy finds no suitable candidate for an interface method, instantiating that implementation's function table fails at compile time.

### 4. Model interface inheritance and conversions

Interface inheritance is declarative and independent of the implementation type. Proxy generates the base-vtable links from the interface declaration, so derived handles convert to their base interfaces.

```cpp
struct IValue {
    int value() const;
};

struct IAdjustable : IValue {
    void add(int amount);
};

struct Adjustable {
    int current = 0;

    int value() const {
        return current;
    }

    void add(int amount) {
        current += amount;
    }
};

Adjustable object{5};
proxy::interface_handle<IAdjustable> adjustable = proxy::as_handle<IAdjustable>(&object);
proxy::interface_handle<IValue> value = adjustable;

assert(value->value() == 5);
```

This applies to transitive and multiple interface inheritance. An inheritance upcast uses the generated interface hierarchy and does not perform a runtime metadata query. A conversion is implicit when the relationship makes it unambiguous; otherwise construct the desired handle explicitly.

### 5. Query optional interfaces at runtime

A handle preserves runtime type information for the interfaces it exposes. Constructing another `interface_handle` from it performs a runtime interface query: the result is non-null when the object supports the requested interface and null otherwise.

```cpp
struct IRoot {
    int root() const;
};

struct IExtra {
    int extra() const;
};

struct IAbsent {
    int absent() const;
};

struct Model {
    int root() const { return 10; }
    int extra() const { return 20; }
};

Model model;
proxy::interface_handle<IRoot> root = proxy::as_handle<IRoot, IExtra>(&model);

proxy::interface_handle<IExtra> extra{root};
proxy::interface_handle<IAbsent> absent{root};

assert(extra && extra->extra() == 20);
assert(!absent);
```

Use `proxy::data_handle` when only the type-erased handle is needed before choosing an interface:

```cpp
proxy::data_handle data = proxy::as_handle<IExtra>(&model);
proxy::interface_handle<IExtra> extra{data};
```

### 6. Manage lifetimes with type-erased ownership

Include `<proxy/memory.hpp>` for owning wrappers. The library provides `make_shared` and `make_unique`, plus aliases that erase the implementation type while retaining the exposed interfaces:

| Factory or wrapper | Ownership |
| --- | --- |
| `make_shared<Impl, I...>(args...)` | Reference-counted ownership |
| `interface_holder<I...>` | Shared, type-erased ownership |
| `interface_view<I...>` | Non-owning view of a shared object |
| `make_unique<Impl, I...>(args...)` | Unique ownership |
| `interface_owner<I...>` | Unique, type-erased ownership |
| `data_view`, `data_holder`, `data_owner` | The respective type-erased wrappers with no exposed interface methods |

```cpp
#include <proxy/memory.hpp>

struct ICounter {
    int count() const;
};

struct Counter {
    int count() const {
        return 42;
    }
};

auto shared = proxy::make_shared<Counter, ICounter>();
proxy::interface_view<ICounter> view = shared;
proxy::interface_holder<ICounter> retained = view;

assert(retained->count() == 42);
```

`interface_holder` is copyable. `interface_owner` is move-only:

```cpp
auto unique = proxy::make_unique<Counter, ICounter>();
proxy::interface_owner<ICounter> owner = std::move(unique);
assert(owner->count() == 42);
```

#### Return shared ownership from an implementation

Derive a shared implementation from `proxy::enable_shared_from_this<Impl>` when one of its operations must return a shared reference to itself. This is commonly expressed as a static interface-aware implementation function: it receives the typed object handle, calls `proxy::recover_shared`, and converts the resulting view to a shared type-erased holder.

```cpp
struct ISelf {
    proxy::interface_holder<ISelf> self() const;
};

struct SharedObject : proxy::enable_shared_from_this<SharedObject> {
    static proxy::interface_holder<ISelf> self(proxy::object_handle<SharedObject, ISelf> self) {
        return proxy::recover_shared(self);
    }
};

auto object = proxy::make_shared<SharedObject, ISelf>();
proxy::interface_holder<ISelf> retained = object->self();
```

`recover_shared` requires an `object_handle<Impl, I...>` for an object created by `make_shared` and an implementation derived from `enable_shared_from_this<Impl>`. It returns an `object_view`, so converting it to `interface_holder` increments the shared control block's reference count. Do not use it for borrowed objects or objects owned by `make_unique`.

### 7. Cross a dynamic-library boundary

For runtime interface lookup across dynamic libraries, annotate every shared interface with a non-empty UUID. The UUID becomes the stable identifier used by cross-casts rather than relying on process-local type identity. The default interface ID is empty and does not match by value, so an unannotated interface can be used directly and through declared base conversions but cannot be reliably discovered by a cross-cast across separately built modules.

```cpp
#include <proxy.hpp>
#include <proxy/reflection.hpp>

struct [[= proxy::uuid("example.service.v1")]] IService {
    int value() const;
};
```

The provider creates the handle and exports it through its C ABI:

```cpp
extern "C" void make_service(proxy::interface_handle<IService>* out) {
    static Service service;
    *out = proxy::as_handle<IService>(&service);
}
```

The consumer declares the same UUID-annotated interface and receives the handle. See the complete shared-library build and usage pattern in [`examples/abi/`](./examples/abi/).

### 8. Evolve an interface compatibly

Use `proxy::version` to mark the interface version embedded in its generated function table. Mark a later-added method with `proxy::since`; its fallback is called when a client invokes that method through an older function table.

```cpp
#ifndef EXAMPLE_API_LEVEL
#define EXAMPLE_API_LEVEL 2
#endif

struct [[= proxy::uuid("example.versioned-service")]]
       [[= proxy::version(EXAMPLE_API_LEVEL)]] IService {
    int value() const;

#if EXAMPLE_API_LEVEL >= 2
    [[= proxy::since(2, [](auto self, int increment) {
        return self->value() + increment;
    })]]
    int value_after_upgrade(int increment) const;
#endif
};
```

Build an older provider with `EXAMPLE_API_LEVEL` set to `1`. A version-2 consumer can call `value_after_upgrade`; for a version-1 provider, Proxy calls the fallback instead. The full producer/consumer demonstration is in [`examples/evolution/`](./examples/evolution/).

### 9. Define the protocol manually

Reflection is the preferred path for ordinary C++ interface declarations. When that path is unavailable or deliberately avoided, Proxy's public non-reflective protocol can describe the same interface explicitly. Provide the following specializations in the `proxy` namespace:

1. `interface_id<I>` defines `static constexpr interface_id_t value`. Use a non-empty stable value whenever the interface must be found by runtime cross-cast, especially across dynamic-library boundaries.
2. `vtable<I>` defines `type`, the function-table type for `I`. Its layout is application-defined.
3. `direct_bases<I>` exposes `value`, a `std::array` or span of `direct_base_t` entries. Each `direct_base<Base>(converter)` records a direct base and a converter from `vtable_t<I> const*` to `vtable_t<Base> const*`.
4. `direct_methods<I>` exposes `value`, a `std::array` or span of `direct_method_t` entries. Each `direct_method(name, invoker)` maps a method name to an invoker callable with `interface_handle<I>` followed by that method's arguments.
5. `vtable_implements<Impl, I>` defines `static constexpr vtable_t<I> const* value`, the table used when `Impl` is adapted as `I`.

The protocol is what powers the reflective path as well: base metadata determines hierarchy conversions, method metadata builds the call facade, interface identifiers enable runtime lookup, and implementation tables select the actual dispatch targets. All table pointers, base converters, and invokers must remain valid for the lifetime of every handle that refers to them. Prefer static-storage tables and keep the method names and invoker signatures consistent with the interface facade.

## Appendix: examples and verification

The [`examples/`](./examples/) directory contains self-contained programs:

| Example | Demonstrates |
| --- | --- |
| [`basic`](./examples/basic/main.cpp) | Basic interfaces, interface templates, and hierarchy conversions |
| [`dynamic_cast`](./examples/dynamic_cast/main.cpp) | Runtime interface queries from interface and data handles |
| [`dispatch`](./examples/dispatch/main.cpp) | Overloads, automatic dispatch, and interface-specific static dispatch |
| [`manual`](./examples/manual/main.cpp) | Explicit vtable and metadata specializations |
| [`memory`](./examples/memory/main.cpp) | Shared, unique, and borrowed wrappers |
| [`abi`](./examples/abi/main.cpp) | Shared-library interfaces identified by UUID |
| [`evolution`](./examples/evolution/main.cpp) | Versioned interfaces with method fallbacks |

Build and run every example from the repository root:

```bash
./scripts/run-tests
```

```powershell
.\scripts\run-tests.ps1
```

The scripts compile all standalone and shared-library examples in a temporary directory and print `All tests passed.` when every executable succeeds.
