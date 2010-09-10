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

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdint.h>
#include <stdexcept>

#include "dg_view.h"
#include "dg_view_parse.h"

using namespace std;

record_parser *record_parser::create(FILE *f)
{
    uint8_t size_small;
    uint64_t size;
    uint8_t type;

    if (fread(&type, sizeof(type), 1, f) != 1)
    {
        if (feof(f))
            return NULL;
        throw record_parser_ferror();
    }

    if (type >= 0x80)
    {
        size = 1;
    }
    else
    {
        if (fread(&size_small, sizeof(size_small), 1, f) != 1)
            throw record_parser_feof();
        if (size_small < 255)
            size = size_small;
        else
        {
            if (fread(&size, sizeof(size), 1, f) != 1)
                throw record_parser_ferror();
        }
    }

    return new record_parser(type, size, f);
}

record_parser::record_parser(uint8_t type, uint64_t size, std::FILE *file)
    : type(type), size(size), offset(0), file(file)
{
}

uint8_t record_parser::get_type() const
{
    return type;
}

uint8_t record_parser::extract_byte()
{
    uint8_t v;
    extract_bytes(&v, sizeof(v));
    return v;
}

HWord record_parser::extract_word()
{
    HWord v;
    extract_bytes((uint8_t *) &v, sizeof(v));
    return v;
}

void record_parser::extract_bytes(uint8_t *buffer, size_t len)
{
    size_t r;
    if (len > size - offset)
        throw record_parser_error("Record is too short");

    r = fread(buffer, 1, len, file);
    offset += r;
    if (r != len)
    {
        if (feof(file))
            throw record_parser_feof();
        else
            throw record_parser_ferror();
    }
}

std::string record_parser::extract_string()
{
    char next;
    string ans;

    while (offset < size)
    {
        next = (char) extract_byte();
        if (next == '\0')
            return ans;
        ans += next;
    }
    /* If we got here, we have offset == size but no terminator */
    throw record_parser_string_error();
}

void record_parser::finish()
{
    if (offset != size)
    {
        ostringstream msg;
        msg << "Record is too large (expected " << offset << " but got " << size << ")";
        discard();
        throw record_parser_length_error(msg.str());
    }
}

void record_parser::discard()
{
    uint8_t dummy[64];

    while (offset < size)
    {
        size_t todo = size - offset;
        if (todo > sizeof(dummy))
            todo = sizeof(dummy);
        extract_bytes(dummy, todo);
    }
}
