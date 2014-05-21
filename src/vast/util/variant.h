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

#include <type_traits>

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
    typename Dummy =
    typename std::enable_if<std::is_convertible<U, T>::value, U>::type
  >
  recursive_wrapper(U const& u)
    : x_(new T(u))
  {
  }

  template <
    typename U,
    typename Dummy =
    typename std::enable_if<std::is_convertible<U, T>::value, U>::type
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
template <typename... Ts>
class variant : equality_comparable<variant<Ts...>>
{
  template <typename U, typename...>
  using front_t = U;

public:
  using tag_type = size_t;

  /// The first type in the variant; used for default-construction.
  using front = front_t<Ts...>;

  /// Default-constructs a variant with the first type.
  variant()
  {
    construct(front{});
    which_ = tag_type{};
  }

  /// Destructs the variant by invoking the destructor of the active instance.
  ~variant()
  {
    destruct();
  }

  /// Constructs a variant from one of the discrimanted types.
  ///
  /// @param x The value to construct the variant with. Note that *x* must be
  /// unambiguously convertible to one of the types in the variant.
  template <
    typename T,
    typename = typename std::enable_if<
      ! std::is_same<
        typename std::remove_reference<variant<Ts...>>::type,
        typename std::remove_reference<T>::type
      >::value,
      T
    >::type
  >
  variant(T&& x)
  {
    static_assert(! std::is_same<variant<Ts...>&, T>::value,
                  "should have been sfinaed out");

    // A compile error here means that T is not unambiguously convertible to
    // any of the variant types.
    initializer<0, Ts...>::initialize(*this, std::forward<T>(x));
  }

  variant(variant const& rhs)
  {
    rhs.apply_visitor_internal(constructor(*this));
    which_ = rhs.which_;
  }

  variant(variant&& rhs)
  {
    rhs.apply_visitor_internal(move_constructor(*this));
    which_ = rhs.which_;
  }

  variant& operator=(variant const& rhs)
  {
    if (this != &rhs)
    {
      rhs.apply_visitor_internal(assigner(*this, rhs.which()));
      which_ = rhs.which_;
    }
    return *this;
  }

  variant& operator=(variant&& rhs)
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
    return visit_impl(Internal{}, which_, &storage_,
                      std::forward<Visitor>(visitor),
                      std::forward<Args>(args)...);
  }

  template <typename Internal, typename Visitor, typename... Args>
  auto apply(Visitor&& visitor, Args&&... args) const
  {
    return visit_impl(Internal{}, which_, &storage_,
                      std::forward<Visitor>(visitor),
                      std::forward<Args>(args)...);
  }

  friend bool operator==(variant const& x, variant const& y)
  {
    return x.which_ != y.which_ || y.apply_visitor_internal(equality(x));
  }

