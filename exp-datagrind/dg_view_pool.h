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

/* A memory allocator which does not allow freeing individual elements.
 * This makes the storage highly compact.
 *
 * The underlying storage is allocated from new, in large chunks, and connected
 * in a linked list. Requests that don't fit in the current chunk cause a new
 * chunk to be created. If the request is over a certain size, this new chunk
 * is sized just to the allocation and the old chunk continues to be used.
 * Otherwise, the new chunk is a standard size and becomes the new latest
 * chunk.
 */

#include <list>
#include <cstddef>

template<typename T>
class pool_allocator
{
private:
    struct chunk
    {
        T *storage;
        std::size_t capacity;
        std::size_t size;
    };

    std::size_t total_capacity;
    std::size_t total_size;
    std::list<chunk> chunks; /* Activity is at the beginning */

    static const std::size_t CHUNK_SIZE = 4096;
    static const std::size_t SPECIAL_SIZE = 128;

    /* Prevent copying */
    pool_allocator(const pool_allocator<T> &);
    pool_allocator<T> &operator =(const pool_allocator<T> &);
public:
    pool_allocator();
    ~pool_allocator();

    T *alloc(size_t n);
};

template<typename T>
pool_allocator<T>::pool_allocator() : total_capacity(0), total_size(0)
{
}

template<typename T>
pool_allocator<T>::~pool_allocator()
{
    typename std::list<chunk>::const_iterator i;
    for (i = chunks.begin();
         i != chunks.end();
         ++i)
        delete i->storage;
}

template<typename T>
T *pool_allocator<T>::alloc(size_t n)
{
    T *ans = NULL;
    if (n > 0)
    {
        std::size_t spare = 0;
        typename std::list<chunk>::iterator c = chunks.begin();
        if (c != chunks.end())
            spare = c->capacity - c->size;
        if (n > spare)
        {
            if (n >= SPECIAL_SIZE)
            {
                chunk next;
                next.storage = ans = new T[n];
                next.capacity = next.size = n;
                total_capacity += n;
                chunks.push_back(next);
            }
            else
            {
                chunk next;
                next.storage = ans = new T[CHUNK_SIZE];
                next.capacity = CHUNK_SIZE;
                next.size = n;
                total_capacity += CHUNK_SIZE;
                chunks.push_front(next);
            }
        }
        else
        {
            ans = c->storage + c->size;
            c->size += n;
        }
        total_size += n;
    }
    return ans;
}
