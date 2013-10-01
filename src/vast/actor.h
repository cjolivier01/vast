#ifndef VAST_ACTOR_H
#define VAST_ACTOR_H

#include <cppa/event_based_actor.hpp>
#include "vast/logger.h"

namespace vast {

/// An actor enhanced in 
template <typename Derived>
class actor : public cppa::event_based_actor
{
public:
  /// Implements `cppa::event_based_actor::init`.
  virtual void init() override
  {
    VAST_LOG_ACTOR_VERBOSE(derived()->description(), "spawned");
    derived()->act();
    if (! has_behavior())
    {
      VAST_LOG_ACTOR_ERROR(derived()->description(),
                           "act() did not set a behavior, terminating");
      quit();
    }
  }

  /// Overrides `event_based_actor::on_exit`.
  virtual void on_exit() override
  {
    VAST_LOG_ACTOR_VERBOSE(derived()->description(), "terminated");
  }

private:
  Derived const* derived() const
  {
    return static_cast<Derived const*>(this);
  }

  Derived* derived()
  {
    return static_cast<Derived*>(this);
  }
};

} // namespace vast

#endif