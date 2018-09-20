#ifndef __SKIP_LIST_HPP__
#define __SKIP_LIST_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "optional.hpp"
#include <atomic>
#include <functional>
#include <cstdint>

namespace std
{
  struct forward_iterator_tag;
}

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
  uintptr_t *next;
  optional<T> key;

  sl_node (unsigned int lvl, uintptr_t *np) : nlvl (lvl), next (np)
    {
    }

  static sl_node<T>* copy (unsigned int lvl, const T& k)
    {
      uintptr_t *np;
      auto self = (sl_node<T> *)sl_alloc_node (lvl, sizeof (sl_node<T>), &np);

      new (self) sl_node<T> (lvl, np);

      try
        {
          self->key = k;
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

      new (self->key) sl_node<T> (lvl, np);
      new (&*self->key) optional<T> (std::forward<Args&&>(args)...);
      return (self);
    }

  void safe_destroy ()
    {
      this->key.reset ();
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
  std::atomic<size_t> hi_water { 1 };
  std::atomic<size_t> nelems { 0 };

  typedef detail::sl_node<T> node_type;
  typedef skip_list<T, Cmp> _Self;

  void _Init (Cmp c, unsigned int depth)
    {
      this->cmpfn = c;
      if ((this->max_depth = depth) > detail::SL_MAX_DEPTH)
        this->max_depth = detail::SL_MAX_DEPTH;

      uintptr_t *np;
      this->head = (node_type *)
          detail::sl_alloc_node (this->max_depth,
                                 sizeof (*this->head), &np);

      new (this->head) node_type (this->max_depth, np);
    }

  skip_list (Cmp c = Cmp (), unsigned int depth = 24)
    {
      this->_Init (c, depth);
    }

  static node_type* _Node (uintptr_t addr)
    {
      return ((node_type *)(addr & ~detail::SL_XBIT));
    }

  static uintptr_t& _Node_at (uintptr_t addr, unsigned int lvl)
    {
      return (_Self::_Node(addr)->next[lvl]);
    }

  static unsigned int _Node_lvl (uintptr_t addr)
    {
      return (_Self::_Node(addr)->nlvl);
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

  static const T& _Getk (uintptr_t addr)
    {
      return (*_Self::_Node(addr)->key);
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
      for (int lvl = (int)this->_Hiwater () - 1; lvl >= 0; --lvl)
        {
          uintptr_t next = _Self::_Node_at (pr, lvl);
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

                      next = _Self::_Node_at (it, lvl);
                    }
                  else
                    {
                      uintptr_t qx = xatomic_cas (&_Self::_Node_at (pr, lvl),
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

              if (it == 0 || this->cmpfn (key, _Self::_Getk (it)) ||
                  (unlink != detail::SL_UNLINK_FORCE &&
                    (got = !this->cmpfn (_Self::_Getk (it), key))))
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
      uintptr_t preds[detail::SL_MAX_DEPTH], succs[detail::SL_MAX_DEPTH];

      for (int i = 0; i < detail::SL_MAX_DEPTH; ++i)
        preds[i] = succs[i] = 0;

    retry:
      size_t n = this->_Rand_lvl ();
      if (this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST,
          preds, succs) != 0)
        return (false);

      uintptr_t nv = (uintptr_t)node_type::copy (n, key);
      uintptr_t next = _Self::_Node_at(nv, 0) = *succs;

      for (int lvl = 1; lvl < n; ++lvl)
        this->_Node_at(nv, lvl) = succs[lvl];

      uintptr_t pred = *preds;
      if (!xatomic_cas_bool (&_Self::_Node_at(pred, 0), next, nv))
        {
          _Self::_Node(nv)->safe_destroy ();
          goto retry;
        }

      for (int lvl = 1; lvl < n; ++lvl)
        while (true)
          {
            pred = preds[lvl];
            if (xatomic_cas_bool (&_Self::_Node_at(pred, lvl),
                                  succs[lvl], nv))
              break;   // Successful link.

            this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST, preds, succs);
            for (int ix = lvl; ix < n; ++ix)
              if ((pred = this->_Node_at (nv, ix)) == succs[ix])
                continue;
              else if (xatomic_cas (&_Self::_Node_at (nv, ix), pred,
                                    succs[ix]) & detail::SL_XBIT)
                { // Another thread is removing this very key - Bail out.
                  this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
                  return (false);
                }
          }

      if (_Self::_Node_at (nv, n - 1) & detail::SL_XBIT)
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

      node_type *nodep = _Self::_Node (it);
      uintptr_t qx = 0, next = 0;

      for (int lvl = nodep->nlvl - 1; lvl >= 0; --lvl)
        {
          qx = nodep->next[lvl];
          do
            {
              next = qx;
              qx = xatomic_or (&nodep->next[lvl], detail::SL_XBIT);
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
      finalize (nodep);
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
      return (it ? optional<T> (_Self::_Getk (it)) : optional<T> ());
    }

  struct iterator : public cs_guard
    {
      uintptr_t node;

      iterator (uintptr_t addr) : node (addr) {}

      iterator& operator++ ()
        {
          while (true)
            {
              if (this->node == 0)
                break;

              this->node = skip_list<T, Cmp>::_Node_at (this->node, 0);
              if ((this->node & detail::SL_XBIT) == 0)
                break;
            }

          return (*this);
        }

      iterator operator++ (int)
        {
          iterator tmp { this->node };
          ++*this;
          return (tmp);
        }

      const T& operator* () const
        {
          return (skip_list<T, Cmp>::_Getk (this->node));
        }

      bool operator== (const iterator& right) const
        {
          return (this->node == right.node);
        }

      bool operator!= (const iterator& right) const
        {
          return (this->node != right.node);
        }
    };

  typedef iterator const_iterator;

  const_iterator cbegin () const
    {
      return (iterator (_Self::_Node_at ((uintptr_t)this->head, 0)));
    }

  iterator begin ()
    {
      return (this->cbegin ());
    }

  const_iterator begin () const
    {
      return (this->cbegin ());
    }

  const_iterator cend () const
    {
      return (iterator (0));
    }

  iterator end ()
    {
      return (this->cend ());
    }

  const_iterator end () const
    {
      return (this->cend ());
    }

  ~skip_list ()
    {
      uintptr_t run = (uintptr_t)this->head;

      while (run != 0)
        {
          uintptr_t next = _Self::_Node_at (run, 0);
          _Self::_Node(run)->safe_destroy ();
          run = next;
        }
    }
};

} // namespace xrcu

#endif
