#ifndef __SKIP_LIST_HPP__
#define __SKIP_LIST_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "optional.hpp"
#include <atomic>
#include <functional>
#include <initializer_list>

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

      new (self) sl_node<T> (lvl, np);
      new (&self->key) optional<T> (std::forward<Args&&>(args)...);
      return (self);
    }

  void safe_destroy ()
    {
      sl_dealloc_node (this);
    }
};

static const uintptr_t SL_XBIT = 1;
static const unsigned int SL_MAX_DEPTH = 24;

static const int SL_UNLINK_NONE = 0;
static const int SL_UNLINK_ASSIST = 1;
static const int SL_UNLINK_FORCE = 2;

inline void
init_preds_succs (uintptr_t *p1, uintptr_t *p2)
{
  for (unsigned int i = 0; i < SL_MAX_DEPTH; ++i)
    p1[i] = p2[i] = 0;
}

} // namespace detail

template <class T, class Cmp = std::less<T> >
struct skip_list
{
  std::atomic<detail::sl_node<T> *> head;
  Cmp cmpfn;
  unsigned int max_depth;
  std::atomic<size_t> hi_water { 1 };

  typedef detail::sl_node<T> node_type;
  typedef skip_list<T, Cmp> _Self;

  node_type* _Make_root ()
    {
      uintptr_t *np;
      auto ret = (node_type *)
        detail::sl_alloc_node (this->max_depth + 1,
                               sizeof (node_type), &np);

      new (ret) node_type (this->max_depth, np + 1);
      return (ret);
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

  uintptr_t* _Root_plen (uintptr_t addr)
    {
      return (_Self::_Node(addr)->next - 1);
    }

  void _Bump_len (uintptr_t *lenp, intptr_t off)
    {
      xatomic_add (lenp, off + off);
    }

  void _Init (Cmp c, unsigned int depth)
    {
      this->cmpfn = c;
      if ((this->max_depth = depth) > detail::SL_MAX_DEPTH)
        this->max_depth = detail::SL_MAX_DEPTH;

      this->head.store (this->_Make_root (), std::memory_order_relaxed);
    }

  skip_list (Cmp c = Cmp (), unsigned int depth = detail::SL_MAX_DEPTH)
    {
      this->_Init (c, depth);
    }

  template <class Iter>
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

  unsigned int _Rand_lvl ()
    {
      size_t lvl = ctz (xrand ()) * 2 / 3;

      if (lvl == 0)
        return (1);
      while (true)
        {
          auto prev = this->_Hiwater ();
          if (lvl <= prev)
            return (lvl);
          else if (prev == detail::SL_MAX_DEPTH ||
              this->hi_water.compare_exchange_weak (prev, prev + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed))
            return (prev);

          xatomic_spin_nop ();
        }
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
      uintptr_t *preds = nullptr, uintptr_t *succs = nullptr,
      uintptr_t *outp = nullptr) const
    {
      bool got = false;
      uintptr_t pr = (uintptr_t)this->head.load (std::memory_order_relaxed);
      uintptr_t it = 0;

      if (outp)
        *outp = pr;

      for (int lvl = (int)this->_Hiwater () - 1; lvl >= 0; --lvl)
        {
          uintptr_t next = _Self::_Node_at (pr, lvl);
          if (next == 0 && lvl >= n)
            continue;
          else if (next & detail::SL_XBIT)
            return (this->_Find_preds (n, key, unlink, preds, succs, outp));

          for (it = next; it != 0; )
            {
              next = _Self::_Node_at (it, lvl);
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
                      uintptr_t qx = xatomic_cas (&_Self::_Node_at(pr, lvl),
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

                      next = it ? _Self::_Node_at (it, lvl) : 0;
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

      return (got ? it : 0);
    }

  optional<T> find (const T& key) const
    {
      cs_guard g;
      uintptr_t rv = this->_Find_preds (0, key, detail::SL_UNLINK_NONE);
      return (rv ? optional<T> (this->_Getk (rv)) : optional<T> ());
    }

  bool _Insert (const T& key)
    {
      uintptr_t xroot;
      uintptr_t preds[detail::SL_MAX_DEPTH], succs[detail::SL_MAX_DEPTH];

      detail::init_preds_succs (preds, succs);

      size_t n = this->_Rand_lvl ();
      if (this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST,
          preds, succs, &xroot) != 0)
        return (false);

      uintptr_t nv = (uintptr_t)node_type::copy (n, key);

      for (size_t lvl = 0; lvl < n; ++lvl)
        _Self::_Node_at(nv, lvl) = succs[lvl];

      uintptr_t pred = *preds;
      if (!xatomic_cas_bool (&_Self::_Node_at(pred, 0), *succs, nv))
        {
          _Self::_Node(nv)->safe_destroy ();
          return (this->_Insert (key));
        }

      for (size_t lvl = 1; lvl < n; ++lvl)
        while (true)
          {
            pred = preds[lvl];
            if (xatomic_cas_bool (&_Self::_Node_at(pred, lvl),
                                  succs[lvl], nv))
              break;   // Successful link.

            this->_Find_preds (n, key, detail::SL_UNLINK_ASSIST, preds, succs);
            for (size_t ix = lvl; ix < n; ++ix)
              if ((pred = _Self::_Node_at (nv, ix)) == succs[ix])
                continue;
              else if (xatomic_cas (&_Self::_Node_at(nv, ix),
                                    pred, succs[ix]) & detail::SL_XBIT)
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

      this->_Bump_len (this->_Root_plen (xroot), 1);
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

      node_type *nodep = _Self::_Node (it);
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
      this->_Bump_len (this->_Root_plen (xroot), -1);
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
      auto tmp = (uintptr_t)this->head.load (std::memory_order_relaxed);
      return (iterator (_Self::_Node_at (tmp, 0)));
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

  size_t size () const
    {
      cs_guard g;
      uintptr_t ret = *(this->head.load(std::memory_order_relaxed)->next - 1);
      return (ret >> 1);
    }

  bool empty () const
    {
      return (this->size () == 0);
    }

  template <bool Destroy = false>
  void _Fini_root (node_type *xroot)
    {
      for (uintptr_t run = (uintptr_t)xroot; run != 0; )
        {
          uintptr_t next = _Self::_Node_at (run, 0);

          if (Destroy)
            _Self::_Node(run)->safe_destroy ();
          else
            finalize (_Self::_Node (run));

          run = next;
        }
    }

  void _Lock_root ()
    {
      cs_guard g;
      while (true)
        {
          auto ptr = this->_Root_plen ((uintptr_t)
            this->head.load (std::memory_order_relaxed));
          auto val = *ptr;

          if ((val & 1) == 0 && xatomic_cas_bool (ptr, val, val | 1))
            break;

          xatomic_spin_nop ();
        }
    }

  template <class Iter>
  void assign (Iter first, Iter last)
    {
      _Self tmp (first, last);
      this->_Lock_root ();
      auto old = this->head.load (std::memory_order_relaxed);
      this->head.store (tmp.head.load (std::memory_order_relaxed),
                        std::memory_order_release);
      tmp.head.store (old, std::memory_order_relaxed);
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
      this->_Lock_root ();
      auto old = this->head.load (std::memory_order_relaxed);
      this->head.store (right.head.load (std::memory_order_relaxed),
                        std::memory_order_release);
      this->_Fini_root<> (old);
    }

  void swap (_Self& right)
    {
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

      xatomic_and (this->_Root_plen ((uintptr_t)rh), ~(uintptr_t)1);
      xatomic_and (right._Root_plen ((uintptr_t)lh), ~(uintptr_t)1);
    }

  void clear ()
    {
      auto xroot = this->_Make_root ();
      this->_Lock_root ();
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

template <class T, class Cmp>
void swap (xrcu::skip_list<T, Cmp>& left,
           xrcu::skip_list<T, Cmp>& right)
{
  left.swap (right);
}

} // namespace std

#endif
