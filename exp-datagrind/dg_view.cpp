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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <stdint.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <getopt.h>
#include <vector>
#include <algorithm>
#include <set>
#include <map>
#include <string>
#include <memory>
#include <sstream>

#include "dg_record.h"
#include "dg_view.h"
#include "dg_view_range.h"
#include "dg_view_debuginfo.h"
#include "dg_view_parse.h"

using namespace std;

struct mem_block
{
    HWord addr;
    HWord size;
    vector<HWord> stack;
    string label;
};

struct mem_access
{
    HWord addr;
    uint8_t dir;
    uint8_t size;
    HWord iaddr;
    uint64_t iseq;
    mem_block *block;

    mem_access() {}
    mem_access(HWord addr, uint8_t dir, uint8_t size, HWord iaddr, uint64_t iseq, mem_block *block)
        : addr(addr), dir(dir), size(size), iaddr(iaddr), iseq(iseq), block(block) {}
};

struct bbdef_access
{
    uint8_t dir;
    uint8_t size;
    uint8_t iseq;
};

struct bbdef
{
    vector<HWord> instr_addrs;
    vector<bbdef_access> accesses;
};

struct bbrun
{
    uint64_t bbdef_index;
    uint64_t iseq_start;
    uint64_t dseq_start;
    vector<HWord> stack;
    vector<HWord> addresses;
    vector<mem_block *> blocks;
};

struct compare_mem_access_iseq
{
    bool operator()(const mem_access &a, const mem_access &b) const
    {
        return a.iseq < b.iseq;
    }

    bool operator()(const mem_access &a, uint64_t iseq) const
    {
        return a.iseq < iseq;
    }

    bool operator()(uint64_t iseq, const mem_access &a) const
    {
        return iseq < a.iseq;
    }
};

#define DG_VIEW_PAGE_SIZE 4096
#define DG_VIEW_LINE_SIZE 64

/* All START_EVENTs with no matching END_EVENT from chosen_events */
static multiset<string> active_events;
/* All TRACK_RANGEs with no matching UNTRACK_RANGE from chosen_ranges */
static multiset<pair<HWord, HWord> > active_ranges;

static rangemap<HWord, mem_block *> block_map;
static vector<mem_block *> block_storage;

/* Events selected on the command line, or empty if there wasn't a choice */
static set<string> chosen_events;
/* Ranges selected on the command line, or empty if there wasn't a choice */
static set<string> chosen_ranges;

static vector<bbdef> bbdefs;
static vector<bbrun> bbruns;
static vector<mem_access> accesses;
static map<HWord, size_t> page_map;
static map<size_t, HWord> rev_page_map;

static GLuint num_vertices;

template<typename T> T page_round_down(T x)
{
    return x & ~(DG_VIEW_PAGE_SIZE - 1);
}

static vector<mem_access>::iterator nearest_access(double addr, double iseq, double addr_scale, double iseq_scale)
{
    /* Start at the right instruction and search outwards until we can bound
     * the search.
     */
    vector<mem_access>::iterator back, forw, best;
    double best_score = HUGE_VAL;

    forw = lower_bound(accesses.begin(), accesses.end(), (uint64_t) iseq, compare_mem_access_iseq());
    back = forw;
    best = forw;
    while (forw != accesses.end() || back != accesses.begin())
    {
        if (forw != accesses.end())
        {
            double iseq_score = (forw->iseq - iseq) * iseq_scale;
            if (iseq_score > best_score)
                forw = accesses.end();
            else
            {
                double addr_score = (forw->addr - addr) * addr_scale;
                double score = hypot(addr_score, iseq_score);
                if (score < best_score)
                {
                    best_score = score;
                    best = forw;
                }
                ++forw;
            }
        }
        if (back != accesses.begin())
        {
            --back;
            double iseq_score = (iseq - back->iseq) * iseq_scale;
            if (iseq_scale > best_score)
                back = accesses.begin();
            else
            {
                double addr_score = (back->addr - addr) * addr_scale;
                double score = hypot(addr_score, iseq_score);
                if (score < best_score)
                {
                    best_score = score;
                    best = back;
                }
            }
        }
    }
    return best;
}

