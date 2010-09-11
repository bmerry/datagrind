/*
   This file is part of Datagrind, a tool for tracking data accesses.

   Copyright (C) 2010 Bruce Merry
      bmerry@users.sourceforge.net

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

/* Stores a collection of ranges, supporting the following operations
 * efficiently:
 * - Add a range
 * - Remove a range by (start, end) address pair
 * - Remove a range by start address
 * - Find a range containing an address
 * - Iterate over all ranges
 *
 * Overlapping ranges complicates this. To keep it simple, we don't currently
 * support it.
 */

#ifndef _DG_VIEW_RANGE_H
#define _DG_VIEW_RANGE_H

#include <utility>
#include <map>
#include <stdexcept>

template<typename Address, typename Data>
class rangemap
{
public:
    typedef typename std::pair<Address, Address> key_type;
    typedef Data data_type;
private:
    typedef typename std::multimap<key_type, Data> storage_type;

    storage_type ranges;
public:
    typedef typename storage_type::size_type size_type;
    typedef typename storage_type::iterator iterator;
    typedef typename storage_type::const_iterator const_iterator;
    typedef typename storage_type::value_type value_type;

    iterator begin() { return ranges.begin(); }
    const_iterator begin() const { return ranges.begin(); }
    iterator end() { return ranges.end(); }
    const_iterator end() const { return ranges.end(); }

    /* Finds a range containing a. Ranges are [first, last). */
    const_iterator find(const Address &addr) const;
    iterator find(const Address &addr);
    const_iterator find(const key_type &key) const { return ranges.find(key); }
    iterator find(const key_type &key) { return ranges.find(key); }

    iterator insert(const value_type &value);
    iterator insert(const Address &start, const Address &end, const data_type &data);

    void erase(iterator it) { ranges.erase(it); }
    size_type erase(const Address &addr);
    size_type erase(const key_type &key) { return ranges.erase(key); }

    rangemap() {}
    virtual ~rangemap() {}
};

template<typename Address, typename Data>
typename rangemap<Address, Data>::iterator rangemap<Address, Data>::find(const Address &addr)
{
    key_type srch(addr, addr);

    iterator next = ranges.upper_bound(srch);
    if (next != ranges.end())
    {
        if (next->first.first == addr)
            return next;
    }
    if (next != ranges.begin())
    {
        --next;
        if (next->first.first <= addr && next->first.second > addr)
            return next;
    }
    return ranges.end();
}

template<typename Address, typename Data>
typename rangemap<Address, Data>::const_iterator rangemap<Address, Data>::find(const Address &addr) const
{
    return const_cast<rangemap<Address, Data> *>(this)->find(addr);
}

template<typename Address, typename Data>
typename rangemap<Address, Data>::size_type rangemap<Address, Data>::erase(const Address &addr)
{
    size_type ans = 0;
    key_type srch(addr, addr);

    iterator low = ranges.lower_bound(srch);
    iterator high = low;
    while (high != ranges.end() && high->first.first == addr)
    {
        high++;
        ans++;
    }
    ranges.erase(low, high);
    return ans;
}

template<typename Address, typename Data>
typename rangemap<Address, Data>::iterator rangemap<Address, Data>::insert(const value_type &value)
{
    if (value.first.first > value.first.second)
    {
        throw std::invalid_argument("Range cannot have negative length");
    }

    /* Check for overlaps - not supported yet */
    const_iterator high = ranges.lower_bound(value.first);
    if (high != ranges.end() && high->first.first < value.first.second)
        throw std::invalid_argument("Overlapping ranges");

    if (high != ranges.begin())
    {
        const_iterator low = high;
        --low;
        if (low->first.second > value.first.first)
        {
            printf("(%#lx,%#lx) overlaps (%#lx,%#lx)\n",
                   low->first.first, low->first.second,
                   value.first.first, value.first.second);
            throw std::invalid_argument("Overlapping ranges");
        }
    }

    return ranges.insert(value);
}

template<typename Address, typename Data>
typename rangemap<Address, Data>::iterator rangemap<Address, Data>::insert(const Address &start, const Address &end, const data_type &data)
{
    return insert(value_type(key_type(start, end), data));
}

#endif /* !_DG_VIEW_RANGE_H */
