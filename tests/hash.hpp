#ifndef __XRCU_TESTS_HASH__
#define __XRCU_TESTS_HASH__   1

#include "xrcu/hash_table.hpp"
#include "utils.hpp"
#include <thread>
#include <algorithm>
#include <cstdio>

typedef xrcu::hash_table<int, std::string, std::equal_to<int>,
                         std::hash<int>, test_allocator<int>> table_t;

namespace ht_test
{

static std::string&
mutate (std::string& in, const char *s)
{
  in += s;
  return (in);
}

static std::string
mknew (std::string& in, const char *s)
{
  return (in + s);
}

void test_single_threaded ()
{
  table_t tx { { -1, "abc" }, { -2, "def" }, { -3, "ghi" } };
  ASSERT (tx.size () == 3);
  ASSERT (!tx.empty ());
  ASSERT (tx.find (-2, std::string ("")) == "def");

  for (int i = 0; i < 4000; ++i)
    tx.insert (i, mkstr (i));

  for (int i = -3; i < 4000; ++i)
    ASSERT (tx.contains (i));

  tx.update (101, mutate, "!!!");
  ASSERT (tx.find(101, std::string ("")).find ("!!!") != std::string::npos);

  tx.update (2002, mknew, "!!!");
  ASSERT (tx.find(2002, std::string ("")).find ("!!!") != std::string::npos);

  auto old_size = tx.size ();

  int i;
  for (i = 0; i < 1000; i += 2)
    ASSERT (tx.erase (i));

  ASSERT (tx.size () == old_size - i / 2);

  auto prev = tx.remove (101);
  ASSERT (prev.has_value ());

  i = 0;
  for (const auto& q : tx)
    {
      ASSERT (q.second.size () > 0);
      ++i;
    }

  ASSERT ((size_t)i == tx.size ());

  auto old = tx;
  tx.clear ();

  ASSERT (tx.size () == 0);
  ASSERT (old.size () != 0);

  tx.swap (old);
  ASSERT (old.size () == 0);
  ASSERT (tx.size () != 0);

  ASSERT (!xrcu::in_cs ());
}

static void
mt_inserter (table_t *tx, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    {
      int key = index * INSERTER_LOOPS + i;
      ASSERT (tx->insert (key, mkstr (key)));
    }
}

static bool
ht_consistent (table_t& tx)
{
  if (tx.empty ())
    return (true);

  for (auto p : tx)
    if (mkstr (p.first) != p.second)
      return (false);

  return (true);
}

void test_insert_mt ()
{
  table_t tx;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter, &tx, i));

  for (auto& thr: thrs)
    thr.join ();

  ASSERT (tx.size () == INSERTER_THREADS * INSERTER_LOOPS);
  ASSERT (ht_consistent (tx));
}

static void
mt_inserter_ov (table_t *tx, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    {
      int key = index * (INSERTER_LOOPS / 2) + i;
      tx->insert (key, mkstr (key));
    }
}

void test_insert_mt_ov ()
{
  table_t tx;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter_ov, &tx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (tx.size () == (INSERTER_THREADS + 1) * INSERTER_LOOPS / 2);
  ASSERT (ht_consistent (tx));
}

static void
mt_eraser (table_t *tx, int index)
{
  for (int i = 0; i < ERASER_LOOPS; ++i)
    {
      int key = index * ERASER_LOOPS + i;
      auto prev = tx->remove (key);
      ASSERT (prev.has_value ());
      ASSERT ((*prev)[0] == '-');
    }
}

static void
fill_table_for_erase (table_t& tx)
{
  for (int i = 0; i < ERASER_THREADS * ERASER_LOOPS; ++i)
    tx.insert (i, mkstr (-i - 1));
}

void test_erase_mt ()
{
  table_t tx;
  fill_table_for_erase (tx);

  std::vector<std::thread> thrs;

  for (int i = 0; i < ERASER_THREADS; ++i)
    thrs.push_back (std::thread (mt_eraser, &tx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (tx.empty ());
}

static void
mt_eraser_ov (table_t *tx, int index)
{
  for (int i = 0; i < ERASER_LOOPS; ++i)
    {
      int key = index * (ERASER_LOOPS / 2) + i;
      tx->erase (key);
    }
}

void test_erase_mt_ov ()
{
  table_t tx;
  fill_table_for_erase (tx);

  std::vector<std::thread> thrs;
  for (int i = 0; i < ERASER_THREADS; ++i)
    thrs.push_back (std::thread (mt_eraser_ov, &tx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (tx.size () == (ERASER_THREADS - 1) * ERASER_LOOPS / 2);
}

static void
mt_mutator (table_t *tx)
{
  int keys[MUTATOR_KEY_SIZE];
  for (int i = 0; i < MUTATOR_KEY_SIZE; ++i)
    keys[i] = i;

  std::random_shuffle (keys, keys + MUTATOR_KEY_SIZE);
  for (int i = 0; i < MUTATOR_KEY_SIZE; ++i)
    if (!tx->insert (keys[i], std::string ("")))
      tx->erase (keys[i]);
}

void test_mutate_mt ()
{
  table_t tx;
  std::vector<std::thread> thrs;

  for (int i = 0; i < MUTATOR_THREADS; ++i)
    thrs.push_back (std::thread (mt_mutator, &tx));

  for (auto& thr : thrs)
    thr.join ();

  for (auto q : tx)
    ASSERT (q.first <= MUTATOR_KEY_SIZE && q.second.empty ());
}

void test_iter ()
{
  table_t tx;

  for (int i = 0; i < 5; ++i)
    tx.insert (i, mkstr (i));

  auto it = tx.begin ();

  for (int i = 10; i < 1000; ++i)
    tx.insert (i, mkstr (i));

  int c = 0;
  for (; it != tx.end (); ++it, ++c)
    ;

  ASSERT (c >= 5);
}

test_module hash_table_tests
{
  "hash table",
  {
    { "API in a single thread", test_single_threaded },
    { "multi threaded insertions", test_insert_mt },
    { "multi threaded overlapped insertions", test_insert_mt_ov },
    { "multi threaded erasures", test_erase_mt },
    { "multi threaded overlapped erasures", test_erase_mt_ov },
    { "multi threaded mutations", test_mutate_mt },
    { "iteration during modifications", test_iter }
  }
};

} // namespace ht_test

#endif
