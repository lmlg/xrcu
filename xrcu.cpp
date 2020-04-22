/* Definitions for the RCU API.

   This file is part of xrcu.

   khipu is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "version.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>

#if defined (__MINGW32__) || defined (__MINGW64__)
  #include <pthread.h>

static void tl_set (void *);

#else

#  define tl_set(p)

#endif

namespace xrcu
{

struct td_link
{
  td_link *next;
  td_link *prev;

  void init_head ()
    {
      this->next = this->prev = this;
    }

  void add (td_link *headp)
    { // Add self to HEADP.
      this->next = headp->next;
      this->prev = headp;
      headp->next->prev = this;
      headp->next = this;
    }

  void del ()
    { // Unlink self.
      this->next->prev = this->prev;
      this->prev->next = this->next;
    }

  bool empty_p () const
    {
      return (this == this->next);
    }

  bool linked_p () const
    {
      return (this->next != nullptr);
    }

  void splice (td_link *dst)
    {
      if (this->empty_p ())
        return;

      this->next->prev = dst;
      this->prev->next = dst->next;
      dst->next->prev = this->prev;
      dst->next = this->next;
    }
};

static const uintptr_t GP_PHASE_BIT =
  (uintptr_t)1 << (sizeof (uintptr_t) * 8 - 1);

static const uintptr_t GP_NEST_MASK = GP_PHASE_BIT - 1;

// Possible states for a reader thread.
enum
{
  rd_active,
  rd_inactive,
  rd_old
};

struct registry
{
  std::atomic_uintptr_t counter;
  td_link root;
  std::mutex td_mtx;
  std::mutex gp_mtx;

  static const unsigned int QS_ATTEMPTS = 1000;

  registry () : counter (1)
    {
      this->root.init_head ();
    }

  void add_tdata (td_link *lp)
    {
      tl_set (lp);
      this->td_mtx.lock ();
      lp->add (&this->root);
      this->td_mtx.unlock ();
    }

  uintptr_t get_ctr () const
    {
      return (this->counter.load (std::memory_order_relaxed));
    }

  void poll_readers (td_link *, td_link *, td_link *);

  void sync ();
};

static registry global_reg;

// Maximum number of pending finalizers before flushing.
#ifndef XRCU_MAX_FINS
#  define XRCU_MAX_FINS   1000
#endif

static const unsigned int MAX_FINS = XRCU_MAX_FINS;

struct tl_data : public td_link
{
  bool must_flush;
  unsigned int n_fins;
  std::atomic_uintptr_t counter;
  size_t xrand_val;
  finalizable *fin_objs;

  uintptr_t get_ctr () const
    {
      return (this->counter.load (std::memory_order_relaxed));
    }

  int state () const
    {
      auto val = this->counter.load (std::memory_order_acquire);
      if (!(val & GP_NEST_MASK))
        return (rd_inactive);
      else if (!((val ^ global_reg.get_ctr ()) & GP_PHASE_BIT))
        return (rd_active);
      else
        return (rd_old);
    }

  bool in_cs () const
    {
      return ((this->get_ctr () & GP_NEST_MASK) != 0);
    }

  bool flush_all ()
    {
      if (!sync ())
        return (false);

      for (auto f = this->fin_objs; f != nullptr; )
        {
          auto next = f->_Fin_next;
          f->safe_destroy ();
          f = next;
        }

      this->fin_objs = nullptr;
      this->n_fins = 0;
      this->must_flush = false;
      return (true);
    }

  void finalize (finalizable *finp)
    {
      finp->_Fin_next = this->fin_objs;
      this->fin_objs = finp;

      if (++this->n_fins < MAX_FINS)
        ;
      else if (!this->flush_all ())
        /* Couldn't reclaim memory since we are in a critical section.
         * Set the flag to do it ASAP. */
        this->must_flush = true;
    }

  ~tl_data ()
    {
      if (!this->linked_p ())
        return;

      this->counter.store (0, std::memory_order_release);

      if (this->n_fins > 0)
        this->flush_all ();

      global_reg.td_mtx.lock ();
      this->del ();
      global_reg.td_mtx.unlock ();
    }
};

#if defined (__MINGW32__) || defined (__MINGW64__)

// Mingw has problems with thread_local destructors.

struct key_handler
{
  pthread_key_t key;

  static void fini (void *ptr)
    {
      ((tl_data *)ptr)->~tl_data ();
    }

  key_handler ()
    {
      if (pthread_key_create (&this->key, key_handler::fini) != 0)
        throw "failed to create thread key";
    }

