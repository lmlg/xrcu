#include "utils.hpp"
#include "../skip_list.hpp"
#include <vector>
#include <string>
#include <vector>
#include <thread>
#include <cstdio>
#include <cctype>

typedef xrcu::skip_list<std::string> sklist_t;

static inline
std::string mkstr (int i)
{
  char buf[100];
  sprintf (buf, "%d", i);
  return (std::string (buf));
}

void test_single_threaded ()
{
  sklist_t sl;

  ASSERT (sl.empty ());

  for (int i = 0; i < 1000; ++i)
    ASSERT (sl.insert (mkstr (i)));

  ASSERT (!sl.insert (mkstr (813)));

  ASSERT (sl.size () == 1000);

  auto prev = sl.remove (mkstr (101));
  ASSERT (prev.has_value ());
  ASSERT (!sl.erase (mkstr (101)));
  ASSERT (sl.erase (mkstr (999)));

  for (const auto& s : sl)
    for (auto ch : s)
      ASSERT (isdigit (ch));

  sklist_t s2 { std::string ("aaa"), std::string ("bbb"),
                std::string ("ccc"), std::string ("ddd") };

  sl.swap (s2);
  ASSERT (sl.size () == 4);
  sl.clear ();
  ASSERT (sl.empty ());
}

static const int INSERTER_LOOPS = 1000;
static const int INSERTER_THREADS = 16;

static void
mt_inserter (sklist_t *sx, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    ASSERT (sx->insert (mkstr (index * INSERTER_LOOPS + i)));
}

void test_insert_mt ()
{
  sklist_t sx;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter, &sx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (sx.size () == INSERTER_THREADS * INSERTER_LOOPS);
}

static void
mt_inserter_ov (sklist_t *sx, int index)
{
  for (int i = 0; i < INSERTER_LOOPS; ++i)
    sx->insert (mkstr (index * (INSERTER_LOOPS / 2) + i));
}

void test_insert_mt_ov ()
{
  sklist_t sx;
  std::vector<std::thread> thrs;

  for (int i = 0; i < INSERTER_THREADS; ++i)
    thrs.push_back (std::thread (mt_inserter_ov, &sx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (sx.size () == (INSERTER_THREADS + 1) * INSERTER_LOOPS / 2);
}

static const int ERASER_LOOPS = 1000;
static const int ERASER_THREADS = 8;

static void
mt_eraser (sklist_t *sx, int index)
{
  for (int i = 0; i < ERASER_LOOPS; ++i)
    {
      auto prev = sx->remove (mkstr (index * ERASER_LOOPS + i));
      ASSERT (prev.has_value ());
      for (auto ch : *prev)
        ASSERT (isdigit (ch));
    }
}

static void
fill_list_for_erase (sklist_t& sx)
{
  for (int i = 0; i < ERASER_THREADS * ERASER_LOOPS; ++i)
    sx.insert (mkstr (i));
}

void test_erase_mt ()
{
  sklist_t sx;
  fill_list_for_erase (sx);

  std::vector<std::thread> thrs;
  for (int i = 0; i < ERASER_THREADS; ++i)
    thrs.push_back (std::thread (mt_eraser, &sx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (sx.empty ());
}

static void
mt_eraser_ov (sklist_t *sx, int index)
{
  for (int i = 0; i < ERASER_LOOPS; ++i)
    sx->erase (mkstr (index * (ERASER_LOOPS / 2) + i));
}

void test_erase_mt_ov ()
{
  sklist_t sx;
  fill_list_for_erase (sx);

  std::vector<std::thread> thrs;
  for (int i = 0; i < ERASER_THREADS; ++i)
    thrs.push_back (std::thread (mt_eraser_ov, &sx, i));

  for (auto& thr : thrs)
    thr.join ();

  ASSERT (sx.size () == (ERASER_THREADS - 1) * ERASER_LOOPS / 2);
}

int main ()
{
  TEST ("skip list API in a single thread", test_single_threaded);
  TEST ("multi threaded insertions", test_insert_mt);
  TEST ("multi threaded, overlapped insertions", test_insert_mt_ov);
  TEST ("multi threaded erasures", test_erase_mt);
  TEST ("multi threaded, overlapped erasures", test_erase_mt_ov);
  std::cout << "Done\n";
  return (0);
}
