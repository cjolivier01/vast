#include "vast/concept/parseable/to.hpp"
#include "vast/concept/parseable/vast/expression.hpp"
#include "vast/concept/printable/stream.hpp"
#include "vast/concept/printable/vast/expression.hpp"

#define SUITE expression
#include "test.hpp"

using namespace vast;
using namespace std::string_literals;

TEST(parseable/printable - predicate) {
  predicate pred;
  // LHS: schema, RHS: data
  std::string str = "x.y.z == 42";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == key_extractor{key{"x", "y", "z"}});
  CHECK(pred.op == equal);
  CHECK(pred.rhs == data{42u});
  CHECK_EQUAL(to_string(pred), str);
  // LHS: data, RHS: data
  str = "42 in {21, 42, 84}";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == data{42u});
  CHECK(pred.op == in);
  CHECK(pred.rhs == data{set{21u, 42u, 84u}});
  CHECK_EQUAL(to_string(pred), str);
  // LHS: type, RHS: data
  str = "&type != \"foo\"";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == attribute_extractor{"type"});
  CHECK(pred.op == not_equal);
  CHECK(pred.rhs == data{"foo"});
  CHECK_EQUAL(to_string(pred), str);
  // LHS: data, RHS: type
  str = "10.0.0.0/8 ni :addr";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == data{*to<subnet>("10.0.0.0/8")});
  CHECK(pred.op == ni);
  CHECK(pred.rhs == type_extractor{address_type{}});
  CHECK_EQUAL(to_string(pred), str);
  // LHS: type, RHS: data
  str = ":real >= -4.8";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == type_extractor{real_type{}});
  CHECK(pred.op == greater_equal);
  CHECK(pred.rhs == data{-4.8});
  CHECK_EQUAL(to_string(pred), str);
  // LHS: data, RHS: time
  str = "now > &time";
  CHECK(parsers::predicate(str, pred));
  CHECK(is<data>(pred.lhs));
  CHECK(pred.op == greater);
  CHECK(pred.rhs == attribute_extractor{"time"});
  str = "x == y";
  CHECK(parsers::predicate(str, pred));
  CHECK(pred.lhs == key_extractor{key{"x"}});
  CHECK(pred.op == equal);
  CHECK(pred.rhs == key_extractor{key{"y"}});
  CHECK_EQUAL(to_string(pred), str);
  // Invalid type name.
  CHECK(!parsers::predicate(":foo == -42"));
}

TEST(parseable - expression) {
  expression expr;
  predicate p1{key_extractor{key{"x"}}, equal, data{42u}};
  predicate p2{type_extractor{port_type{}}, equal, data{port{53, port::udp}}};
  predicate p3{key_extractor{key{"a"}}, greater, key_extractor{key{"b"}}};
  MESSAGE("conjunction");
  CHECK(parsers::expr("x == 42 && :port == 53/udp", expr));
  CHECK_EQUAL(expr, expression(conjunction{p1, p2}));
  CHECK(parsers::expr("x == 42 && :port == 53/udp && x == 42", expr));
  CHECK_EQUAL(expr, expression(conjunction{p1, p2, p1}));
  CHECK(parsers::expr("x == 42 && ! :port == 53/udp && x == 42", expr));
  CHECK_EQUAL(expr, expression(conjunction{p1, negation{p2}, p1}));
  CHECK(parsers::expr("x > 0 && x < 42 && a.b == x.y", expr));
  MESSAGE("disjunction");
  CHECK(parsers::expr("x == 42 || :port == 53/udp || x == 42", expr));
  CHECK_EQUAL(expr, expression(disjunction{p1, p2, p1}));
  CHECK(parsers::expr("a==b || b==c || c==d", expr));
  MESSAGE("negation");
  CHECK(parsers::expr("! x == 42", expr));
  CHECK_EQUAL(expr, expression(negation{p1}));
  CHECK(parsers::expr("!(x == 42 || :port == 53/udp)", expr));
  CHECK_EQUAL(expr, expression(negation{disjunction{p1, p2}}));
  MESSAGE("parentheses");
  CHECK(parsers::expr("(x == 42)", expr));
  CHECK_EQUAL(expr, p1);
  CHECK(parsers::expr("((x == 42))", expr));
  CHECK_EQUAL(expr, p1);
  CHECK(parsers::expr("x == 42 && (x == 42 || a > b)", expr));
  CHECK_EQUAL(expr, expression(conjunction{p1, disjunction{p1, p3}}));
  CHECK(parsers::expr("x == 42 && x == 42 || a > b && x == 42", expr));
  expression expected = disjunction{conjunction{p1, p1}, conjunction{p3, p1}};
  CHECK_EQUAL(expr, expected);
}
