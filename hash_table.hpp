#ifndef __XRCU_HASHTABLE_HPP__
#define __XRCU_HASHTABLE_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include <atomic>
#include <functional>
#include <cstdint>
#include <type_traits>
#include <initializer_list>

namespace std
{
  struct forward_iterator_tag;
}

namespace xrcu
{

namespace detail
{

template <class T>
struct ht_wrapper : public finalizable
{
  T value;

  ht_wrapper (const T& val) : value (val) {}

  void safe_destroy ()
    {
      delete this;
    }
};

template <bool Integral, class T>
struct ht_traits
{
  static const uintptr_t XBIT = (uintptr_t)1 << (sizeof (uintptr_t) * 8 - 1);
  static const uintptr_t FREE = (~(uintptr_t)0) & ~XBIT;
  static const uintptr_t DELT = FREE >> 1;

  uintptr_t make (T val) const
    {
      return ((uintptr_t)(typename std::make_unsigned<T>::type)val);
    }

  T get (uintptr_t w) const
    {
      return ((T)(w & ~XBIT));
    }

  void destroy (uintptr_t) const {}
  void free (uintptr_t) const {}
};

template <class T>
struct ht_traits<false, T>
{
  static const uintptr_t XBIT = 1;
  static const uintptr_t FREE = 2;
  static const uintptr_t DELT = 4;

  uintptr_t make (const T& val) const
    {
      return ((uintptr_t)(new ht_wrapper<T> (val)));
    }

  const T& get (uintptr_t addr) const
    {
      return (((const ht_wrapper<T> *)(addr & ~XBIT))->value);
    }

  void destroy (uintptr_t addr) const
    {
      finalize ((ht_wrapper<T> *)addr);
    }

  void free (uintptr_t addr) const
    {
      delete (ht_wrapper<T> *)addr;
    }
};

const size_t TABVEC_OVERHEAD = 2;

inline constexpr size_t table_idx (size_t idx)
{
  return (idx * 2 + TABVEC_OVERHEAD);
}

struct alignas (uintptr_t) ht_vector : public finalizable
{
  uintptr_t *data;

  ht_vector (uintptr_t *ep) : data (ep) {}

  static ht_vector* make (size_t n);
  void safe_destroy ();

  uintptr_t& entries ()
    {
      return (this->data[0]);
    }

  uintptr_t entries () const
    {
      return (this->data[0]);
    }

  uintptr_t& pidx ()
    {
      return (this->data[1]);
    }

  uintptr_t pidx () const
    {
      return (this->data[1]);
    }

  size_t size () const
    {
      return (table_idx (this->entries ()));
    }
};

extern size_t prime_by_idx (size_t idx);

inline size_t
secondary_hash (size_t hval)
{
  static const size_t keys[] = { 2, 3, 5, 7 };
  return (keys[hval % (sizeof (keys) / sizeof (keys[0]))]);
}

extern size_t find_hsize (size_t size, float mvr, size_t& pidx);

extern ht_vector* make_htvec (size_t pidx, uintptr_t key, uintptr_t val);

struct ht_lock
{
  std::atomic_int lock;

  ht_lock ()
    {
      this->lock.store (0, std::memory_order_relaxed);
    }

  void acquire ();
  void release ();
};

struct ht_sentry
{
  ht_lock *lock;
  uintptr_t xbit;
  ht_vector *vector = nullptr;

  ht_sentry (ht_lock *lp, uintptr_t xb) : lock (lp), xbit (~xb)
    {
      this->lock->acquire ();
    }

  ~ht_sentry ()
    {
      if (this->vector)
        for (size_t i = table_idx (0) + 1; i < this->vector->size (); i += 2)
          xatomic_and (&this->vector->data[i], this->xbit);

      this->lock->release ();
    }
};

template <class Ktraits, class Vtraits>
struct ht_iter : public cs_guard
{
  const ht_vector *vec = nullptr;
  size_t idx = TABVEC_OVERHEAD;
  uintptr_t c_key;
  uintptr_t c_val;
  bool valid = false;

  typedef std::forward_iterator_tag iterator_category;

  void _Init (const ht_vector *vp)
    {
      this->vec = vp;
      this->_Adv ();
    }

  void _Adv ()
    {
      this->valid = false;
      for (; this->idx < this->vec->size (); )
        {
          this->c_key = this->vec->data[idx + 0];
          this->c_val = this->vec->data[idx + 1] & ~Vtraits::XBIT;

          this->idx += 2;
          if ((this->c_key & ~Ktraits::XBIT) != Ktraits::FREE &&
              this->c_val != Vtraits::FREE && this->c_val != Vtraits::DELT)
            {
              this->valid = true;
              break;
            }
        }
    }

  bool operator== (const ht_iter<Ktraits, Vtraits>& right) const
    {
      return ((!this->valid && !right.valid) ||
        (this->vec == right.vec && this->idx == right.idx));
    }

