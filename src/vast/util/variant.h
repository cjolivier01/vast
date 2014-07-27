/*
Copyright (C) 2013 Jarryd Beck (adapated by Matthias Vallentin).

Distributed under the Boost Software License, Version 1.0

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

  The copyright notices in the Software and this entire statement, including
  the above license grant, this restriction and the following disclaimer,
  must be included in all copies of the Software, in whole or in part, and
  all derivative works of the Software, unless such copies or derivative
  works are solely in the form of machine-executable object code generated by
  a source language processor.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.

*/

#ifndef VAST_UTIL_VARIANT_H
#define VAST_UTIL_VARIANT_H

#include <cassert>
#include <type_traits>

#include "vast/config.h"
#include "vast/util/operators.h"
#include "vast/util/meta.h"

namespace vast {
namespace util {

template <typename T>
class recursive_wrapper : equality_comparable<recursive_wrapper<T>>
{
public:
  template <
    typename U,
    typename Dummy = std::enable_if_t<std::is_convertible<U, T>{}, U>
  >
  recursive_wrapper(U const& u)
    : x_(new T(u))
  {
  }

  template <
    typename U,
    typename Dummy = std::enable_if_t<std::is_convertible<U, T>{}, U>
  >
  recursive_wrapper(U&& u)
    : x_(new T(std::forward<U>(u)))
  {
  }

  ~recursive_wrapper()
  {
    delete x_;
  }

  recursive_wrapper(recursive_wrapper const& rhs)
    : x_(new T(rhs.get()))
  {
  }

  recursive_wrapper(recursive_wrapper&& rhs)
    : x_(rhs.x_)
  {
    rhs.x_ = nullptr;
  }

  recursive_wrapper& operator=(recursive_wrapper const& rhs)
  {
    assign(rhs.get());
    return *this;
  }

  recursive_wrapper& operator=(recursive_wrapper&& rhs)
  {
    delete x_;
    x_ = rhs.x_;
    rhs.x_ = nullptr;
    return *this;
  }

  recursive_wrapper& operator=(T const& x)
  {
    assign(x);
    return *this;
  }

  recursive_wrapper& operator=(T&& x)
  {
    assign(std::move(x));
    return *this;
  }

  T& get()
  {
    return *x_;
  }

  T const& get() const
  {
    return *x_;
  }

private:
  T* x_;

  template <typename U>
  void assign(U&& u)
  {
    *x_ = std::forward<U>(u);
  }

  friend bool operator==(recursive_wrapper const& x, recursive_wrapper const& y)
  {
    return *x.x_ == *y.x_;
  }
};

/// A variant class.
/// @tparam Tag The type of the discriminator. If this type is an enum value,
///     it must start at 0 and increment sequentially by 1.
/// @tparam Ts The types the variant should assume.
template <typename Tag, typename... Ts>
class basic_variant : equality_comparable<basic_variant<Tag, Ts...>>
{
#ifdef VAST_GCC
  // Workaround for http://stackoverflow.com/q/24433658/1170277.
  template <typename T, typename...>
  struct front_type
  {
    using type = T;
  };
#else
  template <typename T, typename...>
  using front_type = T;
#endif

public:
  /// The type of the variant discriminator.
  using tag_type = Tag;

  /// The first type in the variant; used for default-construction.
#ifdef VAST_GCC
  using front = typename front_type<Ts...>::type;
#else
  using front = front_type<Ts...>;
#endif

  /// Construct a variant from a type tag.
  /// @pre `0 <= tag < sizeof...(Ts)`
  static basic_variant make(tag_type tag)
  {
    return {factory{}, tag};
  }

  /// Default-constructs a variant with the first type.
  basic_variant() noexcept
  {
    construct(front{});
    which_ = tag_type{};
  }

  /// Destructs the variant by invoking the destructor of the active instance.
  ~basic_variant() noexcept
  {
    destruct();
  }

  /// Constructs a variant from one of the discrimanted types.
  ///
  /// @param x The value to construct the variant with. Note that *x* must be
  /// unambiguously convertible to one of the types in the variant.
  template <
    typename T,
    typename = std::enable_if<
      ! std::is_same<
        std::remove_reference_t<basic_variant>,
        std::remove_reference_t<T>
      >::value,
      T
    >
  >
  basic_variant(T&& x)
  {
    static_assert(! std::is_same<basic_variant&, T>{},
                  "should have been sfinaed out");

    // A compile error here means that T is not unambiguously convertible to
    // any of the variant types.
    initializer<0, Ts...>::initialize(*this, std::forward<T>(x));
  }

