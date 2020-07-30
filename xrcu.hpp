/* Declarations for the RCU API.

   This file is part of xrcu.

   xrcu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

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
  finalizable *_Fin_next = nullptr;

  virtual void safe_destroy ()
    {
      ::delete this;
    }

  virtual ~finalizable () {}
};

// Add FINP to the list of objects to be finalized after a grace period.
extern void finalize (finalizable *finp);

/* Force destruction of pending finalizable objects.
 * Returns true if they were destroyed. */
extern bool flush_finalizers ();

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
struct atfork
{
  void (*prepare) ();
  void (*parent) ();
  void (*child) ();
};

// Fetch the `pthread_atfork' callbacks for XRCU.
extern atfork atfork_data ();

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

// Get the library version.
extern void library_version (int& major, int& minor);

} // namespace xrcu

#endif