  void set (void *ptr)
    {
      pthread_setspecific (this->key, ptr);
    }
};

struct alignas (alignof (tl_data)) tl_buf
{
  unsigned char data[sizeof (tl_data)];
};

static thread_local tl_buf tlbuf;

#define tldata   (*(tl_data *)&tlbuf)

#else

static thread_local tl_data tldata {};

#endif

static inline tl_data*
local_data ()
{
  auto self = &tldata;
  if (!self->linked_p ())
    global_reg.add_tdata (self);

  return (self);
}

void enter_cs ()
{
  auto self = local_data ();
  auto val = self->get_ctr ();
  val = (val & GP_NEST_MASK) == 0 ? global_reg.get_ctr () : val + 1;
  self->counter.store (val, std::memory_order_release);
}

void exit_cs ()
{
  auto self = local_data ();
  auto val = self->get_ctr ();
  self->counter.store (val - 1, std::memory_order_release);

  if (self->must_flush && !self->in_cs ())
    self->flush_all ();
}

bool in_cs ()
{
  auto self = &tldata;
  return (self->linked_p () && self->in_cs ());
}

void registry::poll_readers (td_link *readers, td_link *outp, td_link *qsp)
{
  for (unsigned int loops = 0 ; ; ++loops)
    {
      td_link *next, *runp = readers->next;
      for (; runp != readers; runp = next)
        {
          next = runp->next;
          switch (((tl_data *)runp)->state ())
            {
              case rd_active:
                if (outp != nullptr)
                  {
                    runp->del ();
                    runp->add (outp);
                    break;
                  }

              // Fallthrough.
              case rd_inactive:
                runp->del ();
                runp->add (qsp);
                break;

              default:
                break;
            }
        }

      if (readers->empty_p ())
        break;

      this->td_mtx.unlock ();
      if (loops < QS_ATTEMPTS)
        xatomic_spin_nop ();
      else
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
      this->td_mtx.lock ();
    }
}

void registry::sync ()
{
  this->gp_mtx.lock ();
  this->td_mtx.lock ();

  if (this->root.empty_p ())
    {
      this->td_mtx.unlock ();
      this->gp_mtx.unlock ();
      return;
    }

  td_link out, qs;
  qs.init_head ();
  out.init_head ();

  std::atomic_thread_fence (std::memory_order_acq_rel);
  poll_readers (&this->root, &out, &qs);

  this->counter.store (this->get_ctr () ^ GP_PHASE_BIT,
                       std::memory_order_relaxed);

  poll_readers (&out, nullptr, &qs);
  qs.splice (&this->root);
  this->td_mtx.unlock ();
  this->gp_mtx.unlock ();
}

bool sync ()
{
  if (in_cs ())
    return (false);

  global_reg.sync ();
  return (true);
}

void finalize (finalizable *finp)
{
  if (finp)
    local_data()->finalize (finp);
}

bool flush_finalizers ()
{
  auto tld = local_data ();
  bool ret = tld->flush_all ();

  if (!ret)
    tld->must_flush = true;

  return (ret);
}

unsigned int xrand ()
{
  auto self = &tldata;   // Avoid local_data ()
  if (!self->xrand_val)
    self->xrand_val = (unsigned int)(time (nullptr) ^
      std::hash<std::thread::id>() (std::this_thread::get_id ()));

  self->xrand_val = self->xrand_val * 1103515245 + 12345;
  return (self->xrand_val >> 16);
}

static void
atfork_prepare ()
{
  global_reg.gp_mtx.lock ();
  global_reg.td_mtx.lock ();
}

static void
atfork_parent ()
{
  global_reg.td_mtx.unlock ();
  global_reg.gp_mtx.unlock ();
}

static void
atfork_child ()
{
  atfork_parent ();
  // Reset the registry
  global_reg.root.init_head ();

  auto self = &tldata;
  if (!self->linked_p ())
    return;

  // Manually add ourselves to the registry without locking.
  self->add (&global_reg.root);
}

atfork atfork_data ()
{
  atfork ret;
  ret.prepare = atfork_prepare;
  ret.parent = atfork_parent;
  ret.child = atfork_child;
  return (ret);
}

void library_version (int& major, int& minor)
{
  major = MAJOR, minor = MINOR;
}

} // namespace rcu

#if defined (__MINGW32__) || defined (__MINGW64__)

static void
tl_set (void *ptr)
{
  static xrcu::key_handler handler;
  handler.set (ptr);
}

#endif

