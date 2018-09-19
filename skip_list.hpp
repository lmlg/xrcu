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

void* sl_alloc_node (unsigned int lvl, size_t size, uintptr_t **outpp);
void sl_dealloc_node (void *base);

template <class T>
struct sl_node : public finalizable
{
  unsigned int nlvl;
  optional<T> key;
  uintptr_t *next;

  void init (unsigned int lvl, uintptr_t *np)
    {
      this->nlvl = lvl;
      this->next = np;
    }

  static sl_node<T>* copy (unsigned int lvl, const T& k)
    {
      uintptr_t *np;
      auto self = (sl_node<T> *)sl_alloc_node (lvl, sizeof (sl_node<T>), &np);

      self->init (lvl, np);
      try
        {
          *self->key = k;
          return (self);
        }
      catch (...)
        {
          sl_dealloc_node (self);
          throw;
        }
    }

  template <class ...Args>
  static sl_node<T>* move (unsigned int lvl, Args... args)
    {
      uintptr_t *np;
      auto self = (sl_node<T> *)sl_alloc_node (lvl, sizeof (sl_node<T>), &np);

      self->init (lvl, np);
      new (&*self->key) optional<T> (std::forward<Args&&>(args)...);
      return (self);
    }

  void safe_destroy ()
    {
      sl_dealloc_node (this);
    }
};

static const uintptr_t SL_XBIT = 1;
static const unsigned int SL_MAX_DEPTH = 32;

static const int SL_UNLINK_NONE = 0;
static const int SL_UNLINK_ASSIST = 1;
static const int SL_UNLINK_FORCE = 2;

} // namespace detail

template <class T, class Cmp = std::less<T> >
struct skip_list
{
  detail::sl_node<T> *head;
  Cmp cmpfn;
  unsigned int max_depth;
  std::atomic<size_t> hi_water {0};
  std::atomic<size_t> nelems {0};

  typedef detail::sl_node<T> node_type;

  void _Init (Cmp c, unsigned int depth)
    {
      this->cmpfn = c;
      if ((this->max_depth = depth) > detail::SL_MAX_DEPTH)
        this->max_depth = detail::SL_MAX_DEPTH;

      uintptr_t *np;
      this->head = (node_type *)
          detail::sl_alloc_node (this->max_depth,
                                 sizeof (*this->head), &np);

      this->head->init (this->max_depth, np);
    }

  skip_list (Cmp c = Cmp (), unsigned int depth = 24)
    {
      this->_Init (c, depth);
    }

  node_type* _Node (uintptr_t addr) const
    {
      return ((node_type *)(addr & ~detail::SL_XBIT));
    }

  uintptr_t& _Node_at (uintptr_t addr, unsigned int lvl) const
    {
      return (this->_Node(addr)->next[lvl]);
    }

  unsigned int _Node_lvl (uintptr_t addr) const
    {
      return (this->_Node(addr)->nlvl);
    }

  unsigned int _Rand_lvl ()
    {
      size_t lvl = ctz (xrand ()) * 2 / 3;

      if (lvl == 0)
        return (1);
      else
        while (true)
          {
            auto prev = this->_Hiwater ();
            if (lvl <= prev || prev == detail::SL_MAX_DEPTH)
              break;
            else if (this->hi_water.compare_exchange_weak (prev, prev + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed))
              {
                lvl = prev;
                break;
              }

            xatomic_spin_nop ();
          }

      return (lvl);
    }

  const T& _Getk (uintptr_t addr) const
    {
      return (*this->_Node(addr)->key);
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
          uintptr_t next = this->_Node_at (pr, lvl);
          if (next == 0 && lvl >= n)
            continue;
          else if (next & detail::SL_XBIT)
            goto retry;

          for (it = next; it != 0; )
            {
              next = this->_Node_at (it, lvl);
              while (next & detail::SL_XBIT)
                {
                  if (unlink == detail::SL_UNLINK_NONE)
                    { // Skip logically deleted elements.
                      if ((it = next & ~detail::SL_XBIT) == 0)
                        break;

                      next = this->_Node_at (it, lvl);
                    }
                  else
                    {
                      uintptr_t qx = xatomic_cas (&this->_Node_at (pr, lvl),
                                                  it, next & ~detail::SL_XBIT);
                      if (qx == it)
                        it = next & ~detail::SL_XBIT;
                      else
                        {
                          if (qx & detail::SL_XBIT)
                            goto retry;

                          it = qx;
                        }

                      next = it ? this->_Node_at (it, lvl) : 0;
                    }
                }

              if (it == 0 || this->cmpfn (key, this->_Getk (it)) ||
                  (unlink != detail::SL_UNLINK_FORCE &&
                    (got = !this->cmpfn (this->_Getk (it), key))))
                  break;

              pr = it, it = next;
            }

          if (preds)
            preds[lvl] = pr;
          if (succs)
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
      size_t n = this->_Rand_lvl ();
      if (this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST,
          preds, succs) != 0)
        return (false);

      uintptr_t nv = (uintptr_t)node_type::copy (n, key);
      uintptr_t next = this->_Node_at(nv, 0) = *succs;

      for (int lvl = 1; lvl < n; ++lvl)
        this->_Node_at(nv, lvl) = succs[lvl];

      uintptr_t pred = *preds;
      if (!xatomic_cas_bool (&this->_Node_at(pred, 0), next, nv))
        {
          this->_Node(nv)->safe_destroy ();
          goto retry;
        }

      for (int lvl = 1; lvl < n; ++lvl)
        while (true)
          {
            pred = preds[lvl];
            if (xatomic_cas_bool (&this->_Node_at(pred, lvl),
                                  succs[lvl], nv))
              break;   // Successful link.

            this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST, preds, succs);
            for (int ix = lvl; ix < n; ++ix)
              if ((pred = this->_Node_at (nv, ix)) == succs[ix])
                continue;
              else if (xatomic_cas (&this->_Node_at (nv, ix), pred,
                                    succs[ix]) & detail::SL_XBIT)
                { // Another thread is removing this very key - Bail out.
                  this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
                  return (false);
                }
          }

      if (this->_Node_at (nv, n - 1) & detail::SL_XBIT)
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

      for (int lvl = this->_Node_lvl (it) - 1; lvl >= 0; --lvl)
        {
          qx = this->_Node_at (it, lvl);
          do
            {
              next = qx;
              qx = xatomic_or (&this->_Node_at (it, lvl), detail::SL_XBIT);
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

} // namespace xrcu

#endif