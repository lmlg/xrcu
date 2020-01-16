#ifndef __XRCU_HASHTABLE_HPP__
#define __XRCU_HASHTABLE_HPP__   1

#include "xrcu.hpp"
#include "xatomic.hpp"
#include "optional.hpp"
#include "lwlock.hpp"
#include "utils.hpp"
#include <atomic>
#include <functional>
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

inline constexpr size_t table_idx (size_t idx)
{
  return (idx * 2);
}

struct alignas (uintptr_t) ht_vector : public finalizable
{
  uintptr_t *data;
  size_t entries;
  size_t pidx;
  std::atomic<size_t> nelems { 0 };

  ht_vector (uintptr_t *ep) : data (ep) {}

  void safe_destroy ();

  size_t size () const
    {
      return (table_idx (this->entries));
    }
};

inline size_t
secondary_hash (size_t hval)
{
  static const size_t keys[] = { 2, 3, 5, 7 };
  return (keys[hval % (sizeof (keys) / sizeof (keys[0]))]);
}

extern size_t find_hsize (size_t size, float ldf, size_t& pidx);

extern ht_vector* make_htvec (size_t pidx, uintptr_t key, uintptr_t val);

struct ht_sentry
{
  lwlock *lock;
  uintptr_t xbit;
  ht_vector *vector = nullptr;

  ht_sentry (lwlock *lp, uintptr_t xb) : lock (lp), xbit (~xb)
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
  size_t idx = 0;
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

struct ht_inserter
{
  uintptr_t value;
  ht_inserter (uintptr_t v) : value (v) {}

  uintptr_t call0 () const noexcept
    {
      return (this->value);
    }

  uintptr_t call1 (uintptr_t) const noexcept
    {
      return (this->value);
    }

  void free (uintptr_t) {}
};

} // namespace detail

template <class KeyT, class ValT,
  class EqFn = std::equal_to<KeyT>,
  class HashFn = std::hash<KeyT> >
struct hash_table
{
  typedef detail::wrapped_traits<(sizeof (KeyT) < sizeof (uintptr_t) &&
      std::is_integral<KeyT>::value) || (std::is_pointer<KeyT>::value &&
      alignof (KeyT) >= 8), KeyT> key_traits;

  typedef detail::wrapped_traits<(sizeof (ValT) < sizeof (uintptr_t) &&
      std::is_integral<ValT>::value) || (std::is_pointer<ValT>::value &&
      alignof (ValT) >= 8), ValT> val_traits;

  typedef hash_table<KeyT, ValT, EqFn, HashFn> self_type;

  detail::ht_vector *vec;
  EqFn eqfn;
  HashFn hashfn;
  float loadf = 0.85f;
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
      size_t pidx, gt = detail::find_hsize (size, this->loadf, pidx);

