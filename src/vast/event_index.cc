#include "vast/event_index.h"

#include "vast/util/convert.h"

using namespace cppa;

namespace vast {

struct event_meta_index::loader : expr::default_const_visitor
{
  loader(event_meta_index& idx)
    : idx{idx}
  {
  }

  virtual void visit(expr::predicate const& pred)
  {
    pred.lhs().accept(*this);
  }

  virtual void visit(expr::name_extractor const&)
  {
    if (idx.exists_ && idx.name_.size() == 1)
    {
      // We only hit the file system if the index has exactly one ID, namely 0,
      // representing the default-constructed state.
      io::unarchive(idx.dir_ / "name.idx", idx.name_);
      VAST_LOG_DEBUG(
          "loaded name index (" << idx.name_.size() << " bits)");
    }
  }

  virtual void visit(expr::timestamp_extractor const&)
  {
    if (idx.exists_ && idx.timestamp_.size() == 1)
    {
      io::unarchive(idx.dir_ / "timestamp.idx", idx.timestamp_);
      VAST_LOG_DEBUG(
          "loaded time index (" << idx.timestamp_.size() << " bits)");
    }
  }

  virtual void visit(expr::id_extractor const&)
  {
    assert(! "not yet implemented");
  }

  event_meta_index& idx;
};

struct event_meta_index::querier : expr::default_const_visitor
{
  querier(event_meta_index const& idx)
    : idx{idx}
  {
  }

  virtual void visit(expr::constant const& c)
  {
    val = &c.val;
  }

  virtual void visit(expr::predicate const& pred)
  {
    op = &pred.op;
    pred.rhs().accept(*this);
    pred.lhs().accept(*this);
  }

  virtual void visit(expr::name_extractor const&)
  {
    assert(op);
    assert(val);
    if (auto r = idx.name_.lookup(*op, *val))
      result = std::move(*r);
  }

  virtual void visit(expr::timestamp_extractor const&)
  {
    assert(op);
    assert(val);
    if (auto r = idx.timestamp_.lookup(*op, *val))
      result = std::move(*r);
  }

  virtual void visit(expr::id_extractor const&)
  {
    assert(! "not yet implemented");
  }

  bitstream result;
  event_meta_index const& idx;
  value const* val = nullptr;
  relational_operator const* op = nullptr;
};


event_meta_index::event_meta_index(path dir)
  : event_index<event_meta_index>{std::move(dir)},
    timestamp_{9} // Granularity of seconds
{
  // ID 0 is not a valid event.
  timestamp_.append(1, false);
  name_.append(1, false);

  timestamp_.checkpoint();
  name_.checkpoint();
}

char const* event_meta_index::description() const
{
  return "event-meta-index";
}

void event_meta_index::scan()
{
  if (exists(dir_ / "name.idx") || exists(dir_ / "timestamp.idx"))
    exists_ = true;
}

uint32_t event_meta_index::load(expr::ast const& ast)
{
  loader visitor{*this};
  ast.accept(visitor);
  return 1;
}

void event_meta_index::save()
{
  if (timestamp_.appended() > 0 || name_.appended() > 0)
  {
    if (! exists(dir_))
      mkdir(dir_);

    io::archive(dir_ / "timestamp.idx", timestamp_);
    VAST_LOG_ACTOR_DEBUG(
        "stored timestamp index (" << timestamp_.size() << " bits)");

    io::archive(dir_ / "name.idx", name_);
    VAST_LOG_ACTOR_DEBUG(
        "stored name index (" << name_.size() << " bits)");

    timestamp_.checkpoint();
    name_.checkpoint();
  }
}

bool event_meta_index::index(event const& e)
{
  if (exists_ && timestamp_.size() == 1)
  {
    VAST_LOG_ACTOR_DEBUG("appending to existing event meta data");

    io::unarchive(dir_ / "name.idx", name_);
    VAST_LOG_ACTOR_DEBUG("loaded name index (" << name_.size() << " bits)");

    io::unarchive(dir_ / "timestamp.idx", timestamp_);
    VAST_LOG_ACTOR_DEBUG(
        "loaded time index (" << timestamp_.size() << " bits)");
  }

  return timestamp_.push_back(e.timestamp(), e.id())
      && name_.push_back(e.name(), e.id());
}

bitstream event_meta_index::lookup(expr::ast const& ast) const
{
  querier visitor{*this};
  ast.accept(visitor);

  if (! visitor.result)
    VAST_LOG_ACTOR_DEBUG("found no result for " << ast);

  return std::move(visitor.result);
}


struct event_data_index::loader : expr::default_const_visitor
{
  loader(event_data_index& idx, value_type type)
    : idx{idx},
      type_{type}
  {
  }

