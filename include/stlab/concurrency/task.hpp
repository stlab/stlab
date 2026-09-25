/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_TASK_HPP
#define STLAB_CONCURRENCY_TASK_HPP

/*! @file task.hpp
 *  @brief Move-only callable wrapper for executor scheduling (`task<Signature>`).
 *
 *  @details
 *  `task<F>` type-erases any callable target (function, lambda, `std::bind`, member pointer, etc.)
 *  with a fixed signature. It is similar to `std::function` but **not copyable**, which suits
 *  move-only and single-shot targets (common in messaging and executor queues). An empty task
 *  compares equal to `nullptr`; invoking it throws `std::bad_function_call`.
 *
 *  Mutable `operator()` allows moving arguments through for one invocation. Small targets (function
 *  pointers, `std::reference_wrapper`, `std::function`) may use small-buffer optimization; larger
 *  callables are heap-allocated.
 */

/**************************************************************************************************/

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <stlab/config.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_task task
 *  @ingroup stlab_concurrency
 *  @brief Move-only callable wrapper for executor scheduling (`task<Signature>`).
 *
 *  @details
 *  Use the `task` alias (`task<F>` deduces `noexcept` from the signature). See `task.hpp`.
 *  @{
 */

/**************************************************************************************************/

/// Versioned operation table used to relocate a task target across the shared executor ABI.
struct stlab_v2_task_concept {
    using dtor_t = void (*)(void*) noexcept;
    using move_ctor_t = void (*)(void*, void*) noexcept;
    using target_type_t = const std::type_info& (*)() noexcept;
    using pointer_t = void* (*)(void*) noexcept;
    using const_pointer_t = const void* (*)(const void*) noexcept;

    dtor_t dtor;
    move_ctor_t move_ctor;
    target_type_t target_type;
    pointer_t pointer;
    const_pointer_t const_pointer;
};

/// Number of bytes reserved for a v2 task's relocatable inline model.
inline constexpr std::size_t stlab_v2_task_storage_size =
    std::max(alignof(std::max_align_t) * 2, sizeof(void*) * 8) -
    std::max(alignof(std::max_align_t), sizeof(void*) * 2);

/// Alignment required by a v2 task's relocatable inline model.
inline constexpr std::size_t stlab_v2_task_storage_alignment = alignof(std::max_align_t);

namespace detail {

/// Linker guard keyed by the v2 task relocation storage contract.
template <std::size_t Size, std::size_t Alignment>
struct task_storage_abi_guard {
    STLAB_CORE_API static const unsigned char value;
};

/// Guard specialization required by the current v2 task relocation storage contract.
using current_task_storage_abi_guard =
    task_storage_abi_guard<stlab_v2_task_storage_size, stlab_v2_task_storage_alignment>;

/// Declares the current ABI guard specialization before any inline wrapper references its member.
template <>
struct task_storage_abi_guard<stlab_v2_task_storage_size, stlab_v2_task_storage_alignment> {
    STLAB_CORE_API static const unsigned char value;
};

/// Rounds `offset` up to the next address satisfying `alignment`.
constexpr auto stlab_v2_align_offset(std::size_t offset, std::size_t alignment) noexcept
    -> std::size_t {
    return (offset + alignment - 1) / alignment * alignment;
}

inline constexpr auto stlab_v2_task_concept_move_ctor_offset = stlab_v2_align_offset(
    sizeof(stlab_v2_task_concept::dtor_t), alignof(stlab_v2_task_concept::move_ctor_t));
inline constexpr auto stlab_v2_task_concept_target_type_offset = stlab_v2_align_offset(
    stlab_v2_task_concept_move_ctor_offset + sizeof(stlab_v2_task_concept::move_ctor_t),
    alignof(stlab_v2_task_concept::target_type_t));
inline constexpr auto stlab_v2_task_concept_pointer_offset = stlab_v2_align_offset(
    stlab_v2_task_concept_target_type_offset + sizeof(stlab_v2_task_concept::target_type_t),
    alignof(stlab_v2_task_concept::pointer_t));
inline constexpr auto stlab_v2_task_concept_const_pointer_offset = stlab_v2_align_offset(
    stlab_v2_task_concept_pointer_offset + sizeof(stlab_v2_task_concept::pointer_t),
    alignof(stlab_v2_task_concept::const_pointer_t));
inline constexpr auto stlab_v2_task_concept_alignment = std::max(
    {alignof(stlab_v2_task_concept::dtor_t), alignof(stlab_v2_task_concept::move_ctor_t),
     alignof(stlab_v2_task_concept::target_type_t), alignof(stlab_v2_task_concept::pointer_t),
     alignof(stlab_v2_task_concept::const_pointer_t)});

} // namespace detail

// An incompatible operation-table change requires a new versioned type instead of relaxing the v2
// canaries below to accept a different layout.
static_assert(std::is_standard_layout_v<stlab_v2_task_concept>);
static_assert(alignof(stlab_v2_task_concept) == detail::stlab_v2_task_concept_alignment);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::dtor), stlab_v2_task_concept::dtor_t>);
static_assert(
    std::is_same_v<decltype(stlab_v2_task_concept::move_ctor), stlab_v2_task_concept::move_ctor_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::target_type),
                             stlab_v2_task_concept::target_type_t>);
