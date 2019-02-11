#ifndef __XRCU_HPP__
#define __XRCU_HPP__   1

namespace xrcu
{

// Enter a read-side critical section.
extern void enter_cs ();

// Exit a read-side critical section.
extern void exit_cs ();

// Test if the calling thread is in a read-side critical section.
extern bool in_cs ();

/* Wait until all readers have entered a quiescent state.
 * Returns false if a deadlock is detected, true otherwise. */
extern bool sync ();

// Base type for finalizable objects.
struct finalizable
{
  finalizable *fin_next = nullptr;

  virtual void safe_destroy ()
    {
      delete this;
    }

  virtual ~finalizable () {}
};

// Add FINP to the list of objects to be finalized after a grace period.
extern void finalize (finalizable *__finp);

// Force destruction of pending finalizable objects.
extern void flush_finalizers ();

struct cs_guard
{
  cs_guard ()
    {
      enter_cs ();
    }

  ~cs_guard ()
    {
      exit_cs ();
    }
};

// Miscellaneous functions that don't belong anywhere else.

// Count the number of trailing zeroes.

#ifdef __GNUC__

inline unsigned int
ctz (unsigned int val)
{
  return (__builtin_ctz (val));
}

#else

unsigned int ctz (unsigned int val)
{
  unsigned int ret = 0;

  val &= ~val + 1;   // Isolate the LSB.
  ret += !!(val & 0xaaaaaaaau) << 0;
  ret += !!(val & 0xccccccccu) << 1;
  ret += !!(val & 0xf0f0f0f0u) << 2;
  ret += !!(val & 0xff00ff00u) << 3;
  ret += !!(val & 0xffff0000u) << 4;

  return (ret);
}

#endif

// Generate a pseudo-random number (thread-safe).
extern unsigned int xrand ();

} // namespace xrcu

#endif