      this->vec = detail::make_htvec (pidx,
        key_traits::FREE, val_traits::FREE);
      this->eqfn = e;
      this->hashfn = h;
      this->grow_limit.store (gt, std::memory_order_relaxed);
      this->vec->nelems.store (0, std::memory_order_relaxed);
    }

  hash_table (size_t size = 0, float ldf = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (size, ldf, e, h);
    }

  template <class Iter>
  hash_table (Iter first, Iter last, float ldf = 0.85f,
      EqFn e = EqFn (), HashFn h = HashFn ())
    {
      this->_Init (0, ldf, e, h);
      for (; first != last; ++first)
        this->insert ((*first).first, (*first).second);
    }

  hash_table (std::initializer_list<std::pair<KeyT, ValT> > lst,
      float ldf = 0.85f, EqFn e = EqFn (), HashFn h = HashFn ()) :
      hash_table (lst.begin (), lst.end (), ldf, e, h)
    {
    }

  hash_table (const self_type& right) :
      hash_table (right.begin (), right.end ())
    {
    }

  hash_table (self_type&& right)
    {
      this->vec = right.vec;
      this->eqfn = right.eqfn;
      this->hashfn = right.hashfn;
      this->loadf = right.loadf;
      this->grow_limit.store ((intptr_t)(this->loadf * this->size ()),
                              std::memory_order_relaxed);

      right.vec = nullptr;
    }

  size_t size () const
    {
      cs_guard g;
      return (this->vec->nelems.load (std::memory_order_relaxed));
    }

  size_t max_size () const
    {
      size_t out;
      return (detail::find_hsize (~(size_t)0, 0.85f, out));
    }

  bool empty () const
    {
      return (this->size () == 0);
    }

  size_t _Probe (const KeyT& key, const detail::ht_vector *vp,
      bool put_p, bool& found) const
    {
      size_t code = this->hashfn (key);
      size_t entries = vp->entries;
      size_t idx = code % entries;
      size_t vidx = detail::table_idx (idx);

      found = false;
      uintptr_t k = vp->data[vidx];

      if (k == key_traits::FREE)
        return (put_p ? (found = true, vidx) : (size_t)-1);
      else if (k != key_traits::DELT &&
          this->eqfn (key_traits::get (k), key))
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
            return (put_p ? (found = true, vidx) : (size_t)-1);
          else if (k != key_traits::DELT &&
              this->eqfn (key_traits::get (k), key))
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
      size_t code = this->hashfn (key_traits::get (key));
      size_t entries = vp->entries;
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
          auto old = this->vec;
          auto np = detail::make_htvec (old->pidx + 1,
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

          np->nelems.store (nelem, std::memory_order_relaxed);
          this->grow_limit.store ((intptr_t)(np->entries * this->loadf) -
                                  nelem, std::memory_order_relaxed);
          std::atomic_thread_fence (std::memory_order_release);

          /* At this point, another thread may decrement the growth limit
           * from the wrong vector. That's fine, it just means we'll move
           * the table sooner than necessary. */
          this->vec = np;
          finalize (old);
        }
    }

  uintptr_t _Find (const KeyT& key) const
    {
      auto vp = this->vec;
      size_t idx = this->_Probe (key, vp, false);
      return (idx == (size_t)-1 ? val_traits::DELT :
        vp->data[idx + 1] & ~val_traits::XBIT);
    }

  optional<ValT> find (const KeyT& key) const
    {
      cs_guard g;
      uintptr_t val = this->_Find (key);
      return (val == val_traits::DELT ? optional<ValT> () :
        optional<ValT> (val_traits::get (val)));
    }

  ValT find (const KeyT& key, const ValT& dfl) const
    {
      cs_guard g;
      uintptr_t val = this->_Find (key);
      return (val == val_traits::DELT ? dfl : val_traits::get (val));
    }

  bool contains (const KeyT& key) const
    {
      cs_guard g;
      return (this->_Find (key) != val_traits::DELT);
    }

  bool _Decr_limit ()
    {
      if (this->grow_limit.load (std::memory_order_relaxed) <= 0)
        return (false);

      this->grow_limit.fetch_sub (1, std::memory_order_acq_rel);
      return (true);
    }

  template <class Fn, class ...Args>
  bool _Upsert (uintptr_t k, const KeyT& key, Fn f, Args... args)
    {
      cs_guard g;

      while (true)
        {
          auto vp = this->vec;
          uintptr_t *ep = vp->data;
          bool found;
          size_t idx = this->_Probe (key, vp, true, found);

          if (!found)
            {
              uintptr_t tmp = ep[idx + 1];
              if (tmp != val_traits::DELT && tmp != val_traits::FREE &&
                  (tmp & val_traits::XBIT) == 0)
                {
                  uintptr_t v = f.call1 (tmp, args...);
                  if (v == tmp ||
                      xatomic_cas_bool (ep + idx + 1, tmp, v))
                    {
                      key_traits::free (k);
                      if (v != tmp)
                        val_traits::destroy (tmp);
                      return (found);
                    }

                  f.free (v);
                  continue;
                }
            }
          else if (this->_Decr_limit ())
            {
              /* NOTE: If we fail here, then the growth threshold will end up
               * too small. This simply means that we may have to rehash sooner
               * than absolutely necessary, which is harmless. On the other
               * hand, we must NOT try to reincrement the limit back, because
               * it risks ending up too big, which can be harmful if, for
               * example, a rehash is triggered before the increment. */

              uintptr_t v = f.call0 (args...);
#ifdef XRCU_HAVE_XATOMIC_DCAS
              if (xatomic_dcas_bool (&ep[idx], key_traits::FREE,
                                     val_traits::FREE, k, v))
#else
              if (xatomic_cas_bool (ep + idx + 0, key_traits::FREE, k) &&
                  xatomic_cas_bool (ep + idx + 1, val_traits::FREE, v))
#endif
                {
                  this->vec->nelems.fetch_add (1, std::memory_order_acq_rel);
                  return (found);
                }

              f.free (v);
              continue;
            }

          // The table was being rehashed - retry.
          this->_Rehash ();
        }
    }

  bool insert (const KeyT& key, const ValT& val)
    {
      uintptr_t k = key_traits::make (key), v = val_traits::XBIT;

      try
        {
          v = val_traits::make (val);
          return (this->_Upsert (k, key, detail::ht_inserter (v)));
        }
      catch (...)
        {
          key_traits::free (k);
          if (v != val_traits::XBIT)
            val_traits::free (v);

          throw;
        }
    }

  template <class Fn, class Vtraits>
  struct _Updater
    {
      Fn fct;

      _Updater (Fn f) : fct (f) {}

      template <class ...Args>
      uintptr_t call0 (Args ...args)
        { // Call function with default-constructed value and arguments.
          auto tmp = (typename Vtraits::value_type ());
          auto&& rv = this->fct (tmp, args...);
          return (Vtraits::make (rv));
        }

      template <class ...Args>
      uintptr_t call1 (uintptr_t x, Args ...args)
        { // Call function with stored value and arguments.
          auto&& tmp = Vtraits::get (x);
          auto&& rv = this->fct (tmp, args...);
          return (Vtraits::XBIT == 1 &&
            &rv == &tmp ? x : Vtraits::make (rv));
        }

      void free (uintptr_t x)
        {
          Vtraits::free (x);
        }
    };

  template <class Fn, class ...Args>
  bool update (const KeyT& key, Fn f, Args... args)
    {
      uintptr_t k = key_traits::make (key);

      try
        {
          return (this->_Upsert (k, key,
                                 _Updater<Fn, val_traits> (f), args...));
        }
      catch (...)
        {
          key_traits::free (k);
          throw;
        }
    }

  bool _Erase (const KeyT& key, optional<ValT> *outp = nullptr)
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

              this->vec->nelems.fetch_sub (1, std::memory_order_acq_rel);
              // Safe to set the key without atomic ops.
              ep[idx] = key_traits::DELT;
              key_traits::destroy (oldk);
              val_traits::destroy (oldv);

              if (outp)
                *outp = val_traits::get (oldv);

              return (true);
            }

          // The table was being rehashed - retry.
          this->_Rehash ();
        }
    }

  bool erase (const KeyT& key)
    {
      return (this->_Erase (key));
    }

  optional<ValT> remove (const KeyT& key)
    {
      optional<ValT> ret;
      this->_Erase (key, &ret);
      return (ret);
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
          return (key_traits::get (this->c_key));
        }

      ValT value () const
        {
          return (val_traits::get (this->c_val));
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

  void _Assign_vector (detail::ht_vector *nv, intptr_t gt)
    {
      // First step: Lock the table.
      this->lock.acquire ();
      auto prev = this->vec;

      // Second step: Finalize every valid key/value pair.
      for (size_t i = detail::table_idx (0) + 1; i < prev->size (); i += 2)
        {
          uintptr_t v = xatomic_or (&prev->data[i], val_traits::XBIT);
          if (v != val_traits::FREE && v != val_traits::DELT)
            {
              key_traits::destroy (prev->data[i - 1]);
              val_traits::destroy (v);
            }
        }

      // Third step: Set up the new vector and parameters.
      this->grow_limit.store (gt, std::memory_order_relaxed);
      std::atomic_thread_fence (std::memory_order_release);

      this->vec = nv;
      this->lock.release ();
      finalize (prev);
    }

  void clear ()
    {
      size_t pidx, gt = detail::find_hsize (0, this->loadf, pidx);
      auto nv = detail::make_htvec (pidx, key_traits::FREE, val_traits::FREE);
      this->_Assign_vector (nv, gt);
    }

  template <class Iter>
  void assign (Iter first, Iter last)
    {
      self_type tmp (first, last, 0, this->loadf);
      this->_Assign_vector (tmp.vec,
        tmp.grow_limit.load (std::memory_order_relaxed));
      tmp.vec = nullptr;
    }

  void assign (std::initializer_list<std::pair<KeyT, ValT> > lst)
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

      detail::ht_sentry s1 (&this->lock, ~val_traits::XBIT);
      detail::ht_sentry s2 (&right.lock, ~val_traits::XBIT);

      // Prevent further insertions (still allows deletions).
      this->grow_limit.store (0, std::memory_order_release);
      right.grow_limit.store (0, std::memory_order_release);

      std::swap (this->vec, right.vec);
      std::swap (this->eqfn, right.eqfn);
      std::swap (this->hashfn, right.hashfn);
      std::swap (this->loadf, right.loadf);

      this->grow_limit.store ((intptr_t)(this->loadf *
        this->vec->entries) - this->size (), std::memory_order_release);
      right.grow_limit.store ((intptr_t)(right.loadf *
        right.vec->entries) - right.size (), std::memory_order_release);
    }

  ~hash_table ()
    {
      if (!this->vec)
        return;

      for (size_t i = detail::table_idx (0); i < this->vec->size (); i += 2)
        {
          uintptr_t k = this->vec->data[i] & ~key_traits::XBIT;
          if (k == key_traits::FREE || k == key_traits::DELT)
            continue;

          key_traits::free (k);
          val_traits::free (this->vec->data[i + 1] & ~val_traits::XBIT);
        }

      this->vec->safe_destroy ();
      this->vec = nullptr;
    }
};

} // namespace xrcu

namespace std
{

template <class KeyT, class ValT, class EqFn, class HashFn>
void swap (xrcu::hash_table<KeyT, ValT, EqFn, HashFn>& left,
           xrcu::hash_table<KeyT, ValT, EqFn, HashFn>& right)
{
  left.swap (right);
}

} // namespace std

#endif
