#include "vast/detail/type_manager.h"

#include <cassert>
#include <exception>
#include "vast/bitstream.h"
#include "vast/container.h"
#include "vast/logger.h"
#include "vast/serialization.h"
#include "vast/type_info.h"
#include "vast/event.h"
#include "vast/value.h"
#include "vast/bitmap_index/arithmetic.h"
#include "vast/bitmap_index/address.h"
#include "vast/bitmap_index/port.h"
#include "vast/bitmap_index/string.h"
#include "vast/bitmap_index/time.h"
#include "vast/util/tuple.h"

namespace vast {
namespace detail {

bool
type_manager::add(std::type_info const& ti,
                  std::function<global_type_info*(type_id)> f)
{
  if (by_ti_.find(std::type_index(ti)) != by_ti_.end())
    return false;
  if (by_name_.find(ti.name()) != by_name_.end())
    return false;;

  auto gti = f(++id_);

  VAST_LOG_DEBUG("registering new type " << detail::demangle(ti.name()) <<
                 " with id " << id_ << " (mangled name: " << ti.name() << ")");

  by_id_.emplace(gti->id(), gti);
  by_name_.emplace(gti->name(), gti);
  by_ti_.emplace(std::type_index(ti), std::unique_ptr<global_type_info>(gti));

  return true;
}

global_type_info const* type_manager::lookup(std::type_info const& ti) const
{
  auto i = by_ti_.find(std::type_index(ti));
  if (i == by_ti_.end())
    return nullptr;
  return i->second.get();
}

global_type_info const* type_manager::lookup(type_id id) const
{
  auto i = by_id_.find(id);
  if (i == by_id_.end())
    return nullptr;
  return i->second;
}

global_type_info const* type_manager::lookup(std::string const& name) const
{
  auto i = by_name_.find(name);
  if (i == by_name_.end())
    return nullptr;
  return i->second;
}

// TODO: automatically build transitive closure over linked types.
bool type_manager::add_link(global_type_info const* from,
                            std::type_info const& to)
{
  if (! from)
    return false;
  if (*from == to)
    return false; // We do not store reflexivity...

  auto& set = conversions_[from->id()];
  auto t = set.find(std::type_index(to));
  if (t != set.end())
  {
    VAST_LOG_WARN("attempted to register duplicate conversion from type " <<
                  from->name() << " to type " << detail::demangle(to));
    return false;
  }
  set.emplace(to);
  return true;
}

bool type_manager::check_link(global_type_info const* from,
                              std::type_info const& to) const
{
  if (! from)
    return false;
  if (*from == to)
    return true; // ...but acknowledge it nonetheless.

  auto s = conversions_.find(from->id());
  if (s == conversions_.end())
    return false;
  return s->second.count(std::type_index(to));
}


type_manager* type_manager::create()
{
  return new type_manager;
}


// TODO: Use polymorphic lambdas in C++14.
namespace {

struct announcer
{
  template <typename T>
  void operator()(T /* x */) const
  {
    announce<T>();
  }
};

} // namespace <anonymous>

void type_manager::initialize()
{
  std::tuple<
    bool, double,
    int8_t, int16_t, int32_t, int64_t,
    uint8_t, uint16_t, uint32_t, uint64_t
  > integral_types;

  std::tuple<
    std::string,
    std::vector<std::string>
  > stl_types;

  std::tuple<
    address,
    time_range,
    time_point,
    port,
    prefix,
    record,
    regex,
    set,
    string,
    table,
    vector,
    value_type,
    value, std::vector<value>,
    event, std::vector<event>,
    detail::bitstream_model<null_bitstream>
  > vast_types;

  util::for_each(integral_types, announcer{});
  util::for_each(stl_types, announcer{});
  util::for_each(vast_types, announcer{});

  make_convertible<
    detail::bitstream_model<null_bitstream>,
    detail::bitstream_concept
  >();
}

void type_manager::destroy()
{
  delete this;
}

void type_manager::dispose()
{
  delete this;
}

} // namespace detail
} // namespace vast