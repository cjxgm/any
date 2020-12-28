#pragma once
// An improved std::any implementation.
//
// See also:
//   + http://en.cppreference.com/w/cpp/any
//
// This nonstd::any is different from std::any, in which:
//   + Allow constructing non-copyable non-movable types in-place via `emplace<T>(args...)`.
//   + Allow constructing non-copyable types. Copying an any containing such type results in bad_any_copy exception.
//   + Allow constructing non-movable types. These types are constructed via an indirect pointer. Moving an any containing such type results in moving the indirection pointer.
//   + Moving an any will move the contained type by value with best effort, regardless the use of an indirection pointer. If not possible, the indirection pointer will be moved.
//   + Moving an any is always noexcept. Assumes all types are noexcept movable, even if they are not.
//   + Moving an any leaves the original object in empty state (i.e. always clears the moved-out object).
//
// Copyright (C) Giumo Clanjor (哆啦比猫/兰威举), 2020.
// Licensed under the MIT License.
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>
#include <utility>      // for std::forward and std::move
#include <memory>
#include <stdexcept>
#include <new>

namespace nonstd
{
    struct bad_any_cast;
    struct bad_any_copy;
    struct any;

    namespace any_detail
    {
        inline constexpr auto internal_storage_size = 16;       // two 64-bit pointers
        inline constexpr auto internal_storage_alignment = 16;

        static_assert(internal_storage_size >= int(sizeof(std::unique_ptr<int>)));
        static_assert(internal_storage_alignment >= int(alignof(std::unique_ptr<int>)));

        // A trait to disable moving and copying.
        struct pinned
        {
            pinned() = default;
            pinned(pinned const&) = delete;
            pinned(pinned     &&) = delete;
            auto operator = (pinned const&) -> pinned& = delete;
            auto operator = (pinned     &&) -> pinned& = delete;
        };

        // A trait to enable OOP-usage.
        struct interface: pinned
        {
            virtual ~interface() = default;
        };

        template <class T>
        inline constexpr bool is_any = std::is_same_v<std::decay_t<T>, any>;

        template <class T>
        inline constexpr bool is_copyable = (
            true
            && std::is_copy_constructible<T>::value
            && std::is_copy_assignable<T>::value
        );

        template <class T>
        inline constexpr bool is_copyable<T const> = is_copyable<T>;

        template <class T, class Alloc>
        inline constexpr bool is_copyable<std::vector<T, Alloc>> = is_copyable<T>;

        template <class T>
        inline constexpr bool is_movable = (
            true
            && std::is_move_constructible<T>::value
            && std::is_move_assignable<T>::value
        );

        template <class T>
        inline constexpr bool is_movable<T const> = false;

        template <class T, class Alloc>
        inline constexpr bool is_movable<std::vector<T, Alloc>> = is_movable<T>;

        template <class T>
        inline constexpr bool can_store_internally = (
            true
            && is_movable<T>
            && int(sizeof(T)) <= internal_storage_size
            && int(alignof(T)) <= internal_storage_alignment
        );
    }

    struct bad_any_cast final: std::bad_cast
    {
        auto what() const noexcept -> char const* override
        {
            return "bad any cast";
        }
    };

    struct bad_any_copy final: std::bad_cast
    {
        auto what() const noexcept -> char const* override
        {
            return "bad any copy";
        }
    };

    struct any final
    {
        ~any() noexcept { clear(); }
        any() noexcept {}
        any(any const& x) { if (x.model) { x.model->copy_construct(x.storage, storage); model = x.model; } }
        any(any&& x) noexcept { if (x.model && this != &x) { x.model->move_construct(x.storage, storage); model = x.model; x.clear(); } }

        auto operator = (any const& x) -> any&
        {
            if (x.model) {
                if (model == x.model) {
                    model->copy_assign(x.storage, storage);
                } else {
                    clear();
                    x.model->copy_construct(x.storage, storage);
                    model = x.model;
                }
            } else {
                clear();
            }

            return *this;
        }

        auto operator = (any&& x) noexcept -> any& {
            if (this != &x) {
                if (x.model) {
                    if (model == x.model) {
                        model->move_assign(x.storage, storage);
                    } else {
                        clear();
                        x.model->move_construct(x.storage, storage);
                        model = x.model;
                    }
                    x.clear();
                } else {
                    clear();
                }
            }

            return *this;
        }

