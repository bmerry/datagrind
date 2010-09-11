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

#ifndef __DG_VIEW_PARSE_H
#define __DG_VIEW_PARSE_H

#include <stdint.h>
#include <cstdio>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "dg_view.h"

class record_parser_error : public std::runtime_error
{
public:
    record_parser_error(const std::string &msg) throw() : std::runtime_error(msg) {}
};

class record_parser_content_error : public record_parser_error
{
public:
    record_parser_content_error(const std::string &msg) : record_parser_error(msg) {}
};

class record_parser_length_error : public record_parser_content_error
{
public:
    record_parser_length_error(const std::string &msg) throw() : record_parser_content_error(msg) {}
};

class record_parser_string_error : public record_parser_content_error
{
public:
    record_parser_string_error() throw() : record_parser_content_error("") {}
    virtual const char *what() const throw() { return "Error: string was not terminated"; }
};

class record_parser_ferror : public record_parser_error
{
public:
    record_parser_ferror() throw() : record_parser_error("") {}
    virtual const char *what() const throw() { return "Error reading file"; }
};

class record_parser_feof : public record_parser_error
{
public:
    record_parser_feof() throw() : record_parser_error("") {}
    virtual const char *what() const throw() { return "Unexpected end of file"; }
};

class record_parser
{
private:
    uint8_t type;
    uint64_t size;
    uint64_t offset;
    std::FILE *file;

    /* Prevent copying */
    record_parser(const record_parser &);
    record_parser &operator=(const record_parser &);

    explicit record_parser(uint8_t type, uint64_t size, std::FILE *file);
public:
    /* Use a factory to that NULL can be used to indicate EOF */
    static record_parser *create(std::FILE *f);

    uint8_t get_type() const;

    uint8_t extract_byte();
    HWord extract_word();
    void extract_bytes(uint8_t *buffer, std::size_t len);
    std::string extract_string();

    /* Number of bytes remaining */
    uint64_t remain() const;

    /* Checks that the whole block has been consumed. */
    void finish();

    /* Discard the rest of the record. */
    void discard();
};

#endif /* __DG_VIEW_PARSE_H */
