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

#include "dg_view.h"
#include "dg_view_debuginfo.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <bfd.h>

using namespace std;

struct object_file
{
    HWord text_avma;
    bfd *abfd[2];                /* Main file and .gnu_debuglink */
    vector<asymbol *> syms[2];

    object_file() : text_avma(0)
    {
        abfd[0] = abfd[1] = NULL;
    }
};

static map<string, object_file> object_files;

/* Loads either the main file or the .gnu_debuglink file */
static bool load_single_object_file(const char *filename, bfd *&abfd, vector<asymbol *> &syms)
{
    char **matching;
    long symcount = 0, storage;

    abfd = bfd_openr(filename, NULL);
    if (!abfd)
        return false;
    if (!bfd_check_format_matches(abfd, bfd_object, &matching))
        goto bad_close;

    storage = bfd_get_symtab_upper_bound(abfd);
    if (storage > 0)
    {
        syms.resize(storage);
        symcount = bfd_canonicalize_symtab(abfd, &syms[0]);
    }

    if (symcount == 0)
    {
        storage = bfd_get_dynamic_symtab_upper_bound(abfd);
        if (storage > 0)
        {
            syms.resize(storage);
            symcount = bfd_canonicalize_dynamic_symtab(abfd, &syms[0]);
        }
    }
    if (symcount <= 0)
        goto bad_close;

    return true;

bad_close:
    bfd_close(abfd);
    abfd = NULL;
    return false;
}

void load_object_file(const char *filename, HWord text_avma)
{
    vector<asymbol *> syms;
    bfd *abfd = NULL;

    if (load_single_object_file(filename, abfd, syms))
    {
        object_file &of = object_files[filename];
        if (of.abfd[0] != NULL)
            bfd_close(of.abfd[0]);
        of.text_avma = text_avma;
        of.abfd[0] = abfd;
        of.syms[0].swap(syms);

        char *gnu_debuglink = bfd_follow_gnu_debuglink(abfd, "/usr/lib/debug");
        if (gnu_debuglink != NULL)
        {
            if (load_single_object_file(gnu_debuglink, abfd, syms))
            {
                if (of.abfd[1] != NULL)
                    bfd_close(of.abfd[1]);
                of.syms[1].swap(syms);
                of.abfd[1] = abfd;
            }
            free(gnu_debuglink);
        }
    }
}

struct addr2line_info
{
    HWord addr;
    object_file *obj;
    int pass;
    bool found;
    const char *file_name;
    const char *function_name;
    unsigned int line;
};

static void addr2line_section(bfd *abfd, asection *sect, void *arg)
{
    addr2line_info *info = (addr2line_info *) arg;

    if (info->found)
        return;
    if ((bfd_get_section_flags(abfd, sect) & SEC_ALLOC) == 0)
        return;

    bfd_size_type size = bfd_get_section_size(sect);
    if (info->addr >= info->obj->text_avma + size)
        return;

    info->found = bfd_find_nearest_line(abfd, sect, &info->obj->syms[info->pass][0],
                                        info->addr - info->obj->text_avma,
                                        &info->file_name, &info->function_name,
                                        &info->line);
}

string addr2line(HWord addr)
{
    ostringstream label;
    label << hex << showbase << addr << dec << noshowbase;
    for (map<string, object_file>::iterator i = object_files.begin(); i != object_files.end(); ++i)
    {
        object_file &of = i->second;
        if (addr >= of.text_avma)
        {
            addr2line_info info;

            info.addr = addr;
            info.obj = &of;
            info.found = false;
            for (info.pass = 1; info.pass >= 0; info.pass--)
            {
                if (of.abfd[info.pass] == NULL) continue;
                bfd_map_over_sections(of.abfd[info.pass], addr2line_section, &info);
                if (info.found)
                {
                    if (info.function_name != NULL && info.function_name[0])
                        label << " in " << info.function_name;
                    label << " (";
                    if (info.file_name)
                    {
                        const char *suffix = strrchr(info.file_name, '/');
                        if (suffix == NULL)
                            suffix = info.file_name;
                        else
                            suffix++; /* Skip over the last / */
                        label << suffix << ":" << info.line;
                    }
                    else
                        label << i->first;
                    label << ")";
                    return label.str();
                }
            }
        }
    }
    return label.str();
}
