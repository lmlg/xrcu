#ifndef __SKIP_LIST_HPP__
#define __SKIP_LIST_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "optional.hpp"
#include <atomic>
#include <functional>
#include <cstdint>

namespace xrcu
{

namespace detail
{

struct sl_node_base
{
  unsigned int nlvl;
  uintptr_t *next;

  sl_node_base (unsigned int lvl, uintptr_t *np) : nlvl (lvl), next (np) {}

  static sl_node_base* alloc (unsigned int lvl, size_t size);
  static void dealloc (void *ptr);
};

template <class T>
struct sl_node : public sl_node_base, finalizable
{
  T key;

  static sl_node<T>* copy (unsigned int lvl, const T& k)
    {
      auto self = (sl_node<T> *)sl_node_base::alloc (lvl, sizeof (*self));
      try
        {
          new (&self->key) T (k);
        }
      catch (...)
        {
          sl_node_base::dealloc (self, sizeof (*self));
          throw;
        }

      return (self);
    }

  template <class Args...>
  static sl_node<T>* move (unsigned int lvl, Args... args)
    {
      auto self = (sl_node<T> *)sl_node_base::alloc (lvl, sizeof (*self));
      new (&self->key) T (std::forward<Args&&>(args)...);
      return (self);
    }

  void safe_destroy ()
    {
      this->key.~T ();
      sl_node_base::dealloc (this);
    }
};

static const uintptr_t SL_XBIT = 1;
static const unsigned int SL_MAX_DEPTH = 32;

inline uintptr_t&
node_at (uintptr_t obj, size_t idx)
{
  auto np = (sl_node_base *)(obj & ~SL_XBIT);
  return (np->next[idx]);
}

} // namespace detail

template <class T, class Cmp = std::less_than<T> >
struct skip_list
{
  detail::sl_node_base *head;
  Cmp cmpfn;
  unsigned int max_depth;
  std::atomic<size_t> hi_water {0};
  std::atomic<size_t> nelems {0};

  void _Init (Cmp c, unsigned int depth)
    {
      this->cmpfn = c;
      if ((this->max_depth = depth) > detail::SL_MAX_DEPTH)
        this->max_depth = detail::SL_MAX_DEPTH;

      this->head = detail::sl_node_base::alloc (this->max_depth,
                                                sizeof (*this->head));
    }

  skip_list (Cmp c = Cmp (), unsigned int depth = 24)
    {
      this->_Init (c, depth);
    }

  detail::sl_node<T>* _Node (uintptr_t addr) const
    {
      return ((sl_node<T> *)(addr & ~detail::SL_XBIT));
    }

  const T& _Getk (uintptr_t addr) const
    {
      return (this->_Node(addr)->key);
    }

  size_t _Hiwater () const
    {
      return (this->hi_water.load (std::memory_order_relaxed));
    }

  uintptr_t _Find_preds (int n, const T& key, int unlink,
      uintptr_t *preds = nullptr, uintptr_t *succs = nullptr)
    {
      bool got = false;
      uintptr_t pr = (uintptr_t)this->head, it = 0;

    retry:
      for (int lvl = (int)this->_Hiwater (); lvl >= 0; --lvl)
        {
          uintptr_t next = detail::node_at (pr, lvl);
          if (next == 0 && lvl >= n)
            continue;
          else if (next & detail::SL_XBIT)
            goto retry;

          for (it = next; it != 0; )
            {
              next = detail::node_at (it, lvl);
              while (next & detail::SL_XBIT)
                {
                  if (unlink == detail::SL_UNLINK_NONE)
                    { // Skip logically deleted elements.
                      if ((it = next & ~detail::SL_XBIT) == 0)
                        break;

                      next = detail::node_at (it, lvl);
                    }
                  else
                    {
                      uintptr_t qx = xatomic_cas (&detail::node_at (pr, lvl),
                                                  it, next & ~detail::SL_XBIT);
                      if (qx == it)
                        it = next & ~detail::SL_XBIT;
                      else
                        {
                          if (qx & detail::SL_XBIT)
                            goto retry;

                          it = qx;
                        }

                      next = it ? detail::node_at (it, lvl) : 0;
                    }
                }

              if (it == 0 || this->cmpfn (key, this->_Getk (it)) ||
                  (unlink != detail::SL_UNLINK_FORCE &&
                    (got = !this->cmpfn (this->_Getk (it), key))))
                  break;

              pr = it, it = next;
            }

          if (preds && lvl < n)
            preds[lvl] = pr;
          if (succs && lvl < n)
            succs[lvl] = it;
        }

      return (got ? it : 0);
    }