  virtual void visit(expr::predicate const& pred)
  {
    pred.lhs().accept(*this);
  }

  virtual void visit(expr::offset_extractor const& oe)
  {
    if (idx.offsets_.count(oe.off))
      return;

    auto filename = idx.pathify(oe.off);
    if (! exists(filename))
      return;

    idx.load(filename, &type_);
  }

  virtual void visit(expr::type_extractor const& te)
  {
    auto t = te.type;
    if (idx.types_.count(t))
      return;

    auto er = idx.files_.equal_range(t);
    for (auto i = er.first; i != er.second; ++i)
      idx.load(i->second);
  }

  event_data_index& idx;
  value_type type_;
};

struct event_data_index::querier : expr::default_const_visitor
{
  querier(event_data_index const& idx)
    : idx{idx}
  {
  }

  virtual void visit(expr::constant const& c)
  {
    val = &c.val;
  }

  virtual void visit(expr::predicate const& pred)
  {
    op = &pred.op;
    pred.rhs().accept(*this);
    pred.lhs().accept(*this);
  }

  virtual void visit(expr::offset_extractor const& oe)
  {
    assert(op);
    assert(val);

    auto i = idx.offsets_.find(oe.off);
    if (i != idx.offsets_.end())
      if (auto r = i->second->lookup(*op, *val))
        result = std::move(*r);
  }

  virtual void visit(expr::type_extractor const& te)
  {
    assert(op);
    assert(val);
    assert(te.type == val->which());

    auto er = idx.types_.equal_range(te.type);
    for (auto j = er.first; j != er.second; ++j)
    {
      if (auto r = j->second->lookup(*op, *val))
      {
        if (result)
          result |= *r;
        else
          result = std::move(*r);
      }
    }
  }

  bitstream result;
  event_data_index const& idx;
  value const* val = nullptr;
  relational_operator const* op = nullptr;
};


event_data_index::event_data_index(path dir)
  : event_index<event_data_index>{std::move(dir)}
{
}

char const* event_data_index::description() const
{
  return "event-arg-index";
}

void event_data_index::scan()
{
  if (exists(dir_))
  {
    traverse(
        dir_,
        [&](path const& p) -> bool
        {
          value_type vt;
          io::unarchive(p, vt);
          files_.emplace(vt, p);
          return true;
        });

    assert(! files_.empty());
  }
}

namespace {

struct type_finder : expr::default_const_visitor
{
  virtual void visit(expr::predicate const& pred)
  {
    pred.rhs().accept(*this);
  }

  virtual void visit(expr::constant const& c)
  {
    type = c.val.which();
  }

