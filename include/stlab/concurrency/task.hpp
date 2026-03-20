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

/** @defgroup stlab_concurrency_task task
 *  @ingroup stlab_concurrency
 *  @brief Move-only callable wrapper for executor scheduling (`task<Signature>`).
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

/*
    tasks are functions with a mutable call operator to support moving items through for single
    invocations.
*/

template <bool NoExcept, class R, class... Args>
class task_ {
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

    struct concept_t {
        void (*dtor)(void*) noexcept;
        void (*move_ctor)(void*, void*) noexcept;
        const std::type_info& (*target_type)() noexcept;
        void* (*pointer)(void*) noexcept;
        const void* (*const_pointer)(const void*) noexcept;
    };

    using invoke_t = R (*)(void*, Args...) noexcept(NoExcept);

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

    static constexpr size_t max_align = alignof(std::max_align_t);
    static constexpr size_t small_size =
        std::max(max_align * 2, sizeof(void*) * 8) - std::max(max_align, sizeof(void*) * 2);

    const concept_t* _vtable_ptr = &_vtable;
    invoke_t _invoke = invoke;
    alignas(std::max_align_t) std::array<unsigned char, small_size> _model;

public:
    using result_type = R;

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
        using model_t = std::conditional_t<(sizeof(small_t) <= small_size) &&
                                               (alignof(small_t) <= alignof(decltype(_model))),
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

template <class F>
using task = typename noexcept_deducer<task_, F>::type;

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif

/**************************************************************************************************/
