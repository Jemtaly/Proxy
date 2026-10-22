#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <facet.hpp>

// ============================================================================
// Reflection utilities
// ============================================================================

namespace fct {
// [[=spec]] marks a type as a facet interface specification.
struct spec_t {};

inline constexpr spec_t spec{};

using interface_version_t = std::uint64_t;

// [[=version(n)]] sets the version recorded in the generated function table;
// it defaults to zero when omitted.
struct version_t {
    interface_version_t value;
};

consteval version_t version(interface_version_t value) {
    return {value};
}

// [[=since(n, impl)]] marks a method as introduced in version n. `impl` is
// called as a fallback when the function table's version is older than n.
struct since_t {
    interface_version_t version;
    std::meta::info fallback;
};

template<class Fallback>
consteval since_t since(interface_version_t version, Fallback fallback) {
    return {version, std::meta::reflect_constant(fallback)};
}

// [[=dispatch_via(func)]] specifies the function used to dispatch the
// method in the generated function table.
struct dispatch_via_t {
    std::meta::info dispatcher;
};

template<class Dispatcher>
consteval dispatch_via_t dispatch_via(Dispatcher dispatcher) {
    return {std::meta::reflect_constant(dispatcher)};
}

namespace detail {
template<class Face>
concept is_spec = !meta::annotations_of_with_type(^^Face, ^^spec_t).empty();

inline consteval interface_version_t get_interface_version(meta::info face) {
    auto annotations = meta::annotations_of_with_type(face, ^^version_t);
    return annotations.empty() ? 0 : meta::extract<version_t>(annotations[0]).value;
}

inline consteval since_t get_method_since(meta::info method) {
    auto annotations = meta::annotations_of_with_type(method, ^^since_t);
    return annotations.empty() ? since_t{} : meta::extract<since_t>(annotations[0]);
}

inline consteval meta::info get_method_dispatcher(meta::info method) {
    auto annotations = meta::annotations_of_with_type(method, ^^dispatch_via_t);
    return annotations.empty() ? meta::info{} : meta::extract<dispatch_via_t>(annotations[0]).dispatcher;
}

template<class R, class... A>
using function_signature_t = R(A...);

inline consteval meta::info method_signature(meta::info method) {
    std::vector<meta::info> components = {meta::return_type_of(method)};
    for (meta::info parameter : meta::parameters_of(method)) {
        components.push_back(meta::type_of(parameter));
    }
    return meta::substitute(^^function_signature_t, components);
}

static consteval bool is_static_candidate_function(meta::info candidate) {
    return meta::is_function(candidate) && meta::is_static_member(candidate);
}

static consteval bool is_member_candidate_function(meta::info candidate) {
    return meta::is_function(candidate) &&
           !meta::is_constructor(candidate) &&
           !meta::is_destructor(candidate) &&
           !meta::is_static_member(candidate);
}

static consteval void collect_static_candidates(
    meta::info impl,
    std::vector<meta::info> &visited,
    std::vector<meta::info> &out) {
    for (meta::info v : visited) {
        if (meta::is_same_type(v, impl)) {
            return;
        }
    }
    visited.push_back(impl);
    for (meta::info candidate : meta::members_of(impl, pub_ctx())) {
        if (is_static_candidate_function(candidate)) {
            out.push_back(candidate);
        }
    }
    for (meta::info base : meta::bases_of(impl, pub_ctx())) {
        collect_static_candidates(meta::type_of(base), visited, out);
    }
}

static consteval void collect_member_candidates(
    meta::info impl,
    std::vector<meta::info> &visited,
    std::vector<meta::info> &out) {
    for (meta::info v : visited) {
        if (meta::is_same_type(v, impl)) {
            return;
        }
    }
    visited.push_back(impl);
    for (meta::info candidate : meta::members_of(impl, pub_ctx())) {
        if (is_member_candidate_function(candidate)) {
            out.push_back(candidate);
        }
    }
    for (meta::info base : meta::bases_of(impl, pub_ctx())) {
        collect_member_candidates(meta::type_of(base), visited, out);
    }
}

template<class Impl>
inline constexpr std::span<meta::info const> static_candidates_v = [] {
    std::vector<meta::info> out;
    std::vector<meta::info> visited;
    collect_static_candidates(^^Impl, visited, out);
    return std::define_static_array(out);
}();

template<class Impl>
inline constexpr std::span<meta::info const> member_candidates_v = [] {
    std::vector<meta::info> out;
    std::vector<meta::info> visited;
    collect_member_candidates(^^Impl, visited, out);
    return std::define_static_array(out);
}();

template<class Face>
struct from_spec_def {
    using facets_type = facets<Face>;