  optional<T> find (const T& key) const
    {
      cs_guard g;

      uintptr_t rv = this->_Find_preds (0, key, detail::SL_UNLINK_NONE);
      return (rv ? optional<T> (this->_Getk (rv)) : optional<T> ());
    }

  bool insert (const T& key)
    {
      cs_guard g;

    retry:
      uintptr_t preds[detail::SL_MAX_DEPTH], succs[detail::SL_MAX_DEPTH];
      int n = this->_Rand_lvl ();
      if (this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST,
          preds, succs) != 0)
        return (false);

      uintptr_t nv = this->_Alloc_node (n);
      uintptr_t next = detail::node_at(nv, 0) = *succs;

      for (int lvl = 1; lvl < n; ++lvl)
        detail::node_at(nv, lvl) = succs[lvl];

      uintptr_t pred = *preds;
      if (!xatomic_cas_bool (&detail::node_at(pred, 0), next, nv))
        {
          this->_Node(nv)->safe_destroy ();
          goto retry;
        }

      for (int lvl = 1; lvl < n; ++lvl)
        while (true)
          {
            pred = preds[lvl];
            if (xatomic_cas_bool (&detail::node_at(pred, lvl),
                                  succs[lvl], nv))
              break;   // Successful link.

            this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST, preds, succs);
            for (int ix = lvl; ix < n; ++ix)
              if ((pred = detail::node_at (nv, ix)) == succs[ix])
                continue;
              else if (xatomic_cas (&detail::node_at (nv, ix), pred,
                                    succs[ix]) & detail::SL_XBIT)
                { // Another thread is removing this very key - Bail out.
                  this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
                  return (false);
                }
          }

      if (detail::node_at (nv, n - 1) & detail::SL_XBIT)
        { // Another thread is removing this key - Make sure it's unlinked.
          this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
          return (false);
        }

      this->nelems.fetch_add (1, std::memory_order_acq_rel);
      return (true);
    }

  uintptr_t _Erase (const T& key)
    {
      uintptr_t preds[detail::SL_MAX_DEPTH];
      uintptr_t it = this->_Find_preds (this->_Hiwater (), key,
                                        detail::SL_UNLINK_ASSIST, preds);

      if (it == 0)
        return (it);

      uintptr_t qx = 0, next = 0;

      for (int lvl = detail::node_nlvl (it) - 1; lvl >= 0; --lvl)
        {
          qx = detail::node_at (it, lvl);
          do
            {
              next = qx;
              qx = xatomic_or (&detail::node_at (it, lvl), detail::SL_XBIT);
              if (qx & detail::SL_XBIT)
                {
                  if (lvl == 0)
                    return (false);

                  break;
                }
            }
          while (next != qx);
        }

      // Unlink the item.
      this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
      this->nelems.fetch_sub (1, std::memory_order_acq_rel);
      return (it);
    }

  bool erase (const T& key)
    {
      cs_guard g;
      return (this->_Erase (key) != 0);
    }

  optional<T> remove (const T& key)
    {
      cs_guard g;
      uintptr_t it = this->_Erase (key);
      return (it ? optional<T> (this->_Getk (it)) : optional<T> ());
    }
};

}

#endif
