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
#include <memory>
#include <bfd.h>

using namespace std;

namespace
{

/* Corresponds to either the main file or the .gnu_debuglink file */
class object_subfile
{
public:
    char *filename;  /* bfd uses this storage rather than making its own */
    bfd *abfd;
    vector<asymbol *> syms;

private:
    /* Prevent copying */
    object_subfile(const object_subfile &);
    object_subfile &operator=(const object_subfile &);

public:
    object_subfile() : filename(NULL), abfd(NULL), syms() {}
    ~object_subfile()
    {
        delete[] filename;
        if (abfd != NULL)
            bfd_close(abfd);
    }

    bool load(const char *filename);
};

class object_file
{
public:
    HWord text_avma;
    object_subfile *subfiles[2];

    object_file() : text_avma(0)
    {
        subfiles[0] = subfiles[1] = NULL;
    }

    ~object_file()
    {
        delete subfiles[0];
        delete subfiles[1];
    }

private:
    /* Prevent copying */
    object_file(const object_file &);
    object_file &operator=(const object_file &);
};

/* TODO: this never gets cleaned up */
static map<string, object_file *> object_files;

/* Loads either the main file or the .gnu_debuglink file */
bool object_subfile::load(const char *filename)
{
    char **matching;
    long symcount = 0, storage;
    size_t filename_len = strlen(filename);

    delete[] this->filename;
    if (abfd != NULL)
        bfd_close(abfd);

    this->filename = new char[filename_len + 1];
    strcpy(this->filename, filename);
    abfd = bfd_openr(this->filename, NULL);
    if (!abfd)
        return false;
    if (!bfd_check_format_matches(abfd, bfd_object, &matching))
        return false;

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
        return false;

    return true;

    bfd_close(abfd);
    abfd = NULL;
    return false;
}

} /* namespace */

void load_object_file(const char *filename, HWord text_avma)
{
    auto_ptr<object_subfile> primary(new object_subfile);
    if (!primary->load(filename))
        return;

    object_file *&of = object_files[filename];
    if (of == NULL)
        of = new object_file;
    of->text_avma = text_avma;
    delete of->subfiles[0];
    delete of->subfiles[1];
    of->subfiles[0] = primary.release();
    of->subfiles[1] = NULL;
    primary.release();

    char *gnu_debuglink = bfd_follow_gnu_debuglink(of->subfiles[0]->abfd, "/usr/lib/debug");
    if (gnu_debuglink != NULL)
    {
        auto_ptr<object_subfile> secondary(new object_subfile);
        if (secondary->load(gnu_debuglink))
            of->subfiles[1] = secondary.release();
        free(gnu_debuglink);
    }
}

struct addr2line_info
{
    HWord addr;
    object_file *obj;
    object_subfile *sub;
    bool found;
    const char *source;
    const char *function;
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

    if (!bfd_find_nearest_line(abfd, sect, &info->sub->syms[0],
                               info->addr - info->obj->text_avma,
                               &info->source, &info->function,
                               &info->line))
    {
        info->source = NULL;
        info->function = NULL;
        info->line = 0;
    }
    info->found = true;
}

string addr2line(HWord addr)
{
    ostringstream label;
    label << hex << showbase << addr << dec << noshowbase;
    for (map<string, object_file *>::iterator i = object_files.begin(); i != object_files.end(); ++i)
    {
        object_file *of = i->second;
        if (addr >= of->text_avma)
        {
            addr2line_info info;

            info.addr = addr;
            info.obj = of;
            info.found = false;
            for (int pass = 1; pass >= 0; pass--)
            {
                object_subfile *osf = of->subfiles[pass];
                if (osf == NULL) continue;

                info.sub = osf;
                bfd_map_over_sections(osf->abfd, addr2line_section, &info);
                if (info.found)
                {
                    if (info.function != NULL && info.function[0])
                    {
                        char *demangled = bfd_demangle(osf->abfd, info.function, 0);
                        if (demangled != NULL)
                        {
                            label << " in " << demangled;
                            free(demangled);
                        }
                        else
                            label << " in " << info.function;
                    }
                    label << " (";
                    if (info.source)
                    {
                        const char *suffix = strrchr(info.source, '/');
                        if (suffix == NULL)
                            suffix = info.source;
                        else
                            suffix++; /* Skip over the last / */
                        label << suffix;
                        if (info.line != 0)
                            label << ":" << info.line;
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