  basic_variant(basic_variant const& rhs)
  {
    rhs.apply_visitor_internal(copy_constructor(*this));
    which_ = rhs.which_;
  }

  basic_variant(basic_variant&& rhs) noexcept
  {
    rhs.apply_visitor_internal(move_constructor(*this));
    which_ = rhs.which_;
  }

  basic_variant& operator=(basic_variant const& rhs)
  {
    if (this != &rhs)
    {
      rhs.apply_visitor_internal(assigner(*this, rhs.which()));
      which_ = rhs.which_;
    }
    return *this;
  }

  basic_variant& operator=(basic_variant&& rhs) noexcept
  {
    if (this != &rhs)
    {
      rhs.apply_visitor_internal(move_assigner(*this, rhs.which()));
      which_ = rhs.which_;
    }
    return *this;
  }

  tag_type which() const
  {
    return which_;
  }

  template <typename Internal, typename Visitor, typename... Args>
  auto apply(Visitor&& visitor, Args&&... args)
  {
    return visit_impl(which_, Internal{}, storage_,
                      std::forward<Visitor>(visitor),
                      std::forward<Args>(args)...);
  }

  template <typename Internal, typename Visitor, typename... Args>
  auto apply(Visitor&& visitor, Args&&... args) const
  {
    return visit_impl(which_, Internal{}, storage_,
                      std::forward<Visitor>(visitor),
                      std::forward<Args>(args)...);
  }

  friend bool operator==(basic_variant const& x, basic_variant const& y)
  {
    return x.which_ == y.which_ && y.apply_visitor_internal(equality(x));
  }

private:
  struct default_constructor
  {
    default_constructor(basic_variant& self)
      : self_(self)
    {
    }

    template <typename T>
    void operator()(T const&) const
    {
      self_.construct(T());
    }

  private:
    basic_variant& self_;
  };

  struct copy_constructor
  {
    copy_constructor(basic_variant& self)
      : self_(self)
    {
    }

    template <typename T>
    void operator()(T const& rhs) const
    {
      self_.construct(rhs);
    }

  private:
    basic_variant& self_;
  };

  struct move_constructor
  {
    move_constructor(basic_variant& self)
      : self_(self)
    {
    }

    template <typename T>
    void operator()(T& rhs) const noexcept
    {
      static_assert(std::is_nothrow_move_constructible<T>{},
                    "T must not throw in move constructor");

      self_.construct(std::move(rhs));
    }

  private:
    basic_variant& self_;
  };

  struct assigner
  {
    assigner(basic_variant& self, tag_type rhs_which)
      : self_(self), rhs_which_(rhs_which)
    {
    }

    template <typename Rhs>
    void operator()(Rhs const& rhs) const
    {
      static_assert(std::is_nothrow_destructible<Rhs>{},
                    "T must not throw in destructor");

      static_assert(std::is_nothrow_move_constructible<Rhs>{},
                    "T must not throw in move constructor");

      if (self_.which() == rhs_which_)
      {
        //the types are the same, so just assign into the lhs
        *reinterpret_cast<Rhs*>(&self_.storage_) = rhs;
      }
      else
      {
        Rhs tmp(rhs);
        self_.destruct();
        self_.construct(std::move(tmp));
      }
    }

  private:
    basic_variant& self_;
    tag_type rhs_which_;
  };

  struct move_assigner
  {
    move_assigner(basic_variant& self, tag_type rhs_which)
      : self_(self), rhs_which_(rhs_which)
    {
    }

    template <typename Rhs>
    void operator()(Rhs& rhs) const noexcept
    {
      using rhs_no_const = std::remove_const_t<Rhs>;

      static_assert(std::is_nothrow_destructible<rhs_no_const>{},
                    "T must not throw in destructor");

      static_assert(std::is_nothrow_move_assignable<rhs_no_const>{},
                    "T must not throw in move assignment");

      static_assert(std::is_nothrow_move_constructible<rhs_no_const>{},
                    "T must not throw in move constructor");

      if (self_.which() == rhs_which_)
      {
        //the types are the same, so just assign into the lhs
        *reinterpret_cast<rhs_no_const*>(&self_.storage_) = std::move(rhs);
      }
      else
      {
        self_.destruct();
        self_.construct(std::move(rhs));
      }
    }

  private:
    basic_variant& self_;
    tag_type rhs_which_;
  };

  struct destructor
  {
    template <typename T>
    void operator()(T& x) const noexcept
    {
      static_assert(std::is_nothrow_destructible<T>{},
                    "T must not throw in destructor");
      x.~T();
    }
  };