  bool operator!= (const ht_iter<Ktraits, Vtraits>& right)
    {
      return (!(*this == right));
    }
};

} // namespace detail

template <class KeyT, class ValT,
  class EqFn = std::equal_to<KeyT>,
  class HashFn = std::hash<KeyT> >
struct hash_table
{
  typedef detail::ht_traits<(sizeof (KeyT) < sizeof (uintptr_t) &&
      std::is_integral<KeyT>::value) || (std::is_pointer<KeyT>::value &&
      alignof (KeyT) >= 8), KeyT> key_traits;

  typedef detail::ht_traits<(sizeof (ValT) < sizeof (uintptr_t) &&
      std::is_integral<ValT>::value) || (std::is_pointer<ValT>::value &&
      alignof (ValT) >= 8), ValT> val_traits;

  typedef hash_table<KeyT, ValT, EqFn, HashFn> self_type;

  detail::ht_vector *vec;
  EqFn eqfn;
  HashFn hashfn;
  float mv_ratio;
  std::atomic<intptr_t> grow_limit;
  detail::ht_lock lock;
  std::atomic<size_t> nelems;

  void _Init (size_t size, float mvr, EqFn e, HashFn h)
    {
      this->mv_ratio = mvr < 0.2f || mvr > 0.9f ? 0.85f : mvr;
      size_t pidx, gt = detail::find_hsize (0, this->mv_ratio, pidx);

      this->vec = detail::make_htvec (pidx,
        key_traits::FREE, val_traits::FREE);
      this->eqfn = e;
      this->hashfn = h;
      this->grow_limit.store (gt, std::memory_order_relaxed);
      this->nelems.store (0, std::memory_order_relaxed);
    }

  hash_table (size_t size = 0, float mvr = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (size, mvr, e, h);
    }

  template <class InIt>
  hash_table (InIt first, InIt last, size_t size = 0, float mvr = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (size, mvr, e, h);
      for (; first != last; ++first)
        this->insert ((*first).first, (*first).second);
    }

  hash_table (std::initializer_list<std::pair<KeyT, ValT> > lst,
      size_t size = 0, float mvr = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (size, mvr, e, h);
      for (const auto& pair : lst)
        this->insert (pair.first, pair.second);
    }

  hash_table (const self_type& right) :
      hash_table (right.begin (), right.end ())
    {
    }

  hash_table (self_type&& right) :
      hash_table (right.begin (), right.end ())
    {
    }

  size_t size () const
    {
      return (this->nelems.load (std::memory_order_relaxed));
    }

  size_t _Probe (const KeyT& key, const detail::ht_vector *vp,
      bool put_p, bool& empty) const
    {
      size_t code = this->hashfn (key);
      size_t entries = vp->entries ();
      size_t idx = code % entries;
      size_t vidx = detail::table_idx (idx);

      empty = false;
      uintptr_t k = vp->data[vidx];

      if (k == key_traits::FREE)
        return (put_p ? (empty = true, vidx) : (size_t)-1);
      else if (this->eqfn (key_traits().get (k), key))
        return (vidx);

      for (size_t initial = idx, sec = detail::secondary_hash (code) ; ; )
        {
          if ((idx += sec) >= entries)
            idx -= entries;

          if (idx == initial)
            return ((size_t)-1);

          vidx = detail::table_idx (idx);
          k = vp->data[vidx];

          if (k == key_traits::FREE)
            return (put_p ? (empty = true, vidx) : (size_t)-1);
          else if (this->eqfn (key_traits().get (k), key))
            return (vidx);
        }
    }

  size_t _Probe (const KeyT& key, const detail::ht_vector *vp, bool put_p) const
    {
      bool unused;
      return (this->_Probe (key, vp, put_p, unused));
    }

  size_t _Gprobe (uintptr_t key, detail::ht_vector *vp)
    {
      size_t code = this->hashfn (key_traits().get (key));
      size_t entries = vp->entries ();
      size_t idx = code % entries;
      size_t vidx = detail::table_idx (idx);

      if (vp->data[vidx] == key_traits::FREE)
        return (vidx);

      for (size_t sec = detail::secondary_hash (code) ; ; )
        {
          if ((idx += sec) >= entries)
            idx -= entries;

          vidx = detail::table_idx (idx);
          if (vp->data[vidx] == key_traits::FREE)
            return (vidx);
        }
    }

  void _Rehash ()
    {
      detail::ht_sentry s (&this->lock, val_traits::XBIT);

      if (this->grow_limit.load (std::memory_order_relaxed) <= 0)
        {
          detail::ht_vector *old = this->vec;
          detail::ht_vector *np = detail::make_htvec (old->pidx () + 1,
              key_traits::FREE, val_traits::FREE);
          size_t nelem = 0;

          s.vector = old;

          for (size_t i = detail::table_idx (0); i < old->size (); i += 2)
            {
              uintptr_t key = old->data[i];
              uintptr_t val = xatomic_or (&old->data[i + 1], val_traits::XBIT);

              if (key != key_traits::FREE && key != key_traits::DELT &&
                  val != val_traits::FREE && val != val_traits::DELT)
                {
                  size_t nidx = this->_Gprobe (key, np);
                  np->data[nidx + 0] = key;
                  np->data[nidx + 1] = val;
                  ++nelem;
                }
            }

          s.vector = nullptr;

          this->nelems.store (nelem, std::memory_order_relaxed);
          this->grow_limit.store ((intptr_t)(this->mv_ratio *
              np->entries ()) - nelem, std::memory_order_relaxed);
          std::atomic_thread_fence (std::memory_order_release);

          /* At this point, another thread may decrement the growth limit
           * from the wrong vector. That's fine, it just means we'll move
           * the table sooner than necessary. */
          this->vec = np;
          finalize (old);
        }
    }

