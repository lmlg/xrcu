/* Declarations for the skip list template type.

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

#ifndef __SKIP_LIST_HPP__
#define __SKIP_LIST_HPP__   1

#include "memory.hpp"
#include "xrcu.hpp"
#include "xatomic.hpp"
#include "utils.hpp"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>

namespace std
{
  struct forward_iterator_tag;
}

namespace xrcu
{

namespace detail
{

static const uintptr_t SL_XBIT = 1;
static const unsigned int SL_MAX_DEPTH = 24;

static const int SL_UNLINK_SKIP = -1;
static const int SL_UNLINK_NONE = 0;
static const int SL_UNLINK_ASSIST = 1;
static const int SL_UNLINK_FORCE = 2;

template <typename Alloc>
struct alignas (uintptr_t) sl_node_base : public finalizable
{
  unsigned int nlvl;
  uintptr_t *next;

  sl_node_base (unsigned int lvl, uintptr_t *np) : nlvl (lvl), next (np)
    {
    }

  static sl_node_base* get (uintptr_t addr)
    {
      return ((sl_node_base *)(addr & ~SL_XBIT));
    }

  static uintptr_t& at (uintptr_t addr, unsigned int lvl)
    {
      return (sl_node_base::get(addr)->next[lvl]);
    }

  static uintptr_t* plen (uintptr_t addr)
    {
      return (sl_node_base::get(addr)->next - 1);
    }

  static void bump (uintptr_t *lenp, intptr_t off)
    {
      xatomic_add (lenp, off + off);
    }

  static sl_node_base* make_root (unsigned int depth)
    {
      size_t uptrs;
      uintptr_t *raw = alloc_uptrs<Alloc> (sizeof (sl_node_base),
                                           depth + 1, &uptrs);
      memset (raw, 0, uptrs * sizeof (uintptr_t));
      auto ret = (sl_node_base *)raw;
      uintptr_t *endp = (uintptr_t *)(ret + 1);
      return (new (ret) sl_node_base (depth, endp + 1));
    }

  static unsigned int
  rand_lvl (std::atomic<size_t>& hw)
    {
      size_t lvl = ctz (xrand ()) * 2 / 3;

      if (lvl == 0)
        return (1);
      while (true)
        {
          auto prev = hw.load (std::memory_order_relaxed);
          if (lvl <= prev)
            return (lvl);
          else if (prev == SL_MAX_DEPTH ||
                   hw.compare_exchange_weak (prev, prev + 1,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed))
            return (prev);

          xatomic_spin_nop ();
        }
    }

  void safe_destroy ()
    {
      dealloc_uptrs<Alloc> (this, this->next + this->nlvl);
    }
};

template <typename T, typename Alloc>
struct sl_node : public sl_node_base<Alloc>
{
  T key;
  typedef sl_node<T, Alloc> _Self;

  sl_node (unsigned int lvl, uintptr_t *np, const T& kv) :
      sl_node_base<Alloc> (lvl, np), key (kv)
    {
    }

  template <typename ...Args>
  sl_node (unsigned int lvl, uintptr_t *np, Args... args) :
      sl_node_base<Alloc> (lvl, np), key (std::forward<Args&&>(args)...)
    {
    }

  static sl_node<T, Alloc>* copy (unsigned int lvl, const T& k)
    {
      size_t uptrs;
      uintptr_t *raw = alloc_uptrs<Alloc> (sizeof (_Self), lvl, &uptrs);
      auto ret = (sl_node<T, Alloc> *)raw;
      try
        {
          uintptr_t *endp = (uintptr_t *)(ret + 1);
          return (new (ret) sl_node<T, Alloc> (lvl, endp, k));
        }
      catch (...)
        {
          dealloc_uptrs<Alloc> (raw, raw + uptrs);
          throw;
        }
    }

  template <typename ...Args>
  static sl_node<T, Alloc>* move (unsigned int lvl, Args... args)
    {
      auto ret = (_Self *)alloc_uptrs<Alloc> (sizeof (_Self), lvl);
      uintptr_t *np = (uintptr_t *)(ret + 1);
      return (new (ret) sl_node<T, Alloc> (lvl, np,
                                           std::forward<Args&&>(args)...));
    }

  void safe_destroy ()
    {
      destroy<T> (&this->key);
      dealloc_uptrs<Alloc> (this, this->next + this->nlvl);
    }
};

inline void
init_preds_succs (uintptr_t *p1, uintptr_t *p2)
{
  for (unsigned int i = 0; i < SL_MAX_DEPTH; ++i)
    p1[i] = p2[i] = 0;
}

} // namespace detail

template <typename T, typename Cmp = std::less<T>,
          typename Alloc = std::allocator<T>>
struct skip_list
{
  using Nalloc = typename std::allocator_traits<Alloc>::template
                 rebind_alloc<uintptr_t>;

  std::atomic<detail::sl_node_base<Nalloc> *> head;
  Cmp cmpfn;
  unsigned int max_depth;
  std::atomic<size_t> hi_water { 1 };

  typedef T value_type;
  typedef T key_type;
  typedef Cmp key_compare;
  typedef Cmp value_compare;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef skip_list<T, Cmp, Alloc> _Self;
  typedef detail::sl_node<T, Nalloc> _Node;

  uintptr_t _Head () const
    {
      return ((uintptr_t)this->head.load (std::memory_order_relaxed));
    }

  size_t _Hiwater () const
    {
      return (this->hi_water.load (std::memory_order_relaxed));
    }

  void _Init (Cmp c, unsigned int depth)
    {
      this->cmpfn = c;
      if ((this->max_depth = depth) > detail::SL_MAX_DEPTH)
        this->max_depth = detail::SL_MAX_DEPTH;

      this->head.store (_Node::make_root (this->max_depth + 1),
                        std::memory_order_relaxed);
    }

  skip_list (Cmp c = Cmp (), unsigned int depth = detail::SL_MAX_DEPTH)
    {
      this->_Init (c, depth);
    }

  template <typename Iter>
  skip_list (Iter first, Iter last,
      Cmp c = Cmp (), unsigned int depth = detail::SL_MAX_DEPTH)
    {
      this->_Init (c, depth);
      for (; first != last; ++first)
        this->insert (*first);
    }

  skip_list (std::initializer_list<T> lst,
      Cmp c = Cmp (), unsigned int depth = detail::SL_MAX_DEPTH) :
        skip_list (lst.begin (), lst.end (), c, depth)
    {
    }

  skip_list (const _Self& right) : skip_list (right.begin (), right.end ())
    {
    }

  skip_list (_Self&& right)
    {
      this->head.store (right.head.load (std::memory_order_relaxed),
                        std::memory_order_relaxed);
      this->cmpfn = right.cmpfn;
      this->max_depth = right.max_depth;
      this->hi_water.store (right.hi_water.load (std::memory_order_relaxed),
                            std::memory_order_relaxed);

      right.head.store (nullptr, std::memory_order_relaxed);
    }

  struct iterator : public cs_guard
    {
      uintptr_t node;

      typedef std::forward_iterator_tag iterator_category;

      iterator () : node (0)
        {
        }

      iterator (uintptr_t addr) : node (addr)
        {
        }

      iterator (const iterator& right) : node (right.node)
        {
        }

      iterator (iterator&& right) : node (right.node)
        {
          right.node = 0;
        }

      iterator& operator++ ()
        {
          while (true)
            {
              if (this->node == 0)
                break;

              this->node = _Node::at (this->node, 0);
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
          return (skip_list<T, Cmp, Alloc>::_Getk (this->node));
        }

      const T* operator-> () const
        {
          return (&**this);
        }

      iterator& operator= (const iterator& right)
        {
          this->node = right.node;
          return (*this);
        }

      iterator& operator= (iterator&& right)
        {
          this->node = right.node;
          right.node = 0;
          return (*this);
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

  static const T& _Getk (uintptr_t addr)
    {
      return (((_Node *)_Node::get(addr))->key);
    }

  uintptr_t _Find_preds (int n, const T& key, int unlink,
      uintptr_t *preds = nullptr, uintptr_t *succs = nullptr,
      uintptr_t *outp = nullptr) const
    {
      bool got = false;
      uintptr_t pr = this->_Head (), it = 0;

      if (outp)
        *outp = pr;

      for (int lvl = (int)this->_Hiwater () - 1; lvl >= 0; --lvl)
        {
          uintptr_t next = _Node::at (pr, lvl);
          if (next == 0 && lvl >= n)
            continue;
          else if (next & detail::SL_XBIT)
            return (this->_Find_preds (n, key, unlink, preds, succs, outp));

          for (it = next; it != 0; )
            {
              next = _Node::at (it, lvl);
              while (next & detail::SL_XBIT)
                {
                  if (unlink == detail::SL_UNLINK_SKIP ||
                      unlink == detail::SL_UNLINK_NONE)
                    { // Skip logically deleted elements.
                      if ((it = next & ~detail::SL_XBIT) == 0)
                        break;

                      next = _Node::at (it, lvl);
                    }
                  else
                    {
                      uintptr_t qx = xatomic_cas (&_Node::at(pr, lvl),
                                                  it, next & ~detail::SL_XBIT);
                      if (qx == it)
                        it = next & ~detail::SL_XBIT;
                      else
                        {
                          if (qx & detail::SL_XBIT)
                            return (this->_Find_preds (n,
                              key, unlink, preds, succs, outp));

                          it = qx;
                        }

                      next = it ? _Node::at (it, lvl) : 0;
                    }
                }

              if (it == 0 || this->cmpfn (key, _Self::_Getk (it)) ||
                  (unlink != detail::SL_UNLINK_FORCE &&
                    (got = !this->cmpfn (_Self::_Getk (it), key))))
                break;

              pr = it, it = next;
            }

          if (preds)
            preds[lvl] = pr, succs[lvl] = it;
        }

      return (got || unlink == detail::SL_UNLINK_SKIP ? it : 0);
    }

  std::optional<T> find (const T& key) const
    {
      cs_guard g;
      uintptr_t rv = this->_Find_preds (0, key, detail::SL_UNLINK_NONE);
      return (rv ? std::optional<T> (this->_Getk (rv)) : std::optional<T> ());
    }

  bool contains (const T& key) const
    {
      cs_guard g;
      return (this->_Find_preds (0, key, detail::SL_UNLINK_NONE) != 0);
    }

  const_iterator lower_bound (const T& key) const
    {
      uintptr_t preds[detail::SL_MAX_DEPTH], succs[detail::SL_MAX_DEPTH];
      detail::init_preds_succs (preds, succs);

      cs_guard g;
      this->_Find_preds (0, key, detail::SL_UNLINK_NONE, preds, succs);

      uintptr_t it_val;
      for (auto val : preds)
        if ((it_val = val) != 0)
          break;

      const_iterator ret { it_val };
      if (ret.node == this->_Head ())
        ++ret;

      return (ret);
    }

  const_iterator upper_bound (const T& key) const
    {
      cs_guard g;
      uintptr_t it = this->_Find_preds (0, key, detail::SL_UNLINK_SKIP);
      const_iterator ret { it };
      if (it)
        ++ret;

      return (ret);
    }

  bool _Insert (const T& key)
    {
      uintptr_t xroot;
      uintptr_t preds[detail::SL_MAX_DEPTH], succs[detail::SL_MAX_DEPTH];

      detail::init_preds_succs (preds, succs);

      size_t n = _Node::rand_lvl (this->hi_water);
      if (this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST,
          preds, succs, &xroot) != 0)
        return (false);

      uintptr_t nv = (uintptr_t)_Node::copy (n, key);

      for (size_t lvl = 0; lvl < n; ++lvl)
        _Node::at(nv, lvl) = succs[lvl];

      uintptr_t pred = *preds;
      if (!xatomic_cas_bool (&_Node::at(pred, 0), *succs, nv))
        {
          _Node::get(nv)->safe_destroy ();
          return (this->_Insert (key));
        }

      for (size_t lvl = 1; lvl < n; ++lvl)
        while (true)
          {
            pred = preds[lvl];
            if (xatomic_cas_bool (&_Node::at(pred, lvl), succs[lvl], nv))
              break;   // Successful link.

            this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST, preds, succs);
            for (size_t ix = lvl; ix < n; ++ix)
              if ((pred = _Node::at (nv, ix)) == succs[ix])
                continue;
              else if (xatomic_cas (&_Node::at(nv, ix),
                                    pred, succs[ix]) & detail::SL_XBIT)
                { // Another thread is removing this very key - Bail out.
                  this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
                  return (false);
                }
          }

      if (_Node::at (nv, n - 1) & detail::SL_XBIT)
        { // Another thread is removing this key - Make sure it's unlinked.
          this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
          return (false);
        }

      _Node::bump (_Node::plen (xroot), 1);
      return (true);
    }

  bool insert (const T& key)
    {
      cs_guard g;
      return (this->_Insert (key));
    }

  uintptr_t _Erase (const T& key)
    {
      uintptr_t xroot, it = this->_Find_preds (this->_Hiwater (), key,
                                               detail::SL_UNLINK_NONE,
                                               nullptr, nullptr, &xroot);

      if (it == 0)
        return (it);

      detail::sl_node_base<Nalloc> *nodep = _Node::get (it);
      uintptr_t qx = 0, next = 0;

      for (int lvl = nodep->nlvl - 1; lvl >= 0; --lvl)
        {
          qx = nodep->next[lvl];
          do
            {
              next = qx;
              qx = xatomic_cas (&nodep->next[lvl],
                next, next | detail::SL_XBIT);

              if (qx & detail::SL_XBIT)
                {
                  if (lvl == 0)
                    return (0);
                  break;
                }
            }
          while (next != qx);
        }

      // Unlink the item.
      this->_Find_preds (0, key, detail::SL_UNLINK_FORCE);
      _Node::bump (_Node::plen (xroot), -1);
      finalize (nodep);
      return (it);
    }

  bool erase (const T& key)
    {
      cs_guard g;
      return (this->_Erase (key) != 0);
    }

  std::optional<T> remove (const T& key)
    {
      cs_guard g;
      uintptr_t it = this->_Erase (key);
      return (it ? std::optional<T> (_Self::_Getk (it)) : std::optional<T> ());
    }

  const_iterator cbegin () const
    {
      return (iterator (_Node::at (this->_Head (), 0)));
    }

  iterator begin ()
    {
      return (this->cbegin ());
    }

  const_iterator begin () const
    {
      return (this->cbegin ());
    }

  iterator end ()
    {
      return (this->cend ());
    }

  const_iterator end () const
    {
      return (this->cend ());
    }

  const_iterator cend () const
    {
      return (iterator (0));
    }

  size_t size () const
    {
      cs_guard g;
      uintptr_t *p = this->head.load (std::memory_order_relaxed)->next - 1;
      return (*p >> 1);
    }

  size_t max_size () const
    {
      return ((~(size_t)0) >> 1);
    }

  bool empty () const
    {
      return (this->size () == 0);
    }

  template <bool Destroy = false>
  void _Fini_root (detail::sl_node_base<Nalloc> *xroot)
    {
      for (uintptr_t run = (uintptr_t)xroot; run != 0; )
        {
          uintptr_t next = _Node::at (run, 0);

          if (Destroy)
            _Node::get(run)->safe_destroy ();
          else
            finalize (_Node::get (run));

          run = next;
        }
    }

  void _Lock_root ()
    {
      cs_guard g;
      while (true)
        {
          auto ptr = _Node::plen ((uintptr_t)
            this->head.load (std::memory_order_relaxed));
          auto val = *ptr;

          if ((val & 1) == 0 && xatomic_cas_bool (ptr, val, val | 1))
            break;

          xatomic_spin_nop ();
        }
    }

  template <typename Iter>
  void assign (Iter first, Iter last)
    {
      _Self tmp (first, last);
      auto tp = tmp.head.load (std::memory_order_relaxed);
      tmp.head.store (this->head.exchange (tp, std::memory_order_acq_rel));
    }

  void assign (std::initializer_list<T> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  _Self& operator= (const _Self& right)
    {
      this->assign (right.begin (), right.end ());
      return (*this);
    }

  _Self& operator= (_Self&& right)
    {
      auto tp = right.head.load (std::memory_order_relaxed);
      this->_Fini_root<> (this->head.exchange (tp, std::memory_order_acq_rel));
      right.head.store (nullptr, std::memory_order_relaxed);
    }

  void swap (_Self& right)
    {
      if (this == &right)
        return;

      this->_Lock_root ();
      right._Lock_root ();

      auto lw = this->hi_water.load (std::memory_order_relaxed);
      auto rw = right.hi_water.load (std::memory_order_relaxed);

      this->hi_water.store (rw, std::memory_order_relaxed);
      right.hi_water.store (lw, std::memory_order_relaxed);

      auto lh = this->head.load (std::memory_order_relaxed);
      auto rh = right.head.load (std::memory_order_relaxed);

      this->head.store (rh, std::memory_order_relaxed);
      right.head.store (lh, std::memory_order_relaxed);

      xatomic_and (_Node::plen ((uintptr_t)rh), ~(uintptr_t)1);
      xatomic_and (_Node::plen ((uintptr_t)lh), ~(uintptr_t)1);
    }

  void clear ()
    {
      auto xroot = _Node::make_root (this->max_depth);
      auto prev = this->head.exchange (xroot, std::memory_order_release);
      this->_Fini_root<> (prev);
    }

  ~skip_list ()
    {
      this->_Fini_root<true> (this->head.load (std::memory_order_relaxed));
    }
};

} // namespace xrcu

namespace std
{

template <typename T, typename Cmp, typename Alloc>
void swap (xrcu::skip_list<T, Cmp, Alloc>& left,
           xrcu::skip_list<T, Cmp, Alloc>& right)
{
  left.swap (right);
}

} // namespace std

#endif
