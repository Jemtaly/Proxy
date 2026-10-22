#include <array>
#include <cassert>

#include <facet.hpp>
#include <facet/reflection.hpp>

// Interface tags
struct[[= fct::uuid("facet.tests.manual.root")]] IManualRoot;
struct[[= fct::uuid("facet.tests.manual.reader")]] IManualReader;
struct[[= fct::uuid("facet.tests.manual.counter")]] IManualCounter;

struct RootDispatch {
    int (*root_value)(void *);
};

struct ReaderDispatch {
    int (*read)(void *);
};

struct CounterDispatch {
    int (*count)(void *);
};

namespace fct {
template<>
struct vtable<IManualRoot> {
    struct type {
        RootDispatch const *self;
    };
};

template<>
struct direct_bases<IManualRoot> {
    static constexpr std::array<direct_base_t, 0> value = {};
};

template<>
struct direct_methods<IManualRoot> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("root_value", [](facets<IManualRoot> handle) {
            return handle.template vtable_at<0>()->self->root_value(handle.get());
        }),
    };
};

template<>
struct vtable<IManualReader> {
    struct type {
        ReaderDispatch const *self;
        vtable_t<IManualRoot> root;
    };
};

template<>
struct direct_bases<IManualReader> {
    static constexpr std::array<direct_base_t, 1> value = {
        direct_base<IManualRoot>([](vtable_t<IManualReader> const *table) {
            return &table->root;
        }),
    };
};

template<>
struct direct_methods<IManualReader> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("read", [](facets<IManualReader> handle) {
            return handle.template vtable_at<0>()->self->read(handle.get());
        }),
    };
};

template<>
struct vtable<IManualCounter> {
    struct type {
        CounterDispatch const *self;
        vtable_t<IManualReader> reader;
    };
};

template<>
struct direct_bases<IManualCounter> {
    static constexpr std::array<direct_base_t, 1> value = {
        direct_base<IManualReader>([](vtable_t<IManualCounter> const *table) {
            return &table->reader;
        }),
    };
};

template<>
struct direct_methods<IManualCounter> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("count", [](facets<IManualCounter> handle) {
            return handle.template vtable_at<0>()->self->count(handle.get());
        }),
    };
};

template<typename Impl>
struct vtable_for<Impl, IManualRoot> {
    static int root_value(void *data) {
        return static_cast<Impl *>(data)->value;
    }

    static constexpr RootDispatch dispatch{
        .root_value = root_value,
    };
    static constexpr vtable_t<IManualRoot> table{
        .self = &dispatch,
    };
    static constexpr vtable_t<IManualRoot> const *value = &table;
};

template<typename Impl>
struct vtable_for<Impl, IManualReader> {
    static int read(void *data) {
        return static_cast<Impl *>(data)->value;
    }

    static constexpr ReaderDispatch dispatch{
        .read = read,
    };
    static constexpr vtable_t<IManualReader> table{
        .self = &dispatch,
        .root = vtable_for<Impl, IManualRoot>::table,
    };
    static constexpr vtable_t<IManualReader> const *value = &table;
};

template<typename Impl>
struct vtable_for<Impl, IManualCounter> {
    static int count(void *data) {
        return static_cast<Impl *>(data)->value;
    }

    static constexpr CounterDispatch dispatch{
        .count = count,
    };
    static constexpr vtable_t<IManualCounter> table{
        .self = &dispatch,
        .reader = vtable_for<Impl, IManualReader>::table,
    };
    static constexpr vtable_t<IManualCounter> const *value = &table;
};
}

struct Counter {
    int value;
};

void test_manual_interface() {
    Counter counter{42};
    auto handle = fct::as_facets<IManualCounter>(&counter);
    assert(handle->count() == 42);
    fct::facets<IManualReader> reader{handle};
    assert(reader->read() == 42);
    fct::facets<IManualRoot> root{reader};
    assert(root->root_value() == 42);

    fct::facets<IManualCounter> recast{reader};
    assert(recast);
    assert(recast->count() == 42);
}

struct IManualBase {};

namespace fct {
template<>
struct interface_id<IManualBase> {
    static constexpr interface_id_t value = "facet.tests.manual.base";
};

template<>
struct vtable<IManualBase> {
    struct type {
        int (*base)(void *data);
    };
};

template<>
struct direct_methods<IManualBase> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("base", [](facets<IManualBase> handle) {
            return handle.template vtable_at<0>()->base(handle.get());
        }),
    };
};

template<>
struct direct_bases<IManualBase> {
    static constexpr std::array<direct_base_t, 0> value = {};
};

template<typename Impl>
struct vtable_for<Impl, IManualBase> {
    static int base(void *data) {
        return static_cast<Impl *>(data)->base();
    }

    static constexpr vtable_t<IManualBase> table{
        .base = base,
    };
    static constexpr vtable_t<IManualBase> const *value = &table;
};
}

struct[[= fct::spec]]
      [[= fct::uuid("facet.tests.manual.derived")]] IDerived : IManualBase {
    int derived();
};

struct Derived {
    int base() {
        return 12;
    }

    int derived() {
        return 34;
    }
};

void test_manual_interface_inheritance() {
    Derived derived;
    auto handle = fct::as_facets<IDerived>(&derived);
    assert(handle->base() == 12);
    assert(handle->derived() == 34);
}

template<typename T>
struct IManualValue;

namespace fct {
template<typename T>
struct interface_id<IManualValue<T>> {
    static constexpr interface_id_t value =
        static_string::make(std::meta::display_string_of(^^IManualValue<T>)).data;
};

template<typename T>
struct vtable<IManualValue<T>> {
    struct type {
        T (*value)(void *);;
    };
};

template<typename T>
struct direct_bases<IManualValue<T>> {
    static constexpr std::array<direct_base_t, 0> value = {};
};

template<typename T>
struct direct_methods<IManualValue<T>> {
    static constexpr std::array<direct_method_t, 1> value = {
        direct_method("value", [](facets<IManualValue<T>> handle) {
            return handle.template vtable_at<0>()->value(handle.get());
        }),
    };
};

template<typename Impl, typename T>
struct vtable_for<Impl, IManualValue<T>> {
    static T invoke(void *data) {
        return static_cast<Impl *>(data)->value;
    }

    static constexpr vtable_t<IManualValue<T>> table{
        .value = invoke,
    };
    static constexpr vtable_t<IManualValue<T>> const *value = &table;
};
}

struct ValueModel {
    int value;
};

void test_manual_template_interface() {
    ValueModel model{56};
    auto handle = fct::as_facets<IManualValue<int>>(&model);
    fct::opaque data = handle;

    fct::facets<IManualValue<int>> value{data};
    assert(value);
    assert(value->value() == 56);

    fct::facets<IManualValue<double>> wrong_value{data};
    assert(!wrong_value);
}

int main() {
    test_manual_interface();
    test_manual_interface_inheritance();
    test_manual_template_interface();
}
