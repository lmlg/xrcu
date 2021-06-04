/* Declarations for the hash set template type.

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

#ifndef __XRCU_HASHSET_HPP__
#define __XRCU_HASHSET_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "lwlock.hpp"
#include "optional.hpp"
#include "utils.hpp"
#include <atomic>
#include <cstddef>
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

struct alignas (uintptr_t) hs_vector : public finalizable
{
  uintptr_t *data;
  size_t entries;
  std::atomic<size_t> nelems { 0 };

  hs_vector (uintptr_t *ep, size_t n) : data (ep), entries (n)
    {
    }

  void safe_destroy ();
  static hs_vector* alloc (size_t n, uintptr_t fill);
};

struct hs_sentry
{
  lwlock *lock;
  uintptr_t xbit;
  hs_vector *vector = nullptr;

  hs_sentry (lwlock *lp, uintptr_t xb);
  ~hs_sentry ();
};

template <class Traits>
struct hs_iter : public cs_guard
{
  const hs_vector *vec = nullptr;
  size_t idx = 0;
  uintptr_t c_key;
  bool valid = false;

  typedef std::forward_iterator_tag iterator_category;

  void _Init (const hs_vector *vp)
    {
      this->vec = vp;
      this->_Adv ();
    }

  hs_iter ()
    {
    }

  hs_iter (const hs_iter<Traits>& right) :
      vec (right.vec), idx (right.idx), c_key (right.c_key), valid (right.valid)
    {
    }

  hs_iter (hs_iter<Traits>&& right) :
      vec (right.vec), idx (right.idx), c_key (right.c_key), valid (right.valid)
    {
      right.vec = nullptr;
      right.valid = false;
    }

  void _Adv ()
    {
      for (this->valid = false; this->idx < this->vec->entries; ++this->idx)
        {
          this->c_key = this->vec->data[this->idx] & ~Traits::XBIT;
          if (this->c_key != Traits::FREE && this->c_key != Traits::DELT)
            {
              this->valid = true;
              break;
            }
        }
    }

  bool operator== (const hs_iter<Traits>& right) const
    {
      return ((!this->valid && !right.valid) ||
        (this->vec == right.vec && this->idx == right.idx));
    }

  bool operator!= (const hs_iter<Traits>& right) const
    {
      return (!(*this == right));
    }
};

} // namespace detail

template <class Key,
  class EqFn = std::equal_to<Key>,
  class HashFn = std::hash<Key> >
struct hash_set
{
  typedef detail::wrapped_traits<(sizeof (Key) < sizeof (uintptr_t) &&
      std::is_integral<Key>::value) || (std::is_pointer<Key>::value &&
      alignof (Key) >= 8), Key> key_traits;

  typedef hash_set<Key, EqFn, HashFn> self_type;

  detail::hs_vector *vec;
  EqFn eqfn;
  HashFn hashfn;
  float loadf;
  std::atomic<intptr_t> grow_limit;
  lwlock lock;

  void _Set_loadf (float ldf)
    {
      if (ldf >= 0.4f && ldf <= 0.9f)
        this->loadf = ldf;
    }

  float load_factor (float ldf)
    {
      this->lock.acquire ();
      float ret = this->loadf;
      this->_Set_loadf (ldf);
      this->lock.release ();
      return (ret);
    }

  float load_factor () const
    {
      return (this->loadf);
    }

  void _Init (size_t size, float ldf, EqFn e, HashFn h)
    {
      this->_Set_loadf (ldf);
      size_t sz = size < 8 ? 8 : detail::upsize (size);

      this->vec = detail::hs_vector::alloc (sz, key_traits::FREE);
      this->eqfn = e;
      this->hashfn = h;
      this->grow_limit.store ((intptr_t)(sz * this->loadf),
                              std::memory_order_relaxed);
    }

  hash_set (size_t size = 0, float ldf = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (size, ldf, e, h);
    }

  hash_set (self_type&& right)
    {
      this->vec = right.vec;
      this->eqfn = right.eqfn;
      this->hashfn = right.hashfn;
      this->loadf = right.loadf;
      this->grow_limit.store ((intptr_t)(this->loadf * this->vec->entries),
                              std::memory_order_relaxed);
      right.vec = nullptr;
    }

  template <class Iter>
  hash_set (Iter first, Iter last, float ldf = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (0, ldf, e, h);
      for (; first != last; ++first)
        this->insert (*first);
    }

  hash_set (std::initializer_list<Key> lst, float ldf = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ()) :
      hash_set (lst.begin (), lst.end (), ldf, e, h)
    {
    }

  hash_set (const self_type& right) : hash_set (right.begin (), right.end ())
    {
    }

  size_t size () const
    {
      cs_guard g;
      return (this->vec->nelems.load (std::memory_order_relaxed));
    }

  size_t max_size () const
    {
      return (detail::upsize ((~(size_t)0) >> 2));
    }

  bool empty () const
    {
      return (this->size () == 0);
    }

  template <class K>
  size_t _Probe (const K& key, const detail::hs_vector *vp,
      bool put_p, bool& found) const
    {
      size_t idx = this->hashfn (key) & (vp->entries - 1);
      size_t probe = 1;

      for (found = false ; ; ++probe)
        {
          uintptr_t k = vp->data[idx];
          if (k == key_traits::FREE)
            return (put_p ? (found = true, idx) : (size_t)-1);
          else if (k != key_traits::DELT &&
              this->eqfn (key_traits::get (k), key))
            return (idx);

          idx = (idx + probe) & (vp->entries - 1);
        }
    }

  template <class K>
  size_t _Probe (const K& key, const detail::hs_vector *vp, bool put_p) const
    {
      bool unused;
      return (this->_Probe (key, vp, put_p, unused));
    }

  template <class K>
  uintptr_t _Find (const K& key) const
    {
      auto vp = this->vec;
      size_t idx = this->_Probe (key, vp, false);
      return (idx == (size_t)-1 ? key_traits::DELT :
        vp->data[idx] & ~key_traits::XBIT);
    }

  template <class K>
  optional<Key> find (const K& key) const
    {
      cs_guard g;
      uintptr_t ret = this->_Find (key);
      return (ret == key_traits::DELT ? optional<Key> () :
        optional<Key> (key_traits::get (ret)));
    }

  template <class K>
  bool contains (const K& key) const
    {
      cs_guard g;
      return (this->_Find (key) != key_traits::DELT);
    }

  bool _Decr_limit ()
    {
      return (this->grow_limit.fetch_sub (1, std::memory_order_acq_rel) > 0);
    }

  void _Rehash_set (uintptr_t key, detail::hs_vector *vp)
    {
      size_t idx = this->hashfn (key_traits::get (key)) & (vp->entries - 1);
      for (size_t probe = 1 ; ; ++probe)
        {
          if (vp->data[idx] != key_traits::FREE)
            {
              idx = (idx + probe) & (vp->entries - 1);
              continue;
            }

          vp->data[idx] = key;
          break;
        }
    }

  void _Rehash ()
    {
      detail::hs_sentry s (&this->lock, key_traits::XBIT);
      if (this->grow_limit.load (std::memory_order_relaxed) > 0)
        return;
      
      auto old = this->vec;
      size_t sz = this->vec->entries << 1;
      auto np = detail::hs_vector::alloc (sz, key_traits::FREE);
      size_t nelem = 0;

      s.vector = old;
      for (size_t i = 0; i < old->entries; ++i)
        {
          uintptr_t k = old->data[i];
          if (k != key_traits::FREE && k != key_traits::DELT)
            {
              this->_Rehash_set (k, np);
              ++nelem;
            }
        }

      s.vector = nullptr;
      np->nelems.store (nelem, std::memory_order_relaxed);
      this->grow_limit.store ((intptr_t)(np->entries * this->loadf) -
                              nelem, std::memory_order_relaxed);
      this->vec = np;
      finalize (old);
    }

  bool insert (const Key& key)
    {
      cs_guard g;
      while (true)
        {
          auto vp = this->vec;
          uintptr_t *ep = vp->data;
          bool found;
          size_t idx = this->_Probe (key, vp, true, found);

          if (!found)
            return (false);
          else if (this->_Decr_limit ())
            {
              uintptr_t k = key_traits::make (key);
              if (xatomic_cas_bool (ep + idx, key_traits::FREE, k))
                {
                  vp->nelems.fetch_add (1, std::memory_order_acq_rel);
                  return (found);
                }

              key_traits::free (k);
              continue;
            }

          this->_Rehash ();
        }
    }

  template <class K>
  bool _Erase (const K& k, optional<Key> *outp = nullptr)
    {
      cs_guard g;
      while (true)
        {
          auto vp = this->vec;
          uintptr_t *ep = vp->data;
          size_t idx = this->_Probe (k, vp, false);

          if (idx == (size_t)-1)
            return (false);

          uintptr_t old = ep[idx];
          if (!(old & key_traits::XBIT))
            {
              if (old == key_traits::DELT || old == key_traits::FREE)
                return (false);
              else if (!xatomic_cas_bool (ep + idx, old, key_traits::DELT))
                continue;

              vp->nelems.fetch_sub (1, std::memory_order_acq_rel);
              key_traits::destroy (old);

              if (outp)
                *outp = key_traits::get (old);

              return (true);
            }

          this->_Rehash ();
        }
    }

  template <class K>
  bool erase (const K& key)
    {
      return (this->_Erase (key));
    }

  template <class K>
  optional<Key> remove (const K& key)
    {
      optional<Key> ret;
      this->_Erase (key, &ret);
      return (ret);
    }

  struct iterator : public detail::hs_iter<key_traits>
    {
      typedef detail::hs_iter<key_traits> base_type;

      iterator () : base_type ()
        {
        }

      iterator (const self_type& self) : base_type ()
        {
          this->_Init (self.vec);
        }

      iterator (const iterator& right) : base_type (right)
        {
        }

      iterator (iterator&& right) : base_type (std::move (right))
        {
        }

      Key operator* () const
        {
          return (key_traits::get (this->c_key));
        }

      iterator& operator++ ()
        {
          this->_Adv ();
          return (*this);
        }

      iterator operator++ (int)
        {
          iterator ret = *this;
          this->_Adv ();
          return (ret);
        }
    };

  typedef iterator const_iterator;

  iterator begin () const
    {
      return (iterator (*this));
    }

  const_iterator cbegin () const
    {
      return (const_iterator (*this));
    }

  iterator end () const
    {
      return (iterator ());
    }

  const_iterator cend () const
    {
      return (const_iterator ());
    }

  void _Assign_vector (detail::hs_vector *vp, intptr_t gt)
    {
      this->lock.acquire ();
      auto prev = this->vec;

      for (size_t i = 0; i < prev->entries; ++i)
        {
          uintptr_t k = xatomic_or (&prev->data[i], key_traits::XBIT);
          if (k != key_traits::FREE && k != key_traits::DELT)
            key_traits::destroy (k);
        }

      this->grow_limit.store (gt, std::memory_order_release);
      this->vec = vp;
      this->lock.release ();
      finalize (prev);
    }

  void clear ()
    {
      this->lock.acquire ();
      this->grow_limit.store (0, std::memory_order_release);

      for (size_t i = 0; i < this->vec->entries; ++i)
        {
          uintptr_t k = xatomic_swap (&this->vec->data[i],
                                      key_traits::FREE) & ~key_traits::XBIT;

          if (k != key_traits::FREE && k != key_traits::DELT)
            key_traits::destroy (k);
        }

      this->grow_limit.store ((intptr_t)(this->loadf * this->vec->entries),
                              std::memory_order_relaxed);
      this->vec->nelems.store (0, std::memory_order_release);
      this->lock.release ();
    }

  template <class Iter>
  void assign (Iter first, Iter last)
    {
      self_type tmp (first, last, 0, this->loadf);
      this->_Assign_vector (tmp.vec,
        tmp.grow_limit.load (std::memory_order_relaxed));
      tmp.vec = nullptr;
    }

  void assign (std::initializer_list<Key> lst)
    {
      this->assign (lst.begin (), lst.end ());
    }

  self_type& operator= (const self_type& right)
    {
      this->assign (right.begin (), right.end ());
      return (*this);
    }

  self_type& operator= (self_type&& right)
    {
      this->_Assign_vector (right.vec,
        right.grow_limit.load (std::memory_order_relaxed));
      this->loadf = right.loadf;
      right.vec = nullptr;
      return (*this);
    }

  void swap (self_type& right)
    {
      if (this == &right)
        return;

      detail::hs_sentry s1 (&this->lock, key_traits::XBIT);
      detail::hs_sentry s2 (&right.lock, key_traits::XBIT);

      this->grow_limit.store (0, std::memory_order_release);
      right.grow_limit.store (0, std::memory_order_release);

      std::swap (this->vec, right.vec);
      std::swap (this->eqfn, right.eqfn);
      std::swap (this->hashfn, right.hashfn);
      std::swap (this->loadf, right.loadf);

      this->grow_limit.store ((intptr_t)(this->loadf * this->vec->entries) -
                              this->size (), std::memory_order_release);
      right.grow_limit.store ((intptr_t)(right.loadf * right.vec->entries) -
                              right.size (), std::memory_order_release);
    }

  ~hash_set ()
    {
      if (!this->vec)
        return;

      for (size_t i = 0; i < this->vec->entries; ++i)
        {
          uintptr_t k = this->vec->data[i] & ~key_traits::XBIT;
          if (k != key_traits::FREE && k != key_traits::DELT)
            key_traits::free (k);
        }

      this->vec->safe_destroy ();
      this->vec = nullptr;
    }
};

} // namespace xrcu

#endif
