#include "vast/concept/parseable/to.hpp"
#include "vast/event.hpp"

#include "vast/format/bro.hpp"

#define SUITE format
#include "test.hpp"
#include "fixtures/events.hpp"

using namespace vast;
using namespace std::string_literals;

namespace {

template <class Attribute>
bool bro_parse(type const& t, std::string const& s, Attribute& attr) {
  return format::bro::make_bro_parser<std::string::const_iterator>(t)(s, attr);
}

} // namspace <anonymous>

TEST(bro data parsing) {
  using namespace std::chrono;
  data d;
  CHECK(bro_parse(boolean_type{}, "T", d));
  CHECK(d == true);
  CHECK(bro_parse(integer_type{}, "-49329", d));
  CHECK(d == integer{-49329});
  CHECK(bro_parse(count_type{}, "49329"s, d));
  CHECK(d == count{49329});
  CHECK(bro_parse(timestamp_type{}, "1258594163.566694", d));
  auto ts = duration_cast<timespan>(double_seconds{1258594163.566694});
  CHECK(d == timestamp{ts});
  CHECK(bro_parse(timespan_type{}, "1258594163.566694", d));
  CHECK(d == ts);
  CHECK(bro_parse(string_type{}, "\\x2afoo*"s, d));
  CHECK(d == "*foo*");
  CHECK(bro_parse(address_type{}, "192.168.1.103", d));
  CHECK(d == *to<address>("192.168.1.103"));
  CHECK(bro_parse(subnet_type{}, "10.0.0.0/24", d));
  CHECK(d == *to<subnet>("10.0.0.0/24"));
  CHECK(bro_parse(port_type{}, "49329", d));
  CHECK(d == port{49329, port::unknown});
  CHECK(bro_parse(vector_type{integer_type{}}, "49329", d));
  CHECK(d == vector{49329});
  CHECK(bro_parse(set_type{string_type{}}, "49329,42", d));
  CHECK(d == set{"49329", "42"});
}

FIXTURE_SCOPE(bro_tests, fixtures::events)

TEST(bro writer) {
  // Sanity check some Bro events.
  CHECK_EQUAL(bro_conn_log.size(), 8462u);
  CHECK_EQUAL(bro_conn_log.front().type().name(), "bro::conn");
  auto record = get_if<vector>(bro_conn_log.front().data());
  REQUIRE(record);
  REQUIRE_EQUAL(record->size(), 17u); // 20 columns, but 4 for the conn record
  CHECK_EQUAL(record->at(3), data{"udp"}); // one after the conn record
  CHECK_EQUAL(record->back(), data{set{}}); // table[T] is actually a set
  // Perform the writing.
  auto dir = path{"vast-unit-test-bro"};
  auto guard = caf::detail::make_scope_guard([&] { rm(dir); });
  format::bro::writer writer{dir};
  for (auto& e : bro_conn_log)
    if (!writer.write(e))
      FAIL("failed to write event");
  for (auto& e : bro_http_log)
    if (!writer.write(e))
      FAIL("failed to write event");
  CHECK(exists(dir / bro_conn_log[0].type().name() + ".log"));
  CHECK(exists(dir / bro_http_log[0].type().name() + ".log"));
}

FIXTURE_SCOPE_END()