    struct vtable_type;

    static constexpr interface_version_t interface_version = get_interface_version(^^Face);

    enum class field_kind {
        base,
        method,
    };

    struct field_descriptor {
        field_kind kind;
        meta::info spec;
    };

    static constexpr std::span<field_descriptor const> field_descriptors = [] {
        if (!meta::is_class_type(^^Face)) {
            compile_time_error("facet: interface spec must be a class type");
        }
        std::vector<field_descriptor> result;
        for (meta::info base : meta::bases_of(^^Face, all_ctx())) {
            result.emplace_back(field_kind::base, base);
        }
        for (meta::info member : meta::members_of(^^Face, all_ctx())) {
            if (meta::is_special_member_function(member)) {
                continue;
            }
            if (!meta::is_function(member) ||
                meta::is_constructor(member) ||
                meta::is_destructor(member) ||
                meta::is_static_member(member) ||
                meta::is_operator_function(member) ||
                meta::is_conversion_function(member) ||
                meta::is_function_template(member)) {
                compile_time_error("facet: interface spec contains invalid member function");
            }
            result.emplace_back(field_kind::method, member);
        }
        return std::define_static_array(result);
    }();

    template<meta::info Base,
             class Type = typename[:meta::type_of(Base):]>
    struct base_pointer {
        using type = vtable_t<Type> const *;
    };

    template<meta::info Base>
    using base_pointer_t = typename base_pointer<Base>::type;

    template<meta::info Method,
             class Sig = typename[:method_signature(Method):]>
    struct method_pointer;

    template<meta::info Method, class R, class... A>
    struct method_pointer<Method, R(A...)> {
        using type = R (*)(facets_type, A...);
    };

    template<meta::info Method>
    using method_pointer_t = method_pointer<Method>::type;

    consteval {
        std::vector<meta::info> members = {
            meta::data_member_spec(^^interface_version_t, {.name = "version"}),
        };
        for (auto desc : field_descriptors) {
            if (desc.kind == field_kind::base) {
                members.push_back(meta::data_member_spec(
                    meta::substitute(
                        ^^base_pointer_t,
                        {
                            meta::reflect_constant(desc.spec),
                        }),
                    {.name = "_"}));
            }
            if (desc.kind == field_kind::method) {
                members.push_back(meta::data_member_spec(
                    meta::substitute(
                        ^^method_pointer_t,
                        {
                            meta::reflect_constant(desc.spec),
                        }),
                    {.name = "_"}));
            }
        }
        meta::define_aggregate(^^vtable_type, members);
    }

    static constexpr std::span<meta::info const> vtable_members = all_data_members_v<vtable_type>.subspan(1);

    template<auto... Members>
    static constexpr vtable_type content_with = {
        interface_version,
        Members...,
    };

    template<auto... Members>
    static constexpr vtable_type const *value_with = &content_with<Members...>;

    template<meta::info Base, meta::info Member,
             class Type = typename[:meta::type_of(Base):]>
    struct automatic_base_converter_t {
        constexpr vtable_t<Type> const *operator()(vtable_type const *vt) const noexcept {
            return vt->[:Member:];
        }
    };

    template<meta::info Base, meta::info Member>
    static constexpr automatic_base_converter_t<Base, Member> automatic_base_converter{};

    template<meta::info Method, meta::info Member,
             class Sig = typename[:method_signature(Method):]>
    struct automatic_method_invoker_t;

    template<meta::info Method, meta::info Member, class R, class... A>
    struct automatic_method_invoker_t<Method, Member, R(A...)> {
        R operator()(facets_type self, A... args) const {
            vtable_type const *vt = self.template vtable_at<0>();
            constexpr since_t since = get_method_since(Method);
            if constexpr (since.fallback != meta::info{}) {
                if (vt->version < since.version) {
                    return [:since.fallback:](self, std::forward<A>(args)...);
                }
            }
            return vt->[:Member:](self, std::forward<A>(args)...);
        }
    };

