#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include <facet.hpp>

// ============================================================================
// Ownership wrappers
//
// borrowed_typed_facets is non-owning, shared_typed_facets shares ownership,
// and unique_typed_facets uniquely owns its implementation. The interface and
// opaque aliases expose these same wrappers without an implementation type.
// ============================================================================

namespace fct {
namespace detail {
struct unique_control {
    void (*destroy)(unique_control *) noexcept;
};

inline void release(unique_control *control) noexcept {
    if (control != nullptr) {
        control->destroy(control);
    }
}

template<class Impl>
struct unique_control_for_def {
    struct type : unique_control {
        Impl impl;

        template<class... Args>
        explicit type(Args &&...args) noexcept : unique_control{.destroy = destroy}, impl(std::forward<Args>(args)...) {}

        static void destroy(unique_control *control) noexcept {
            delete static_cast<type *>(control);
        }

        Impl *get_impl() noexcept {
            return &impl;
        }
    };
};

template<class Impl>
using unique_control_for = typename unique_control_for_def<Impl>::type;
}

template<class Impl, class... Faces>
struct unique_typed_facets;

template<class... Faces>
using unique_facets = unique_typed_facets<void, Faces...>;

using unique_opaque = unique_facets<>;

template<class Impl, class... Faces>
struct unique_typed_facets {
    using handle_type = typed_facets<Impl, Faces...>;

    handle_type handle_;
    detail::unique_control *control_ = nullptr;

    constexpr unique_typed_facets() noexcept = default;

    unique_typed_facets(handle_type handle, detail::unique_control *control) noexcept : handle_(handle), control_(control) {}

    unique_typed_facets(unique_typed_facets const &) = delete;

    unique_typed_facets(unique_typed_facets &&other) noexcept : handle_(other.handle_), control_(std::exchange(other.control_, nullptr)) {}

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type> && !std::is_same_v<unique_typed_facets<Base, Tos...>, unique_typed_facets>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator unique_typed_facets<Base, Tos...>() && noexcept {
        return unique_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), std::exchange(control_, nullptr));
    }

    ~unique_typed_facets() {
        detail::release(control_);
    }

    void swap(unique_typed_facets &other) noexcept {
        std::swap(handle_, other.handle_);
        std::swap(control_, other.control_);
    }

    unique_typed_facets &operator=(unique_typed_facets other) noexcept {
        swap(other);
        return *this;
    }

    auto handle() const noexcept {
        return handle_;
    }

    auto *get() const noexcept {
        return handle_.get();
    }

    template<std::size_t K>
    auto const *vtable_at() const noexcept {
        return handle_.template vtable_at<K>();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(handle_) && control_ != nullptr;
    }

    auto &operator*() const noexcept {
        return *handle_;
    }

    auto *operator->() const noexcept {
        return handle_.operator->();
    }

    template<class Face>
    auto *as() const noexcept {
        return handle_.template as<Face>();
    }
};

template<class Impl, class... Faces, class... Args>
auto make_unique(Args &&...args) {
    auto *control = new detail::unique_control_for<Impl>(std::forward<Args>(args)...);
    return unique_typed_facets<Impl, Faces...>(as_facets<Faces...>(control->get_impl()), control);
}
}

namespace fct {
namespace detail {
using ref_count_t = std::uint32_t;

struct shared_control {
    ref_count_t references = 1;
    void (*destroy)(shared_control *) noexcept;
};

inline shared_control *add_ref(shared_control *control) noexcept {
    if (control != nullptr) {
        std::atomic_ref<ref_count_t>(control->references).fetch_add(1, std::memory_order_relaxed);
    }
    return control;
}

inline void release(shared_control *control) noexcept {
    if (control != nullptr) {
        if (std::atomic_ref<ref_count_t>(control->references).fetch_sub(1, std::memory_order_acq_rel) == 1) {
            control->destroy(control);
        }
    }
}

template<class Impl>
struct shared_control_for_def {
    struct type : shared_control {
        Impl impl;

        template<class... Args>
        explicit type(Args &&...args) noexcept : shared_control{.destroy = destroy}, impl(std::forward<Args>(args)...) {}

        static void destroy(shared_control *control) noexcept {
            delete static_cast<type *>(control);
        }

        Impl *get_impl() noexcept {
            return &impl;
        }
    };
};

template<class Impl>
using shared_control_for = typename shared_control_for_def<Impl>::type;
}

template<class Impl, class... Faces>
struct borrowed_typed_facets;

template<class... Faces>
using borrowed_facets = borrowed_typed_facets<void, Faces...>;

using borrowed_opaque = borrowed_facets<>;

template<class Impl, class... Faces>
struct shared_typed_facets;

template<class... Faces>
using shared_facets = shared_typed_facets<void, Faces...>;

using shared_opaque = shared_facets<>;

template<class Impl, class... Faces>
struct borrowed_typed_facets {
    using handle_type = typed_facets<Impl, Faces...>;

    handle_type handle_;
    detail::shared_control *control_ = nullptr;

    constexpr borrowed_typed_facets() noexcept = default;