static void add_access(HWord addr, uint8_t dir, uint8_t size, HWord iaddr, uint64_t iseq)
{
    bool matched;
    if (!chosen_events.empty() && active_events.empty())
        matched = false;
    else if (!chosen_ranges.empty())
    {
        matched = false;
        for (multiset<pair<HWord, HWord> >::const_iterator i = active_ranges.begin(); i != active_ranges.end(); ++i)
        {
            if (addr + size > i->first && addr < i->first + i->second)
            {
                matched = true;
                break;
            }
        }
    }
    else
        matched = true;

    if (matched)
    {
        mem_block *block = NULL;
        rangemap<HWord, mem_block *>::iterator block_it = block_map.find(addr);
        if (block_it != block_map.end())
            block = block_it->second;
        accesses.push_back(mem_access(addr, dir, size, iaddr, iseq, block));
        page_map[page_round_down(addr)] = 0;
    }
}

static void load(const char *filename)
{
    bool first = true;
    uint64_t iseq = 0;
    uint64_t dseq = 0;
    HWord iaddr = 0;
    record_parser *rp_ptr;

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Could not open `%s'.\n", filename);
        exit(1);
    }
    while (NULL != (rp_ptr = record_parser::create(f)))
    {
        auto_ptr<record_parser> rp(rp_ptr);
        uint8_t type = rp->get_type();

        try
        {
            if (first)
            {
                uint8_t version, endian, wordsize;

                if (type != DG_R_HEADER)
                    throw record_parser_error("Error: did not find header");
                if (rp->extract_string() != "DATAGRIND1")
                    throw record_parser_error("Error: did not find signature");
                version = rp->extract_byte();
                endian = rp->extract_byte();
                wordsize = rp->extract_byte();
                int expected_version = 1;
                if (version != expected_version)
                {
                    fprintf(stderr, "Warning: version mismatch (expected %d, got %u).\n",
                            expected_version, version);
                }
                /* TODO: do something useful with endianness */
                if (wordsize != sizeof(HWord))
                {
                    ostringstream msg;
                    msg << "Error: pointer size mismatch (expected " << sizeof(HWord) << ", got " << wordsize << ")";
                    throw record_parser_content_error(msg.str());
                }

                first = false;
            }
            else
            {
                switch (type)
                {
                case DG_R_HEADER:
                    throw record_parser_content_error("Error: found header after first record.\n");

                case DG_R_BBDEF:
                    {
                        bbdef bbd;
                        uint8_t n_instrs = rp->extract_byte();
                        HWord n_accesses = rp->extract_word();

                        if (n_instrs == 0)
                        {
                            throw record_parser_content_error("Error: empty BB");
                        }
                        bbd.instr_addrs.resize(n_instrs);
                        bbd.accesses.resize(n_accesses);

                        for (HWord i = 0; i < n_instrs; i++)
                        {
                            bbd.instr_addrs[i] = rp->extract_word();
                            // discard size
                            (void) rp->extract_byte();
                        }
                        for (HWord i = 0; i < n_accesses; i++)
                        {
                            bbd.accesses[i].dir = rp->extract_byte();
                            bbd.accesses[i].size = rp->extract_byte();
                            bbd.accesses[i].iseq = rp->extract_byte();
                        }
                        bbdefs.push_back(bbd);
                    }
                    break;
                case DG_R_BBRUN:
                    {
                        bbrun bbr;

                        bbr.bbdef_index = rp->extract_word();
                        if (bbr.bbdef_index >= bbdefs.size())
                        {
                            ostringstream msg;
                            msg << "Error: bbdef index " << bbr.bbdef_index << " is out of range";
                            throw record_parser_content_error(msg.str());
                        }

                        uint8_t n_stack = rp->extract_byte();
                        if (n_stack == 0)
                            throw record_parser_content_error("Error: empty call stack");
                        bbr.stack.resize(n_stack);
                        for (uint8_t i = 0; i < n_stack; i++)
                            bbr.stack[i] = rp->extract_word();

                        const bbdef &bbd = bbdefs[bbr.bbdef_index];
                        uint8_t n_instrs = rp->extract_byte();
                        uint64_t n_addrs = rp->remain() / sizeof(HWord);
                        if (n_addrs > bbd.accesses.size())
                            throw record_parser_content_error("Error: too many access addresses");

                        for (HWord i = 0; i < n_addrs; i++)
                        {
                            HWord addr = rp->extract_word();
                            const bbdef_access &access = bbd.accesses[i];
                            add_access(addr, access.dir, access.size, bbd.instr_addrs[access.iseq], iseq + access.iseq);
                        }

                        bbruns.push_back(bbr);
                        iseq += n_instrs;
                        dseq += n_addrs;
                    }
                    break;
                case DG_R_INSTR:
                    {
                        /* size = */ rp->extract_byte();
                        iaddr = rp->extract_word();
                        iseq++;
                    }
                    break;
                case DG_R_READ:
                    {
                        uint8_t size = rp->extract_byte();
                        HWord addr = rp->extract_word();
                        add_access(addr, DG_ACC_READ, size, iaddr, iseq);
                        dseq++;
                    }
                    break;
                case DG_R_WRITE:
                    {
                        uint8_t size = rp->extract_byte();
                        HWord addr = rp->extract_word();
                        add_access(addr, DG_ACC_WRITE, size, iaddr, iseq);
                        dseq++;
                    }
                    break;
                case DG_R_TRACK_RANGE:
                    {
                        HWord addr = rp->extract_word();
                        HWord size = rp->extract_word();

                        string var_type = rp->extract_string();
                        string label = rp->extract_string();

                        if (chosen_ranges.count(label))
                            active_ranges.insert(make_pair(addr, size));
                    }
                    break;
                case DG_R_UNTRACK_RANGE:
                    {
                        HWord addr = rp->extract_word();
                        HWord size = rp->extract_word();

                        pair<HWord, HWord> key(addr, size);
                        multiset<pair<HWord, HWord> >::iterator it = active_ranges.find(key);
                        if (it != active_ranges.end())
                            active_ranges.erase(it);
                    }
                    break;
                case DG_R_MALLOC_BLOCK:
                    {
                        HWord addr = rp->extract_word();
                        HWord size = rp->extract_word();
                        HWord n_ips = rp->extract_word();
                        vector<HWord> ips;

                        mem_block *block = new mem_block;
                        block->addr = addr;
                        block->size = size;
                        block->stack.reserve(n_ips);
                        for (HWord i = 0; i < n_ips; i++)
                        {
                            HWord stack_addr = rp->extract_word();
                            block->stack.push_back(stack_addr);
                        }
                        block_storage.push_back(block);
                        block_map.insert(addr, addr + size, block);
                    }
                    break;
                case DG_R_FREE_BLOCK:
                    {
                        HWord addr = rp->extract_word();
                        block_map.erase(addr);
                    }
                    break;
                case DG_R_START_EVENT:
                case DG_R_END_EVENT:
                    {
                        string label = rp->extract_string();
                        if (chosen_events.count(label))
                        {
                            if (type == DG_R_START_EVENT)
                                active_events.insert(label);
                            else
                            {
                                multiset<string>::iterator it = active_events.find(label);
                                if (it != active_events.end())
                                    active_events.erase(it);
                            }
                        }
                    }
                    break;
                case DG_R_TEXT_AVMA:
                    {
                        HWord avma = rp->extract_word();
                        string filename = rp->extract_string();
                        load_object_file(filename.c_str(), avma);
                    }
                    break;
                default:
                    {
                        ostringstream msg;
                        msg << showbase << hex;
                        msg << "Error: unknown record type " << (unsigned int) type;
                        throw record_parser_content_error(msg.str());
                    }
                }
            }
            rp->finish();
        }
        catch (record_parser_content_error &e)
        {
            fprintf(stderr, "%s\n", e.what());
            rp->discard();
        }
        catch (record_parser_error &e)
        {
            fprintf(stderr, "%s\n", e.what());
            exit(1);
        }
    }
    fclose(f);

    size_t remapped_base = 0;
    for (map<HWord, size_t>::iterator i = page_map.begin(); i != page_map.end(); i++)
    {
        i->second = remapped_base;
        remapped_base += DG_VIEW_PAGE_SIZE;
        rev_page_map[i->second] = i->first;
    }
}

static size_t remap_address(HWord a)
{
    HWord base = page_round_down(a);
    map<HWord, size_t>::const_iterator it = page_map.find(base);
    assert(it != page_map.end());
    return (a - base) + it->second;
}

static void usage(const char *prog, int code)
{
    fprintf(stderr, "Usage: %s [--ranges=r1,r2] [--events=e1,e1] <file>\n", prog);
    exit(code);
}

typedef struct
{
    GLfloat pos[2];
    GLubyte color[4];
} vertex;

static GLfloat min_x, max_x, min_y, max_y;
static GLfloat window_width, window_height;
static GLint zoom_x, zoom_y;

static size_t count_access_bytes(void)
{
    size_t total = 0;
    for (size_t i = 0; i < accesses.size(); i++)
        total += accesses[i].size;
    return total;
}

static void init_gl(void)
{
    GLuint vbo;
    GLubyte color_read[4] = {0, 255, 0, 255};
    GLubyte color_write[4] = {0, 0, 255, 255};
    GLubyte color_instr[4] = {255, 0, 0, 255};
    vertex *start = NULL;
    num_vertices = count_access_bytes();

    vector<vertex> vertices(num_vertices);
    min_x = HUGE_VALF;
    max_x = -HUGE_VALF;

    size_t v = 0;
    for (size_t i = 0; i < accesses.size(); i++)
    {
        for (int j = 0; j < accesses[i].size; j++)
        {
            vertices[v].pos[0] = remap_address(accesses[i].addr) + j;
            vertices[v].pos[1] = accesses[i].iseq;
            min_x = min(min_x, vertices[v].pos[0]);
            max_x = max(max_x, vertices[v].pos[0]);
            switch (accesses[i].dir)
            {
            case DG_ACC_READ:
                memcpy(vertices[v].color, color_read, sizeof(color_read));
                break;
            case DG_ACC_WRITE:
                memcpy(vertices[v].color, color_write, sizeof(color_write));
                break;
            case DG_ACC_EXEC:
                memcpy(vertices[v].color, color_instr, sizeof(color_instr));
                break;
            }
            v++;
        }
    }
    assert(v == num_vertices);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr, "Error initialising GL state\n");
        exit(1);
    }
    glBufferData(GL_ARRAY_BUFFER, num_vertices * sizeof(vertex), &vertices[0], GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr,
                "Error loading buffer data. It may be more than your GL implementation can handle.\n"
                "Try using the --events and --ranges options.\n");
        exit(1);
    }

    glVertexPointer(2, GL_FLOAT, sizeof(vertex), &start->pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex), &start->color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBlendFuncSeparate(GL_ONE, GL_DST_ALPHA, GL_ONE, GL_ZERO);
    glEnable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    min_y = vertices[0].pos[1] - 1.0f;
    max_y = vertices.back().pos[1] + 1.0f;
    min_x -= 0.5f;
    max_x += 0.5f;

    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr, "Error initialising GL state\n");
        exit(1);
    }
}

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glLoadIdentity();
    glOrtho(min_x, max_x, max_y, min_y, -1.0, 1.0);

    HWord last = 0;
    GLfloat xrate = (max_x - min_x) / window_width;
    glBegin(GL_LINES);
    for (map<HWord, size_t>::const_iterator i = page_map.begin(); i != page_map.end(); ++i)
    {
        if (i->first != last + DG_VIEW_PAGE_SIZE)
        {
            glColor4ub(192, 192, 192, 0);
            glVertex2f(i->second, min_y);
            glVertex2f(i->second, max_y);
        }
        else if (xrate < DG_VIEW_PAGE_SIZE / 8)
        {
            glColor4ub(64, 64, 64, 0);
            glVertex2f(i->second, min_y);
            glVertex2f(i->second, max_y);
        }
        if (xrate < DG_VIEW_LINE_SIZE / 8)
        {
            glColor4ub(96, 32, 32, 0);
            for (int j = DG_VIEW_LINE_SIZE; j < DG_VIEW_PAGE_SIZE; j += DG_VIEW_LINE_SIZE)
            {
                glVertex2f(i->second + j, min_y);
                glVertex2f(i->second + j, max_y);
            }
        }
        last = i->first;
    }
    glEnd();

    glDrawArrays(GL_POINTS, 0, num_vertices);

    glutSwapBuffers();
}

static void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            zoom_x = x;
            zoom_y = y;
        }
        else if (abs(zoom_x - x) > 2 && abs(zoom_y - y) > 2)
        {
            GLfloat x1 = min_x + (zoom_x + 0.5) * (max_x - min_x) / window_width;
            GLfloat y1 = min_y + (zoom_y + 0.5) * (max_y - min_y) / window_height;
            GLfloat x2 = min_x + (x + 0.5) * (max_x - min_x) / window_width;
            GLfloat y2 = min_y + (y + 0.5) * (max_y - min_y) / window_height;

            min_x = min(x1, x2) - 0.5f;
            max_x = max(x1, x2) + 0.5f;
            min_y = min(y1, y2) - 0.5f;
            max_y = max(y1, y2) + 0.5f;
            glutPostRedisplay();
        }
        else
        {
            HWord remapped = (HWord) (0.5 + (GLdouble) (x + 0.5f) / window_width * (max_x - min_x) + min_x);
            HWord remapped_page = page_round_down(remapped);
            HWord page = rev_page_map[remapped_page];

            HWord addr = (remapped - remapped_page) + page;
            double seq = (GLdouble) (y + 0.5f) / window_height * (max_y - min_y) + min_y;

            double addr_scale = window_width / (max_x - min_x);
            double seq_scale = window_height / (max_y - min_y);

            vector<mem_access>::const_iterator access = nearest_access(addr, seq, addr_scale, seq_scale);
            if (access != accesses.end())
            {
                printf("Nearest access: %#zx", access->addr);
                mem_block *block = access->block;
                if (block != NULL)
                {
                    printf(": %zu bytes inside a block of size %zu, allocated at\n",
                           addr - block->addr, block->size);
                    for (size_t i = 0; i < block->stack.size(); i++)
                    {
                        string loc = addr2line(block->stack[i]);
                        printf("  %s\n", loc.c_str());
                    }
                }
                else
                    printf("\n");

                if (access->iaddr != 0)
                {
                    string loc = addr2line(access->iaddr);
                    printf("At %s\n", loc.c_str());
                }
            }
        }
    }
}

static void reshape(int width, int height)
{
    window_width = width;
    window_height = height;
    glViewport(0, 0, width, height);
}

/* Splits a string to pieces on commas. Empty parts are preserved, but if
 * s is empty then no strings are returned.
 */
static vector<string> split_comma(const string &s)
{
    vector<string> out;
    string::size_type pos = 0;

    if (s.empty()) return out;
    while (true)
    {
        string::size_type next = s.find(',', pos);
        if (next == string::npos)
        {
            /* End of string - capture the last element */
            out.push_back(s.substr(pos));
            return out;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
}

static void parse_opts(int *argc, char **argv)
{
    static const struct option longopts[] =
    {
        { "ranges", 1, NULL, 'r' },
        { "events", 1, NULL, 'e' },
        { NULL, 0, NULL, 0 }
    };
    int opt;

    while ((opt = getopt_long(*argc, argv, "r:e:", longopts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'r':
            {
                vector<string> ranges = split_comma(optarg);
                chosen_ranges = set<string>(ranges.begin(), ranges.end());
            }
            break;
        case 'e':
            {
                vector<string> events = split_comma(optarg);
                chosen_events = set<string>(events.begin(), events.end());
            }
            break;
        case '?':
        case ':':
            exit(2);
        default:
            assert(0);
        }
    }

    /* Remove options from argv */
    for (int i = optind; i < *argc; i++)
        argv[i - optind + 1] = argv[i];
    *argc -= optind - 1;
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    parse_opts(&argc, argv);

    if (argc != 2)
    {
        usage(argv[0], 2);
    }
    load(argv[1]);
    if (accesses.empty())
    {
        fprintf(stderr, "No accesses match the criteria.\n");
        return 0;
    }

    glutInitWindowSize(800, 800);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutCreateWindow("dg_view");
    glutDisplayFunc(display);
    glutMouseFunc(mouse);
    glutReshapeFunc(reshape);
    glewInit();
    if (!GLEW_VERSION_1_5)
    {
        fprintf(stderr, "OpenGL 1.5 or later is required.\n");
        return 1;
    }
    init_gl();
    glutMainLoop();

    return 0;
}