    template<meta::info Method, meta::info Member>
    static constexpr automatic_method_invoker_t<Method, Member> automatic_method_invoker{};

    struct direct_members_t {
        std::span<direct_base_t const> bases;
        std::span<direct_method_t const> methods;
    };

    static constexpr direct_members_t direct_members = [] {
        std::vector<direct_base_t> bases;
        std::vector<direct_method_t> methods;
        for (auto [desc, member] : std::views::zip(field_descriptors, vtable_members)) {
            if (desc.kind == field_kind::base) {
                bases.emplace_back(
                    meta::type_of(desc.spec),
                    meta::substitute(
                        ^^automatic_base_converter,
                        {
                            meta::reflect_constant(desc.spec),
                            meta::reflect_constant(member),
                        }));
            }
            if (desc.kind == field_kind::method) {
                methods.emplace_back(
                    static_string::make(meta::identifier_of(desc.spec)),
                    meta::substitute(
                        ^^automatic_method_invoker,
                        {
                            meta::reflect_constant(desc.spec),
                            meta::reflect_constant(member),
                        }));
            }
        }
        return direct_members_t{
            std::define_static_array(bases),
            std::define_static_array(methods),
        };
    }();

    template<class Impl>
    struct vtable_for {
        using typed_facets_type = typed_facets<Impl, Face>;

        template<meta::info Base,
                 class Type = typename[:meta::type_of(Base):]>
            requires(has_vtable_for<Impl, Type>)
        struct base_pointer {
            static constexpr vtable_t<Type> const *value = vtable_for_v<Impl, Type>;
        };

        template<meta::info Base>
            requires requires { base_pointer<Base>::value; }
        static constexpr auto base_pointer_v = base_pointer<Base>::value;

        template<meta::info Method,
                 class Sig = typename[:method_signature(Method):]>
        struct method_pointer;

        template<meta::info Method, class R, class... A>
        struct method_pointer<Method, R(A...)> {
            static consteval bool is_qualified_name(meta::info candidate) {
                return meta::has_identifier(candidate) &&
                       meta::identifier_of(candidate) == meta::identifier_of(Method);
            }

            template<meta::info Candidate>
                requires requires(typed_facets_type typed_self, A... args) {
                    { [:Candidate:](typed_self, std::forward<A>(args)...) } -> std::convertible_to<R>;
                }
            static constexpr auto static_dispatcher = [](typed_facets_type typed_self, A... args) -> R {
                return [:Candidate:](typed_self, std::forward<A>(args)...);
            };

            static consteval bool is_static_exact(meta::info candidate) {
                auto candidate_params = meta::parameters_of(candidate);
                auto method_params = meta::parameters_of(Method);
                std::size_t i = 0;
                std::size_t j = 0;
                if (i >= candidate_params.size() ||
                    !meta::is_same_type(
                        meta::type_of(candidate_params[i++]),
                        ^^typed_facets_type)) {
                    return false;
                }
                while (i < candidate_params.size() && j < method_params.size()) {
                    if (!meta::is_same_type(
                            meta::type_of(candidate_params[i++]),
                            meta::type_of(method_params[j++]))) {
                        return false;
                    }
                }
                return i == candidate_params.size() && j == method_params.size();
            }

            template<meta::info Candidate>
                requires requires(typed_facets_type typed_self, A... args) {
                    { typed_self.get()->[:Candidate:](std::forward<A>(args)...) } -> std::convertible_to<R>;
                }
            static constexpr auto member_dispatcher = [](typed_facets_type typed_self, A... args) -> R {
                return typed_self.get()->[:Candidate:](std::forward<A>(args)...);
            };

            static consteval bool is_member_exact(meta::info candidate) {
                auto candidate_params = meta::parameters_of(candidate);
                auto method_params = meta::parameters_of(Method);
                std::size_t i = 0;
                std::size_t j = 0;
                while (i < candidate_params.size() && j < method_params.size()) {
                    if (!meta::is_same_type(
                            meta::type_of(candidate_params[i++]),
                            meta::type_of(method_params[j++]))) {
                        return false;
                    }
                }
                return i == candidate_params.size() && j == method_params.size();
            }