        // Q: Why not take T by value here?
        // A: If taking T by value, calling `any{std::cref(var_of_type_any)}`
        // will invoke the copy constructor instead of the constructor below.
        template <class T, class = std::enable_if_t<!any_detail::is_any<T>>>
        any(T&& x) noexcept
        {
            construct(std::move(x));
        }

        template <class T, class = std::enable_if_t<!any_detail::is_any<T>>>
        auto operator = (T&& x) noexcept -> any&
        {
            return (*this = any{std::move(x)});
        }

        auto clear() noexcept -> void
        {
            if (model) {
                model->free(storage);
                model = nullptr;
            }
        }

        auto empty() const noexcept -> bool
        {
            return (model == nullptr);
        }

        auto type() const noexcept -> std::type_index
        {
            return (model ? model->type() : std::type_index{typeid(void)});
        }

        template <class T, class... Args>
        auto emplace(Args&&... args) noexcept(noexcept(std::decay_t<T>(std::forward<Args>(args)...))) -> void
        {
            using U = std::decay_t<T>;

            clear();

            if constexpr (any_detail::can_store_internally<U>) {
                new (storage) U(std::forward<Args>(args)...);
            } else {
                auto p = std::make_unique<U>(std::forward<Args>(args)...);
                new (storage) std::unique_ptr<U>{std::move(p)};
            }

            // model must be assigned AFTER the creation of storage
            // for exception safety reasons.
            model = model_of<U>();
        }

        template <class T>
        auto construct(T x) noexcept(noexcept(emplace<std::decay_t<T>>(std::move(x)))) -> void
        {
            using U = std::decay_t<T>;

            emplace<U>(std::move(x));
        }

        template <class T>
        auto try_cast() const noexcept -> std::decay_t<T> const*
        {
            using U = std::decay_t<T>;

            if (model == nullptr)
                return nullptr;

            // Merging of data structures are not reliable across translation units. Even the type_info structure may not be merged.
            if (model != model_of<U>() && model->type() != typeid(U))
                return nullptr;

            if constexpr (any_detail::can_store_internally<U>) {
                auto x = std::launder(reinterpret_cast<U const*>(storage));
                return x;
            } else {
                auto x = std::launder(reinterpret_cast<std::unique_ptr<U const> const*>(storage));
                return x->get();
            }
        }

        template <class T>
        auto try_cast() noexcept -> std::remove_reference_t<T>*
        {
            using U = std::decay_t<T>;

            if (model == nullptr)
                return nullptr;

            // Merging of data structures are not reliable across translation units. Even the type_info structure may not be merged.
            if (model != model_of<U>() && model->type() != typeid(U))
                return nullptr;

            if constexpr (any_detail::can_store_internally<U>) {
                auto x = std::launder(reinterpret_cast<U*>(storage));
                return x;
            } else {
                auto x = std::launder(reinterpret_cast<std::unique_ptr<U>*>(storage));
                return x->get();
            }
        }

        template <class T>
        auto cast() & -> decltype(auto)
        {
            if (auto x = try_cast<T>())
                return *x;
            throw bad_any_cast{};
        }

        template <class T>
        auto cast() const& -> decltype(auto)
        {
            if (auto x = try_cast<T>())
                return *x;
            throw bad_any_cast{};
        }

        template <class T>
        auto cast() && -> auto
        {
            if (auto x = try_cast<T>())
                return std::move(*x);
            throw bad_any_cast{};
        }

    private:
        struct storage_model;

        alignas(any_detail::internal_storage_alignment) char storage[any_detail::internal_storage_size];
        storage_model const* model{};

        struct storage_model: any_detail::interface
        {
            virtual auto type() const noexcept -> std::type_index = 0;
            virtual auto free(char* storage) const noexcept -> void = 0;
            virtual auto copy_construct(char const* src, char* dst) const -> void = 0;      // assumes src is initialized, while dst is uninitialized.
            virtual auto move_construct(char* src, char* dst) const noexcept -> void = 0;   // assumes src is initialized, while dst is uninitialized.
            virtual auto copy_assign(char const* src, char* dst) const -> void = 0;         // assumes both src and dst are initialized.
            virtual auto move_assign(char* src, char* dst) const noexcept -> void = 0;      // assumes both src and dst are initialized.
        };