    borrowed_typed_facets(handle_type handle, detail::shared_control *control) noexcept : handle_(handle), control_(control) {}

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type> && !std::is_same_v<borrowed_typed_facets<Base, Tos...>, borrowed_typed_facets>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator borrowed_typed_facets<Base, Tos...>() const & noexcept {
        return borrowed_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), control_);
    }

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator shared_typed_facets<Base, Tos...>() const & noexcept {
        return shared_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), detail::add_ref(control_));
    }

    auto handle() const noexcept {
        return handle_;
    }

    auto *get() const noexcept {
        return handle_.get();
    }

    template<std::size_t K>
    auto const *vtable_at() const noexcept {
        return handle_.template vtable_at<K>();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(handle_) && control_ != nullptr;
    }

    auto &operator*() const noexcept {
        return *handle_;
    }

    auto *operator->() const noexcept {
        return handle_.operator->();
    }

    template<class Face>
    auto *as() const noexcept {
        return handle_.template as<Face>();
    }
};

template<class Impl, class... Faces>
struct shared_typed_facets {
    using handle_type = typed_facets<Impl, Faces...>;

    handle_type handle_;
    detail::shared_control *control_ = nullptr;

    constexpr shared_typed_facets() noexcept = default;

    shared_typed_facets(handle_type handle, detail::shared_control *control) noexcept : handle_(handle), control_(control) {}

    shared_typed_facets(shared_typed_facets const &other) noexcept : handle_(other.handle_), control_(detail::add_ref(other.control_)) {}

    shared_typed_facets(shared_typed_facets &&other) noexcept : handle_(other.handle_), control_(std::exchange(other.control_, nullptr)) {}

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type> && !std::is_same_v<shared_typed_facets<Base, Tos...>, shared_typed_facets>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator shared_typed_facets<Base, Tos...>() const & noexcept {
        return shared_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), detail::add_ref(control_));
    }

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type> && !std::is_same_v<shared_typed_facets<Base, Tos...>, shared_typed_facets>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator shared_typed_facets<Base, Tos...>() && noexcept {
        return shared_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), std::exchange(control_, nullptr));
    }

    template<class Base, class... Tos>
        requires(std::is_constructible_v<typed_facets<Base, Tos...>, handle_type>)
    explicit(!std::is_convertible_v<handle_type, typed_facets<Base, Tos...>>)
    operator borrowed_typed_facets<Base, Tos...>() const & noexcept {
        return borrowed_typed_facets<Base, Tos...>(typed_facets<Base, Tos...>(handle_), control_);
    }

    ~shared_typed_facets() {
        detail::release(control_);
    }

    void swap(shared_typed_facets &other) noexcept {
        std::swap(handle_, other.handle_);
        std::swap(control_, other.control_);
    }

    shared_typed_facets &operator=(shared_typed_facets other) noexcept {
        swap(other);
        return *this;
    }

    auto handle() const noexcept {
        return handle_;
    }

    auto *get() const noexcept {
        return handle_.get();
    }

    template<std::size_t K>
    auto const *vtable_at() const noexcept {
        return handle_.template vtable_at<K>();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(handle_) && control_ != nullptr;
    }

    auto &operator*() const noexcept {
        return *handle_;
    }

    auto *operator->() const noexcept {
        return handle_.operator->();
    }

    template<class Face>
    auto *as() const noexcept {
        return handle_.template as<Face>();
    }
};

template<class Impl, class... Faces, class... Args>
auto make_shared(Args &&...args) {
    auto *control = new detail::shared_control_for<Impl>(std::forward<Args>(args)...);
    return shared_typed_facets<Impl, Faces...>(as_facets<Faces...>(control->get_impl()), control);
}

template<class Self>
struct enable_shared_from_self {
    detail::shared_control *control_ = nullptr;

    enable_shared_from_self() = default;

    detail::shared_control *get_control() noexcept {
        return control_;
    }
};

namespace detail {
template<class Impl>
    requires(std::derived_from<Impl, enable_shared_from_self<Impl>>)
struct shared_control_for_def<Impl> {
    struct type : shared_control {
        Impl impl;

        template<class... Args>
        explicit type(Args &&...args) noexcept : shared_control{.destroy = destroy}, impl(std::forward<Args>(args)...) {
            impl.control_ = this;
        }

        static void destroy(shared_control *control) noexcept {
            delete static_cast<type *>(control);
        }

        Impl *get_impl() noexcept {
            return &impl;
        }
    };
};
}

template<class Impl, class... Faces>
    requires(std::derived_from<Impl, enable_shared_from_self<Impl>>)
auto borrowed_from(typed_facets<Impl, Faces...> handle) {
    return borrowed_typed_facets<Impl, Faces...>(handle, handle.get()->get_control());
}

template<class Impl, class... Faces>
    requires(std::derived_from<Impl, enable_shared_from_self<Impl>>)
auto shared_from(typed_facets<Impl, Faces...> handle) {
    return shared_typed_facets<Impl, Faces...>(handle, detail::add_ref(handle.get()->get_control()));
}
}