            static constexpr meta::info dispatcher = [] -> meta::info {
                auto dispatcher = get_method_dispatcher(Method);
                if (dispatcher != meta::info{}) {
                    return dispatcher;
                }

                std::vector<meta::info> static_dispatchers;
                for (meta::info candidate : static_candidates_v<Impl>) {
                    if (is_qualified_name(candidate)) {
                        if (meta::can_substitute(^^static_dispatcher, {meta::reflect_constant(candidate)})) {
                            auto dispatcher = meta::substitute(^^static_dispatcher, {meta::reflect_constant(candidate)});
                            if (is_static_exact(candidate)) {
                                return dispatcher;
                            } else {
                                static_dispatchers.push_back(dispatcher);
                            }
                        }
                    }
                }

                std::vector<meta::info> member_dispatchers;
                for (meta::info candidate : member_candidates_v<Impl>) {
                    if (is_qualified_name(candidate)) {
                        if (meta::can_substitute(^^member_dispatcher, {meta::reflect_constant(candidate)})) {
                            auto dispatcher = meta::substitute(^^member_dispatcher, {meta::reflect_constant(candidate)});
                            if (is_member_exact(candidate)) {
                                return dispatcher;
                            } else {
                                member_dispatchers.push_back(dispatcher);
                            }
                        }
                    }
                }

                for (meta::info dispatcher : static_dispatchers) {
                    return dispatcher;
                }

                for (meta::info dispatcher : member_dispatchers) {
                    return dispatcher;
                }

                return {};
            }();

            static R call(facets_type self, A... args)
                requires(dispatcher != meta::info{}) && requires(typed_facets_type typed_self, A... args) {
                    [:dispatcher:](typed_self, std::forward<A>(args)...);
                }
            {
                typed_facets_type typed_self(self);
                return [:dispatcher:](typed_self, std::forward<A>(args)...);
            }
        };

        template<meta::info Method>
            requires requires { &method_pointer<Method>::call; }
        static constexpr auto method_pointer_v = &method_pointer<Method>::call;

        static constexpr bool has_value = [] {
            for (auto desc : field_descriptors) {
                if (desc.kind == field_kind::base) {
                    if (!meta::can_substitute(
                            ^^base_pointer_v,
                            {
                                meta::reflect_constant(desc.spec),
                            })) {
                        return false;
                    }
                }
                if (desc.kind == field_kind::method) {
                    if (!meta::can_substitute(
                            ^^method_pointer_v,
                            {
                                meta::reflect_constant(desc.spec),
                            })) {
                        return false;
                    }
                }
            }
            return true;
        }();

        static constexpr vtable_type const *value = [] {
            std::vector<meta::info> members;
            for (auto desc : field_descriptors) {
                if (desc.kind == field_kind::base) {
                    members.push_back(
                        meta::substitute(
                            ^^base_pointer_v,
                            {
                                meta::reflect_constant(desc.spec),
                            }));
                }
                if (desc.kind == field_kind::method) {
                    members.push_back(
                        meta::substitute(
                            ^^method_pointer_v,
                            {
                                meta::reflect_constant(desc.spec),
                            }));
                }
            }
            return meta::extract<vtable_type const *>(meta::substitute(^^value_with, members));
        }();
    };
};
}

template<detail::is_spec Face>
struct vtable<Face> {
    using type = typename detail::from_spec_def<Face>::vtable_type;
};

template<detail::is_spec Face>
struct direct_methods<Face> {
    static constexpr auto const &value = detail::from_spec_def<Face>::direct_members.methods;
};

template<detail::is_spec Face>
struct direct_bases<Face> {
    static constexpr auto const &value = detail::from_spec_def<Face>::direct_members.bases;
};

template<class Impl, detail::is_spec Face>
    requires(detail::from_spec_def<Face>::template vtable_for<Impl>::has_value)
struct vtable_for<Impl, Face> {
    static constexpr vtable_t<Face> const *value = detail::from_spec_def<Face>::template vtable_for<Impl>::value;
};
}