  template <size_t TT, typename... Tail>
  struct initializer;

  template <size_t TT, typename T, typename... Tail>
  struct initializer<TT, T, Tail...> 
    : public initializer<TT + 1, Tail...>
  {
    using base = initializer<TT + 1, Tail...>;
    using base::initialize;

    static void initialize(basic_variant& v, T&& x)
    {
      v.construct(std::move(x));
      v.which_ = static_cast<tag_type>(TT);
    }

    static void initialize(basic_variant& v, T const& x)
    {
      v.construct(x);
      v.which_ = static_cast<tag_type>(TT);
    }
  };

  template <size_t TT>
  struct initializer<TT>
  {
    void initialize(); //this should never match
  };

  struct equality
  {
    equality(basic_variant const& self)
      : self_(self)
    {
    }

    template <typename Rhs>
    bool operator()(Rhs const& rhs) const
    {
      return *reinterpret_cast<Rhs const*>(&self_.storage_) == rhs;
    }

  private:
    basic_variant const& self_;
  };

  template <typename T, typename Internal>
  static T& get_value(T& x, Internal)
  {
    return x;
  }

  template <typename T, typename Internal>
  static T const& get_value(T const& x, Internal)
  {
    return x;
  }

  template <typename T>
  static T& get_value(recursive_wrapper<T>& x, std::false_type)
  {
    return x.get();
  }

  template <typename T>
  static T const& get_value(recursive_wrapper<T> const& x, std::false_type)
  {
    return x.get();
  }

  template <typename T, typename Storage>
  using const_type =
    std::conditional_t<
      std::is_const<std::remove_reference_t<Storage> >::value,
      T const,
      T
    >;

  template <
    typename T,
    typename Internal,
    typename Storage,
    typename Visitor,
    typename... Args
  >
  static auto invoke(Internal internal, Storage&& storage, Visitor&& visitor,
                     Args&&... args)
    // FIXME: why does adding this decltype expression squelch compile errors
    // with visitors returning references?
    -> decltype(visitor(*reinterpret_cast<const_type<T, Storage>*>(&storage), args...))
  {
    auto x = reinterpret_cast<const_type<T, Storage>*>(&storage);
    return visitor(get_value(*x, internal), std::forward<Args>(args)...);
  }

  template <
    typename Internal,
    typename Storage,
    typename Visitor,
    typename... Args
  >
  static auto visit_impl(tag_type which,
                         Internal internal,
                         Storage&& storage,
                         Visitor&& visitor,
                         Args&&... args)
  {
    using visitor_type = std::decay_t<Visitor>;
    using this_front = const_type<front, Storage>;
    using result_type = decltype(visitor(std::declval<this_front&>(), args...));

    // TODO: Consider all overloads, not just the one with the first type.
    static_assert(callable<visitor_type, this_front&, Args&&...>::value,
                  "visitor has no viable overload for operator()");

    using fn = result_type (*)(Internal, Storage, Visitor&&, Args&&...);
    static fn callers[sizeof...(Ts)] =
    {
      &invoke<Ts, Internal, Storage, Visitor, Args...>...
    };

    assert(static_cast<size_t>(which) >= 0
           && static_cast<size_t>(which) < sizeof...(Ts));

    return (*callers[static_cast<size_t>(which)])(
        internal,
        std::forward<Storage>(storage),
        std::forward<Visitor>(visitor),
        std::forward<Args>(args)...);
  }

  struct factory { };
  basic_variant(factory, tag_type tag)
  {
    which_ = tag;
    apply<std::false_type>(default_constructor{*this});
  }

  template <typename Visitor>
  auto apply_visitor_internal(Visitor&& v)
  {
    return apply<std::true_type>(std::forward<Visitor>(v));
  }

  template <typename Visitor>
  auto apply_visitor_internal(Visitor&& v) const
  {
    return apply<std::true_type>(std::forward<Visitor>(v));
  }

  template <typename T>
  void construct(T&& x) noexcept(std::is_rvalue_reference<decltype(x)>{})
  {
    using type = typename std::remove_reference<T>::type;

    // FIXME: Somehow the copmiler doesn't generate nothrow move constructors
    // for some of our custom types, even though they are annotated as such.
    // Needs investigation.
    //static_assert(std::is_nothrow_move_constructible<type>{},
    //              "move constructor of T must not throw");

    new (&storage_) type(std::forward<T>(x));
  }

  void destruct() noexcept
  {
    apply_visitor_internal(destructor());
  }

#ifdef VAST_GCC
  // FIXME: Seems like GCC doesn't have std::aligned_union implemented.
  template <typename T>
  struct Sizeof
  {
    static constexpr auto value = sizeof(T);
  };