static_assert(
    std::is_same_v<decltype(stlab_v2_task_concept::pointer), stlab_v2_task_concept::pointer_t>);
static_assert(std::is_same_v<decltype(stlab_v2_task_concept::const_pointer),
                             stlab_v2_task_concept::const_pointer_t>);
static_assert(offsetof(stlab_v2_task_concept, dtor) == 0);
static_assert(offsetof(stlab_v2_task_concept, move_ctor) ==
              detail::stlab_v2_task_concept_move_ctor_offset);
static_assert(offsetof(stlab_v2_task_concept, target_type) ==
              detail::stlab_v2_task_concept_target_type_offset);
static_assert(offsetof(stlab_v2_task_concept, pointer) ==
              detail::stlab_v2_task_concept_pointer_offset);
static_assert(offsetof(stlab_v2_task_concept, const_pointer) ==
              detail::stlab_v2_task_concept_const_pointer_offset);
static_assert(sizeof(stlab_v2_task_concept) ==
              detail::stlab_v2_align_offset(detail::stlab_v2_task_concept_const_pointer_offset +
                                                sizeof(stlab_v2_task_concept::const_pointer_t),
                                            alignof(stlab_v2_task_concept)));

/**************************************************************************************************/

/// Type-erased, move-only callable with signature `R(Args...)` (or `noexcept` variant).
///
/// @details
/// Mutable `operator()` supports moving arguments through for single invocations. Compare to
/// `nullptr` when empty; use `target()` / `target_type()` for runtime type queries.
template <bool NoExcept, class R, class... Args>
class task_ {
public:
    /// Stable vtable describing how to move-construct, destroy, and introspect a task's target,
    /// independent of the target's concrete type `F`. Used to relocate a task's target across an
    /// ABI boundary (see `relocate()`) without depending on `F` or re-boxing the target.
    using concept_t = stlab_v2_task_concept;

    /// Function used to invoke a task's target, valid for as long as the target is live.
    using invoke_t = R (*)(void*, Args...) noexcept(NoExcept);

private:
    template <class F>
    constexpr static bool maybe_empty =
        std::is_pointer_v<std::decay_t<F>> || std::is_member_pointer_v<std::decay_t<F>> ||
        std::is_same_v<std::function<R(Args...)>, std::decay_t<F>>;

    template <class F>
    constexpr static auto is_empty(const F& f) -> std::enable_if_t<maybe_empty<F>, bool> {
        return !f;
    }

    template <class F>
    constexpr static auto is_empty(const F&) -> std::enable_if_t<!maybe_empty<F>, bool> {
        return false;
    }

    template <class F, bool Small>
    struct model;

    template <class F>
    struct model<F, true> {
        template <class G> // for forwarding
        model(G&& f) : _f(std::forward<G>(f)) {}
        model(model&&) noexcept = delete;

        static void dtor(void* self) noexcept { static_cast<model*>(self)->~model(); }
        static void move_ctor(void* self, void* p) noexcept {
            new (p) model(std::move(static_cast<model*>(self)->_f));
        }

        /*
            NOTE (sean-parent): `Args` are _not_ universal references. This is a `concrete`
            interface for the model. Do not add `&&`, that would make it an rvalue reference.
            The `forward<Args>` here is correct. We are forwarding from the client defined
            signature to the actual captured model.
        */

        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        static auto invoke(void* self, Args... args) noexcept(NoExcept) -> R {
            return (static_cast<model*>(self)->_f)(std::forward<Args>(args)...);
        }

