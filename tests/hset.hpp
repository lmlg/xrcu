#ifndef __XRCU_TESTS_HSET__
#define __XRCU_TESTS_HSET__   1

#include "../hash_set.hpp"
#include "utils.hpp"

struct hstring
{
  int ix = -1;
  std::string sx = "";

  hstring (int i, const std::string& s) : ix (i), sx (s)
    {
    }
};

typedef xrcu::hash_set<std::string> set_t;

namespace hs_set
{

void test_single_threaded ()
{
  set_t s { "abc", "def", "ghi" };

  ASSERT (s.size () == 3);
  ASSERT (!s.empty ());
  ASSERT (s.find("def").has_value ());

  for (int i = 0; i < 4000; ++i)
    s.insert (mkstr (i));

  for (int i = 0; i < 4000; ++i)
    ASSERT (s.contains (mkstr (i)));
}

test_module hash_set_tests
{
  "hash set",
  {
    { "API in a single thread", test_single_threaded }
  }
};

} // namespace hs_set

#endif