  value_type type;
};

} // namespace <anonymous>

uint32_t event_data_index::load(expr::ast const& ast)
{
  type_finder tf;
  ast.accept(tf);

  loader visitor{*this, tf.type};
  ast.accept(visitor);
  return 1;
}

void event_data_index::save()
{
  VAST_LOG_ACTOR_DEBUG("saves indexes to filesystem");

  std::map<bitmap_index*, value_type> inverse;
  for (auto& p : types_)
    if (inverse.find(p.second) == inverse.end())
      inverse.emplace(p.second, p.first);

  for (auto& p : offsets_)
  {
    if (p.second->empty() || p.second->appended() == 0)
      continue;

    if (! exists(dir_))
      mkdir(dir_);

    auto const filename = pathify(p.first);
    assert(inverse.count(p.second.get()));
    io::archive(filename, inverse[p.second.get()], p.second);
    p.second->checkpoint();
    VAST_LOG_ACTOR_DEBUG("stored index " << filename.trim(-4) <<
                         " (" << p.second->size() << " bits)");
  }
}

bool event_data_index::index(event const& e)
{
  if (e.empty())
    return true;

  idx_off_.clear();
  idx_off_.push_back(0);
  return index_record(e, e.id(), idx_off_);
}

bitstream event_data_index::lookup(expr::ast const& ast) const
{
  querier visitor{*this};
  ast.accept(visitor);

  if (! visitor.result)
    VAST_LOG_ACTOR_DEBUG("found no result for " << ast);

  return std::move(visitor.result);
}

path event_data_index::pathify(offset const& o) const
{
  static string prefix{"@"};
  static string suffix{".idx"};
  return dir_ / (prefix + to<string>(o) + suffix);
}

bitmap_index* event_data_index::load(path const& p, value_type const* type)
{
  offset o;
  auto str = p.basename(true).str().substr(1);
  auto start = str.begin();
  if (! extract(start, str.end(), o))
  {
    VAST_LOG_ACTOR_ERROR("got invalid offset in path: " << p);
    return nullptr;
  }

  // We have issued an offset query in the past and loaded the corresponding
  // index already.
  auto i = offsets_.find(o);
  if (i != offsets_.end())
    return i->second.get();

  value_type vt;
  std::unique_ptr<bitmap_index> bmi;

  if (type)
  {
    io::unarchive(p, vt);
    if (vt != *type)
    {
      VAST_LOG_ACTOR_ERROR("type mismatch: wanted " << *type << ", got " << vt);
      return nullptr;
    }
  }

  io::unarchive(p, vt, bmi);
  if (! bmi)
  {
    VAST_LOG_ACTOR_ERROR("got corrupt index: " << p.basename());
    return nullptr;
  }

  VAST_LOG_ACTOR_DEBUG("loaded index " << p.trim(-4) <<
                       " (" << bmi->size() << " bits)");

  auto idx = bmi.get();
  types_.emplace(vt, idx);
  offsets_.emplace(o, std::move(bmi));

  return idx;
}

bool event_data_index::index_record(record const& r, uint64_t id, offset& o)
{
  if (o.empty())
    return true;

  for (auto& v : r)
  {
    if (v)
    {
      if (v.which() == record_type)
      {
        auto& inner = v.get<record>();
        if (! inner.empty())
        {
          o.push_back(0);
          if (! index_record(inner, id, o))
            return false;
          o.pop_back();
        }
      }
      else if (! is_container_type(v.which()))
      {
        bitmap_index* idx = nullptr;
        auto i = offsets_.find(o);
        if (i != offsets_.end())
        {
          idx = i->second.get();
        }
        else
        {
          // Check if we have an exisiting persistent index to append to.
          for (auto& p : files_)
          {
            offset off;
            auto str = p.second.basename(true).str().substr(1);
            auto start = str.begin();
            if (! extract(start, str.end(), off))
            {
              VAST_LOG_ACTOR_ERROR("got invalid offset in path: " << p.second);
              quit(exit::error);
              return false;
            }

            if (o == off)
            {
              idx = load(p.second);
              if (! idx)
              {
                quit(exit::error);
                return false;
              }

              VAST_LOG_ACTOR_DEBUG("appending to: " << p.second);
              break;
            }
          }
        }

        if (! idx)
        {
          // If we haven't found an index to append to, we create a new one.
          auto bmi = make_bitmap_index<bitstream_type>(v.which());
          if (! bmi)
          {
            VAST_LOG_ACTOR_ERROR(bmi.failure().msg());
            quit(exit::error);
            return false;
          }

          idx = bmi->get();
          idx->append(1, false); // ID 0 is not a valid event.
          types_.emplace(v.which(), idx);
          offsets_.emplace(o, std::move(*bmi));
        }

        if (! idx->push_back(v, id))
          return false;
      }
    }

    ++o.back();
  }

  return true;
}

} // namespace vast