        static auto target_type() noexcept -> const std::type_info& { return typeid(F); }
        static auto pointer(void* self) noexcept -> void* { return &static_cast<model*>(self)->_f; }
        static auto const_pointer(const void* self) noexcept -> const void* {
            return &static_cast<const model*>(self)->_f;
        }
#if defined(__GNUC__) && __GNUC__ < 7 && !defined(__clang__)
        static const concept_t _vtable;
        static const invoke_t _invoke;
#else
        static constexpr concept_t _vtable = {dtor, move_ctor, target_type, pointer, const_pointer};
        static constexpr invoke_t _invoke = invoke;
#endif
        F _f;
    };

    template <class F>
    struct model<F, false> {
        template <class G> // for forwarding
        model(G&& f) : _p(std::make_unique<F>(std::forward<G>(f))) {}
        model(model&&) noexcept = default;

        static void dtor(void* self) noexcept { static_cast<model*>(self)->~model(); }
        static void move_ctor(void* self, void* p) noexcept {
            new (p) model(std::move(*static_cast<model*>(self)));
        }

        /*
            NOTE (sean-parent): `Args` are _not_ universal references. This is a `concrete`
            interface for the model. Do not add `&&`, that would make it an rvalue reference.
            The `forward<Args>` here is correct. We are forwarding from the client defined
            signature to the actual captured model.
        */

        static auto invoke(void* self, Args... args) noexcept(NoExcept) -> R {
            return (*static_cast<model*>(self)->_p)(std::forward<Args>(args)...);
        }

        static auto target_type() noexcept -> const std::type_info& { return typeid(F); }
        static auto pointer(void* self) noexcept -> void* {
            return static_cast<model*>(self)->_p.get();
        }
        static auto const_pointer(const void* self) noexcept -> const void* {
            return static_cast<const model*>(self)->_p.get();
        }

#if defined(__GNUC__) && __GNUC__ < 7 && !defined(__clang__)
        static const concept_t _vtable;
        static const invoke_t _invoke;
#else
        static constexpr concept_t _vtable = {dtor, move_ctor, target_type, pointer, const_pointer};
        static constexpr invoke_t _invoke = invoke;
#endif

        std::unique_ptr<F> _p;
    };

    // empty (default) vtable
    static void dtor(void*) noexcept {}
    static void move_ctor(void*, void*) noexcept {}
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    static auto invoke(void*, Args...) noexcept(NoExcept) -> R {
        if constexpr (NoExcept) {
            try {
                throw std::bad_function_call();
            } catch (...) {
                std::terminate();
            }
        } else {
            throw std::bad_function_call();
        }
    }
    static auto target_type_() noexcept -> const std::type_info& { return typeid(void); }
    static auto pointer(void*) noexcept -> void* { return nullptr; }
    static auto const_pointer(const void*) noexcept -> const void* { return nullptr; }

#if defined(__GNUC__) && __GNUC__ < 7 && !defined(__clang__)
    static const concept_t _vtable;
#else
    static constexpr concept_t _vtable = {dtor, move_ctor, target_type_, pointer, const_pointer};
#endif

    /*
    The layout of this object is going to be:
        _vtable_ptr
        _invoke
        _model

    Because the model is going to be max-aligned, it will leave a gap between the vtable_ptr and
    the _model, we fill that gap by lifting the _invoke pointer into it from the vtable. This means
    invoke calls require one less indirection.

    The size of the model is big enough for 6 pointers or (2 max aligned object - 2 pointers)
    (which would typically be 2 pointers). The rational here is that we want the total object size
    to be a power of 2, for the object but the model store to be large enough to hold 2
    weak-pointers (which is 4 pointers total), so we give things a little extra room.
    */

    const concept_t* _vtable_ptr = &_vtable;
    invoke_t _invoke = invoke;
    alignas(stlab_v2_task_storage_alignment)
        std::array<unsigned char, stlab_v2_task_storage_size> _model;

public:
    using result_type = R;

    /// Returns the vtable needed to relocate, invoke, or destroy this task's target without
    /// depending on its concrete type.
    [[nodiscard]] auto relocation_concept() const noexcept -> const concept_t* {
        return _vtable_ptr;
    }

    /// Returns the function used to invoke this task's target.
    [[nodiscard]] auto relocation_invoke() const noexcept -> invoke_t { return _invoke; }

    /// Returns the address of this task's inline model storage.
    ///
    /// - Precondition: valid only until `*this` is destroyed, reassigned, or moved from.
    [[nodiscard]] auto relocation_source() noexcept -> void* { return &_model; }