  template <typename T>
  struct Alignof
  {
    static constexpr auto value = alignof(T);
  };

  std::aligned_storage_t<
    max<Sizeof, Ts...>(),
    max<Alignof, Ts...>()
  > storage_;
#else
  std::aligned_union_t<0, Ts...> storage_;
#endif

  tag_type which_;
};

namespace detail {

template <typename T>
struct getter
{
  T const* operator()(T const& x) const
  {
    return &x;
  }

  T* operator()(T& x) const
  {
    return &x;
  }

  template <typename U>
  T const* operator()(U const&) const
  {
    return nullptr;
  }

  template <typename U>
  T* operator()(U&) const
  {
    return nullptr;
  }
};

template <typename Visitor>
class delayed_visitor
{
public:
  delayed_visitor(Visitor v)
    : visitor_(std::move(v))
  {
  }

  template <typename... Visitables>
  auto operator()(Visitables&&... vs)
  {
    return apply_visitor(visitor_, std::forward<Visitables>(vs)...);
  }

private:
  Visitor visitor_;
};

template <typename Visitor>
class delayed_visitor_wrapper
{
public:
  delayed_visitor_wrapper(Visitor& visitor)
    : visitor_(visitor)
  {
  }

  template <typename... Visitables>
  auto operator()(Visitables&&... vs)
  {
    return apply_visitor(visitor_, std::forward<Visitables>(vs)...);
  }

private:
  Visitor& visitor_;
};

template <typename Visitor, typename Visitable>
class binary_visitor
{
public:
  binary_visitor(Visitor& visitor, Visitable& visitable)
    : visitor_(visitor),
      visitable_(visitable)
  {
  }

  template <typename... Ts>
  auto operator()(Ts&&... xs)
  {
    return visitable_.template apply<std::false_type>(
        visitor_, std::forward<Ts>(xs)...);
  }

private:
  Visitor& visitor_;
  Visitable& visitable_;
};

} // namespace detail

template <typename Visitor>
auto apply_visitor(Visitor&& visitor)
{
  return detail::delayed_visitor<Visitor>(std::move(visitor));
}

template <typename Visitor>
auto apply_visitor(Visitor& visitor)
{
  return detail::delayed_visitor_wrapper<Visitor>(visitor);
}

template <typename Visitor, typename Visitable>
auto apply_visitor(Visitor&& visitor, Visitable&& visitable)
{
  return visitable.template apply<std::false_type>(
      std::forward<Visitor>(visitor));
}

template <typename Visitor, typename V, typename... Vs>
auto apply_visitor(Visitor&& visitor, V&& v, Vs&&... vs)
{
  return apply_visitor(detail::binary_visitor<Visitor, V>(visitor, v), vs...);
}

/// A variant with a defaulted tag type.
template <typename... Ts>
using variant = basic_variant<size_t, Ts...>;

// Variant Concept
// ===============
//
// The *Variant* concept comes in handy for types which contain a variant but
// offer an extended interface. Such types benefit from uniform access of the
// variant aspect, namely visitation and selective type checking/extraction.
//
// To model the *Variant* concept, a type `V` must provide two overloads of the
// free function:
//
//    variant<Ts...>&       expose(V& x)
//    variant<Ts...> const& expose(V const& x)
//
// This function is found via ADL and enables the following free functions:
//
//    1) `auto visit(Visitor, Vs&&... vs)`
//    2) `auto which(V&& x)`
//    3) `auto get(V&& x)`
//    4) `bool is(V&& x)`

template <typename Tag, typename... Ts>
basic_variant<Tag, Ts...>& expose(basic_variant<Tag, Ts...>& v)
{
  return v;
}

template <typename Tag, typename... Ts>
basic_variant<Tag, Ts...> const& expose(basic_variant<Tag, Ts...> const& v)
{
  return v;
}

template <typename Visitor, typename... Vs>
auto visit(Visitor&& v, Vs&&... vs)
{
  return apply_visitor(std::forward<Visitor>(v), expose(vs)...);
}

template <typename V>
auto which(V&& v)
{
  return expose(v).which();
}

template <typename T, typename V>
auto get(V&& v)
{
  return visit(detail::getter<T>{}, v);
}

template <typename T, typename V>
auto is(V&& v)
{
  return get<T>(v) != nullptr;
}

} // namespace util

// We want to use these functions to work via ADL within VAST.
using util::which;
using util::get;
using util::is;
using util::visit;

} // namespace vast

#endif