        template <class T>
        struct internal_storage final: storage_model
        {
            auto type() const noexcept -> std::type_index override
            {
                return typeid(T);
            }

            auto free(char* storage) const noexcept -> void override
            {
                auto x = std::launder(reinterpret_cast<T*>(storage));
                x->~T();
            }

            auto copy_construct(char const* src, char* dst) const -> void override
            {
                if constexpr (any_detail::is_copyable<T>) {
                    auto s = std::launder(reinterpret_cast<T const*>(src));
                    new (dst) T(*s);
                } else {
                    throw bad_any_copy{};
                }
            }

            auto move_construct(char* src, char* dst) const noexcept -> void override
            {
                auto s = std::launder(reinterpret_cast<T*>(src));
                new (dst) T(std::move(*s));
            }

            auto copy_assign(char const* src, char* dst) const -> void override
            {
                if constexpr (any_detail::is_copyable<T>) {
                    auto s = std::launder(reinterpret_cast<T const*>(src));
                    auto d = std::launder(reinterpret_cast<T*>(dst));
                    *d = *s;
                } else {
                    throw bad_any_copy{};
                }
            }

            auto move_assign(char* src, char* dst) const noexcept -> void override
            {
                auto s = std::launder(reinterpret_cast<T*>(src));
                auto d = std::launder(reinterpret_cast<T*>(dst));
                *d = std::move(*s);
            }
        };

        template <class T>
        struct external_storage final: storage_model
        {
            auto type() const noexcept -> std::type_index override
            {
                return typeid(T);
            }

            auto free(char* storage) const noexcept -> void override
            {
                using P = std::unique_ptr<T>;
                auto x = std::launder(reinterpret_cast<P*>(storage));
                x->~P();
            }

            auto copy_construct(char const* src, char* dst) const -> void override
            {
                if constexpr (any_detail::is_copyable<T>) {
                    using P = std::unique_ptr<T>;
                    auto s = std::launder(reinterpret_cast<P const*>(src));
                    new (dst) std::unique_ptr<T>{std::make_unique<T>(**s)};
                } else {
                    throw bad_any_copy{};
                }
            }

            auto move_construct(char* src, char* dst) const noexcept -> void override
            {
                using P = std::unique_ptr<T>;
                auto s = std::launder(reinterpret_cast<P*>(src));
                new (dst) std::unique_ptr<T>{std::move(*s)};
            }

            auto copy_assign(char const* src, char* dst) const -> void override
            {
                if constexpr (any_detail::is_copyable<T>) {
                    using P = std::unique_ptr<T>;
                    auto s = std::launder(reinterpret_cast<P const*>(src));
                    auto d = std::launder(reinterpret_cast<P*>(dst));
                    **d = **s;
                } else {
                    throw bad_any_copy{};
                }
            }

            auto move_assign(char* src, char* dst) const noexcept -> void override
            {
                using P = std::unique_ptr<T>;
                auto s = std::launder(reinterpret_cast<P*>(src));
                auto d = std::launder(reinterpret_cast<P*>(dst));
                if constexpr (any_detail::is_movable<T>) {
                    **d = std::move(**s);
                } else {
                    *d = std::move(*s);
                }
            }
        };

        template <class T>
        auto model_of() const noexcept -> storage_model const*
        {
            using U = std::decay_t<T>;

            if constexpr (any_detail::can_store_internally<U>) {
                static auto m = internal_storage<U>{};
                return &m;
            } else {
                static auto m = external_storage<U>{};
                return &m;
            }
        }
    };

    template <class T>
    auto any_cast(any const& x) -> decltype(auto)
    {
        return x.cast<T>();
    }

    template <class T>
    auto any_cast(any& x) -> decltype(auto)
    {
        return x.cast<T>();
    }

    template <class T>
    auto any_cast(any&& x) -> auto
    {
        return std::move(x).cast<T>();
    }

    template <class T>
    auto any_cast(any const* x) noexcept -> auto
    {
        return x->try_cast<T>();
    }

    template <class T>
    auto any_cast(any* x) noexcept -> auto
    {
        return x->try_cast<T>();
    }
}