    /// Constructs a task by relocating the target described by (`vtable_ptr`, `invoke_fn`) out of
    /// `source`, without depending on the target's concrete type.
    ///
    /// - Precondition: `source` is the `relocation_source()` of a live task sharing the same
    ///   `vtable_ptr`/`invoke_fn`.
    /// - Postcondition: the target at `source` is left moved-from; the source task must still be
    ///   destroyed normally (it is not emptied by this call).
    task_(const concept_t* vtable_ptr, invoke_t invoke_fn, void* source) noexcept :
        _vtable_ptr(vtable_ptr), _invoke(invoke_fn) {
        vtable_ptr->move_ctor(source, &_model);
    }

    constexpr task_() noexcept = default;
    constexpr task_(std::nullptr_t) noexcept : task_() {}
    task_(const task_&) = delete;
    task_(const task_&&) = delete;
    task_(task_&& x) noexcept : _vtable_ptr(x._vtable_ptr), _invoke(x._invoke) {
        _vtable_ptr->move_ctor(&x._model, &_model);
    }

    template <class F,
              std::enable_if_t<!NoExcept || std::is_nothrow_invocable_v<F, Args...>, bool> = true>
    task_(F&& f) {
        using small_t = model<std::decay_t<F>, true>;
        using large_t = model<std::decay_t<F>, false>;
        using model_t =
            std::conditional_t<(sizeof(small_t) <= stlab_v2_task_storage_size) &&
                                   (alignof(small_t) <= stlab_v2_task_storage_alignment),
                               small_t, large_t>;

        if (is_empty(f)) return;

        new (&_model) model_t(std::forward<F>(f));
        _vtable_ptr = &model_t::_vtable;
        _invoke = &model_t::invoke;
    }

    ~task_() { _vtable_ptr->dtor(&_model); };

    auto operator=(const task_&) -> task_& = delete;

    auto operator=(task_&& x) noexcept -> task_& {
        _vtable_ptr->dtor(&_model);
        _vtable_ptr = x._vtable_ptr;
        _invoke = x._invoke;
        _vtable_ptr->move_ctor(&x._model, &_model);
        return *this;
    }

    auto operator=(std::nullptr_t) noexcept -> task_& { return *this = task_(); }

    template <class F>
    auto operator=(F&& f)
        -> std::enable_if_t<!NoExcept || std::is_nothrow_invocable_v<decltype(f), Args...>,
                            task_&> {
        return *this = task_(std::forward<F>(f));
    }

    void swap(task_& x) noexcept { std::swap(*this, x); }

    explicit operator bool() const { return _vtable_ptr->const_pointer(&_model) != nullptr; }

    [[nodiscard]] auto target_type() const noexcept -> const std::type_info& {
        return _vtable_ptr->target_type();
    }

    template <class T>
    auto target() -> T* {
        return (target_type() == typeid(T)) ? static_cast<T*>(_vtable_ptr->pointer(&_model)) :
                                              nullptr;
    }

    template <class T>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] auto target() const -> const T* {
        return (target_type() == typeid(T)) ?
                   static_cast<const T*>(_vtable_ptr->const_pointer(&_model)) :
                   nullptr;
    }

    template <class... Brgs>
    auto operator()(Brgs&&... brgs) noexcept(NoExcept) {
        return _invoke(&_model, std::forward<Brgs>(brgs)...);
    }

    friend inline void swap(task_& x, task_& y) noexcept { return x.swap(y); }
    friend inline auto operator==(const task_& x, std::nullptr_t) -> bool {
        return !static_cast<bool>(x);
    }
    friend inline auto operator==(std::nullptr_t, const task_& x) -> bool {
        return !static_cast<bool>(x);
    }
    friend inline auto operator!=(const task_& x, std::nullptr_t) -> bool {
        return static_cast<bool>(x);
    }
    friend inline auto operator!=(std::nullptr_t, const task_& x) -> bool {
        return static_cast<bool>(x);
    }
};

/**************************************************************************************************/

template <template <bool, class, class...> class T, class F>
struct noexcept_deducer;

template <template <bool, class, class...> class T, class R, class... Args>
struct noexcept_deducer<T, R(Args...)> {
    using type = T<false, R, Args...>;
};

template <template <bool, class, class...> class T, class R, class... Args>
struct noexcept_deducer<T, R(Args...) noexcept> {
    using type = T<true, R, Args...>;
};

/// `task_` with `noexcept` deduced from the function type `F` (e.g. `void()` vs `void() noexcept`).
template <class F>
using task = typename noexcept_deducer<task_, F>::type;

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