private:
  template <typename T, typename Internal>
  static T& get_value(T& x, Internal)
  {
    return x;
  }

  template <typename T>
  static T& get_value(recursive_wrapper<T>& x, std::false_type)
  {
    return x.get();
  }

  template <typename T>
  static T const&
  get_value(recursive_wrapper<T> const& x, std::false_type)
  {
    return x.get();
  }

  template <typename T, typename Storage>
  using const_type =
    typename std::conditional<
      std::is_const<
        typename std::remove_pointer<
          typename std::remove_reference<Storage>::type
        >::type
       >::value,
       T const,
       T
     >::type;

  template <
    typename Internal,
    typename T,
    typename Storage,
    typename Visitor,
    typename... Args
  >
  static auto invoke(Internal internal, Storage&& storage, Visitor&& visitor,
                     Args&&... args)
  {
    auto x = reinterpret_cast<const_type<T, Storage>*>(storage);
    return visitor(get_value(*x, internal), std::forward<Args>(args)...);
  }

  template <
    typename Internal,
    typename Storage,
    typename Visitor,
    typename... Args
  >
  static auto visit_impl(Internal internal,
                         tag_type which,
                         Storage&& storage,
                         Visitor&& visitor,
                         Args&&... args)
  {
    using visitor_type = std::decay_t<Visitor>;

    // TODO: Consider all overloads, not just the one with the first type.
    static_assert(callable<visitor_type, front const&, Args&&...>::value
                  || callable<visitor_type, front&, Args&&...>::value,
                  "visitor has no viable overload for operator()");

    using result_type =
      std::conditional_t<
        callable<visitor_type, front const&, Args&&...>::value,
        std::result_of_t<visitor_type(front const&, Args&&...)>,
        std::result_of_t<visitor_type(front&, Args&&...)>
      >;

    using fn = result_type (*)(Internal, Storage&&, Visitor&&, Args&&...);
    static fn callers[sizeof...(Ts)] =
    {
      &invoke<Internal, Ts, Storage&&, Visitor, Args&&...>...
    };

    assert(static_cast<size_t>(which) >= 0
           && static_cast<size_t>(which) < sizeof...(Ts));

    return (*callers[which])(internal,
                             std::forward<Storage>(storage),
                             std::forward<Visitor>(visitor),
                             std::forward<Args>(args)...);
  }

  struct constructor
  {
    constructor(variant& self)
      : self_(self)
    {
    }

    template <typename T>
    void operator()(T const& rhs) const
    {
      self_.construct(rhs);
    }

  private:
    variant& self_;
  };

  struct move_constructor
  {
    move_constructor(variant& self)
      : self_(self)
    {
    }

    template <typename T>
    void operator()(T& rhs) const
    {
      self_.construct(std::move(rhs));
    }

  private:
    variant& self_;
  };

  struct assigner
  {
    assigner(variant& self, tag_type rhs_which)
      : self_(self), rhs_which_(rhs_which)
    {
    }

    template <typename Rhs>
    void operator()(Rhs const& rhs) const
    {
      if (self_.which() == rhs_which_)
      {
        //the types are the same, so just assign into the lhs
        *reinterpret_cast<Rhs*>(&self_.storage_) = rhs;
      }
      else
      {
        Rhs tmp(rhs);
        self_.destruct(); //nothrow
        self_.construct(std::move(tmp)); //nothrow (please)
      }
    }

  private:
    variant& self_;
    tag_type rhs_which_;
  };

  struct move_assigner
  {
    move_assigner(variant& self, tag_type rhs_which)
      : self_(self), rhs_which_(rhs_which)
    {
    }

    template <typename Rhs>
    void operator()(Rhs& rhs) const
    {
      using rhs_no_const = typename std::remove_const<Rhs>::type;

      if (self_.which() == rhs_which_)
      {
        //the types are the same, so just assign into the lhs
        *reinterpret_cast<rhs_no_const*>(&self_.storage_) = std::move(rhs);
      }
      else
      {
        self_.destruct(); //nothrow
        self_.construct(std::move(rhs)); //nothrow (please)
      }
    }

  private:
    variant& self_;
    tag_type rhs_which_;
  };

  struct equality
  {
    equality(variant const& self)
      : self_(self)
    {
    }

    template <typename Rhs>
    bool operator()(Rhs& rhs) const
    {
      return *reinterpret_cast<Rhs*>(&self_.storage_) == rhs;
    }

  private:
    variant const& self_;
  };

  struct destructor
  {
    template <typename T>
    void operator()(T& x) const
    {
      x.~T();
    }
  };

  template <tag_type Tag, typename... MyTypes>
  struct initializer;

  template <tag_type Tag, typename Current, typename... MyTypes>
  struct initializer<Tag, Current, MyTypes...>
    : public initializer<Tag + 1, MyTypes...>
  {
    using base = initializer<Tag + 1, MyTypes...>;
    using base::initialize;

    static void initialize(variant& v, Current&& current)
    {
      v.construct(std::move(current));
      v.which_ = Tag;
    }

    static void initialize(variant& v, Current const& current)
    {
      v.construct(current);
      v.which_ = Tag;
    }
  };

  template <tag_type Tag>
  struct initializer<Tag>
  {
    //this should never match
    void initialize();
  };

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
  void construct(T&& x)
  {
    using type = typename std::remove_reference<T>::type;
    new (&storage_) type(std::forward<T>(x));
  }

  void destruct()
  {
    apply_visitor_internal(destructor());
  }

  typename std::aligned_union<0, Ts...>::type storage_;
  tag_type which_;
};

template <typename T>
struct getter
{
  T* operator()(T& x) const
  {
    return &x;
  }

  template <typename U>
  T* operator()(U const&) const
  {
    return nullptr;
  }
};

template <typename T, typename... Ts>
T* get(variant<Ts...>& var)
{
  return apply_visitor(getter<T>(), var);
}

template <typename T, typename... Ts>
T const* get(variant<Ts...> const& var)
{
  return apply_visitor(getter<T const>(), var);
}

template <typename T, typename V>
bool variant_is_type(V const& v)
{
  return get<T>(&v) != nullptr;
}

namespace detail {

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
  return visitable.template apply<std::false_type>(visitor);
}

template <typename Visitor, typename V, typename... Vs>
auto apply_visitor(Visitor&& visitor, V&& v, Vs&&... vs)
{
  return apply_visitor(detail::binary_visitor<Visitor, V>(visitor, v), vs...);
}

} // namespace util
} // namespace vast

#endif
