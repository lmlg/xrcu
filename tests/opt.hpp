#ifndef __XRCU_TESTS_OPT__
#define __XRCU_TESTS_OPT__   1

#include "../optional.hpp"
#include "utils.hpp"

namespace opt_test
{

void test_optional ()
{
  xrcu::optional<std::string> opt;

  ASSERT (!opt.has_value ());
  opt.reset ();
  ASSERT (!opt.has_value ());
  opt = mkstr (100);
  ASSERT (opt.has_value ());

  *opt += mkstr (200);
  ASSERT (*opt == mkstr (100) + mkstr (200));

  opt->clear ();
  ASSERT (opt.has_value ());
  ASSERT (opt->empty ());

  opt.reset ();
  ASSERT (!opt.has_value ());

  std::string s = mkstr (300);
  opt = s;
  ASSERT (s == *opt);

  xrcu::optional<std::string> o2 { opt };
  ASSERT (*o2 == *opt);

  o2.reset ();
  ASSERT (opt.has_value ());

  xrcu::optional<std::string> o3 { mkstr (400) };
  ASSERT (o3.has_value ());
}

test_module optional_tests
{
  "optional",
  {
    { "API", test_optional }
  }
};

} // namespace opt_test

#endif