  ValT find (const KeyT& key, const ValT& dfl = ValT ()) const
    {
      cs_guard g;

      while (true)
        {
          auto vp = this->vec;
          size_t idx = this->_Probe (key, vp, false);
          if (idx == (size_t)-1)
            return (dfl);
          else
            {
              uintptr_t val = vp->data[idx + 1] & ~val_traits::XBIT;
              return (val == val_traits::DELT ? dfl : val_traits().get (val));
            }
        }
    }

  bool insert (const KeyT& key, const ValT& val)
    {
      uintptr_t k = key_traits().make (key);
      uintptr_t v;

      try
        {
          v = val_traits().make (val);
        }
      catch (...)
        {
          val_traits().free (k);
          throw;
        }

      cs_guard g;

      while (true)
        {
          auto vp = this->vec;
          uintptr_t *ep = vp->data;
          bool empty;
          size_t idx = this->_Probe (key, vp, true, empty);

          if (empty)
            {
              if (this->grow_limit.load (std::memory_order_relaxed) > 0)
                {
                  this->grow_limit.fetch_sub (1, std::memory_order_acq_rel);

#ifdef XRCU_HAVE_XATOMIC_DCAS
                  if (xatomic_dcas_bool (&ep[idx], key_traits::FREE,
                      val_traits::FREE, k, v))
#else
                  if (xatomic_cas_bool (ep + idx + 0, key_traits::FREE, k) &&
                      xatomic_cas_bool (ep + idx + 1, val_traits::FREE, v))
#endif
                    {
                      this->nelems.fetch_add (1, std::memory_order_acq_rel);
                      return (empty);
                    }

                  continue;
                }
            }
          else
            {
              uintptr_t tmp = ep[idx + 1];
              if (tmp != val_traits::DELT && tmp != val_traits::FREE &&
                  (tmp & val_traits::XBIT) == 0 &&
                  xatomic_cas_bool (ep + idx + 1, tmp, v))
                {
                  key_traits().free (k);
                  val_traits().destroy (tmp);
                  return (empty);
                }

              continue;
            }

          this->_Rehash ();
        }
    }

  bool erase (const KeyT& key)
    {
      cs_guard g;

      while (true)
        {
          auto vp = this->vec;
          uintptr_t *ep = vp->data;
          size_t idx = this->_Probe (key, vp, false);

          if (idx == (size_t)-1)
            return (false);

          uintptr_t oldk = ep[idx], oldv = ep[idx + 1];

          if ((oldv & val_traits::XBIT) == 0)
            {
              if (oldk == key_traits::DELT || oldk == key_traits::FREE ||
                  oldv == val_traits::DELT)
                return (false);
              else if (!xatomic_cas_bool (ep + idx + 1,
                                          oldv, val_traits::DELT))
                continue;

              this->nelems.fetch_sub (1, std::memory_order_acq_rel);
              // Safe to set the key without atomic ops.
              ep[idx] = key_traits::DELT;
              key_traits().destroy (oldk);
              val_traits().destroy (oldv);
              return (true);
            }

          // The table was being rehashed - retry.
          this->_Rehash ();
        }
    }

  struct iterator : public detail::ht_iter<key_traits, val_traits>
    {
      iterator (const self_type& self)
        {
          this->_Init (self.vec);
        }

      iterator () {}

      KeyT key () const
        {
          return (key_traits().get (this->c_key));
        }

      ValT value () const
        {
          return (val_traits().get (this->c_val));
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

      std::pair<KeyT, ValT> operator* () const
        {
          return (std::pair<KeyT, ValT> (this->key (), this->value ()));
        }
    };

  typedef iterator const_iterator;

  iterator begin () const
    {
      return (iterator (*this));
    }

  iterator end () const
    {
      return (iterator ());
    }

  iterator cbegin () const
    {
      return (this->begin ());
    }

  iterator cend () const
    {
      return (this->end ());
    }

  ~hash_table ()
    {
      for (size_t i = detail::table_idx (0); i < this->vec->size (); i += 2)
        {
          uintptr_t k = this->vec->data[i] & ~key_traits::XBIT;
          if (k == key_traits::FREE || k == key_traits::DELT)
            continue;

          key_traits().free (k);
          val_traits().free (this->vec->data[i + 1] & ~val_traits::XBIT);
        }

      this->vec->safe_destroy ();
    }
};

} // namespace xrcu

#endif
