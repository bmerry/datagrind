#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <stdint.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <vector>
#include <algorithm>
#include <set>
#include <map>
#include <string>

#include "dg_record.h"

using namespace std;

typedef uintptr_t HWord;

typedef enum
{
    ACCESS_TYPE_READ,
    ACCESS_TYPE_WRITE,
} access_type;

struct mem_access
{
    HWord addr;
    uint8_t size;
    access_type type;
    uint64_t seq;

    mem_access() {}
    mem_access(HWord addr, uint8_t size, access_type type, uint64_t seq) : addr(addr), size(size), type(type), seq(seq) {}
};

#define PAGE_SIZE 4096
#define LINE_SIZE 64

/* All START_EVENTs with no matching END_EVENT from chosen_events */
static multiset<string> active_events;
/* All TRACK_RANGEs with no matching UNTRACK_RANGE from chosen_ranges */
static multiset<pair<HWord, HWord> > active_ranges;

/* Events selected on the command line, or empty if there wasn't a choice */
static set<string> chosen_events;
/* Ranges selected on the command line, or empty if there wasn't a choice */
static set<string> chosen_ranges;

static vector<mem_access> accesses;
static map<HWord, size_t> page_map;

static GLuint num_vertices;

template<typename T> T page_round_down(T x)
{
    return x & ~(PAGE_SIZE - 1);
}

static void load(const char *filename)
{
    uint8_t type;
    uint8_t len;
    uint8_t *body = NULL;
    unsigned int body_size = 0;
    bool first = true;
    size_t seq = 0;

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Could not open `%s'.\n", filename);
        exit(1);
    }
    while (fread(&type, 1, 1, f) == 1 && fread(&len, 1, 1, f) == 1)
    {
        /* No non-zero subtypes or short records so far */
        if (type & 0x80)
        {
            fprintf(stderr, "Warning: unexpected header byte %#x\n", type);
            continue;
        }
        else
        {
            if (len >= body_size)
            {
                delete[] body;
                body = new uint8_t[len + 1];
                body_size = len + 1;
            }
            if (fread(body, 1, len, f) != len)
            {
                fprintf(stderr, "Warning: short record at end of file\n");
                break;
            }
            body[len] = '\0'; /* Guarantees termination of embedded strings */

            if (first)
            {
                const char magic[] = "DATAGRIND1";
                uint8_t version, endian, wordsize;
                if (type != DG_R_HEADER)
                {
                    fprintf(stderr, "Error: did not find header\n");
                    goto bad_file;
                }
                if (len < sizeof(magic) + 3)
                {
                    fprintf(stderr, "Error: header too short\n");
                    goto bad_file;
                }
                if (0 != strncmp(magic, (const char *) body, sizeof(magic)))
                {
                    fprintf(stderr, "Error: header magic does not match\n");
                    goto bad_file;
                }
                version = body[sizeof(magic)];
                endian = body[sizeof(magic) + 1];
                wordsize = body[sizeof(magic) + 2];
                int expected_version = 1;
                if (version != 1)
                {
                    fprintf(stderr, "Warning: version mismatch (expected %d, got %u).\n",
                            expected_version, version);
                }
                /* TODO: do something useful with endianness */
                if (wordsize != sizeof(HWord))
                {
                    fprintf(stderr, "Error: pointer size mismatch (expected %u, got %u)\n",
                            (unsigned int) sizeof(HWord), wordsize);
                    goto bad_file;
                }

                first = false;
            }
            else
            {
                switch (type)
                {
                case DG_R_HEADER:
                    fprintf(stderr, "Warning: found header after first record.\n");
                    goto bad_record;
                case DG_R_READ:
                case DG_R_WRITE:
                    {
                        if (len != 1 + sizeof(HWord))
                        {
                            fprintf(stderr, "Error: Wrong record length");
                            goto bad_record;
                        }
                        uint8_t size = body[0];
                        HWord addr;
                        memcpy(&addr, body + 1, sizeof(addr));

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
                            accesses.push_back(mem_access(addr, size, type == DG_R_READ ? ACCESS_TYPE_READ : ACCESS_TYPE_WRITE, seq));
                            page_map[page_round_down(addr)] = 0;
                        }
                        seq++;
                    }
                    break;
                case DG_R_TRACK_RANGE:
                    {
                        HWord addr;
                        HWord size;
                        const uint8_t *ptr = body;
                        if (len < 2 * sizeof(HWord) + 2)
                        {
                            fprintf(stderr, "Error: record too short (%d < %zu)\n",
                                    len, 2 * sizeof(HWord) + 2);
                            goto bad_record;
                        }
                        memcpy(&addr, ptr, sizeof(addr));
                        ptr += sizeof(addr);
                        memcpy(&size, ptr, sizeof(size));
                        ptr += sizeof(size);
                        string var_type = (const char *) ptr;
                        ptr += var_type.size() + 1;
                        if (ptr >= body + len)
                        {
                            fprintf(stderr, "Error: record missing field\n");
                            goto bad_record;
                        }
                        string label = (const char *) ptr;
                        ptr += label.size() + 1;
                        if (ptr != body + len)
                        {
                            fprintf(stderr, "Error: record not properly terminated (%p vs %p)\n",
                                    ptr, body + len);
                            goto bad_record;
                        }

                        if (chosen_ranges.count(label))
                            active_ranges.insert(make_pair(addr, size));
                    }
                    break;
                case DG_R_UNTRACK_RANGE:
                    {
                        HWord addr;
                        HWord size;
                        if (len != 2 * sizeof(HWord))
                        {
                            fprintf(stderr, "Error: record has wrong size\n");
                            goto bad_record;
                        }
                        memcpy(&addr, body, sizeof(addr));
                        memcpy(&size, body + sizeof(addr), sizeof(size));

                        pair<HWord, HWord> key(addr, size);
                        multiset<pair<HWord, HWord> >::iterator it = active_ranges.find(key);
                        if (it != active_ranges.end())
                            active_ranges.erase(it);
                    }
                    break;
                case DG_R_START_EVENT:
                case DG_R_END_EVENT:
                    {
                        string label = (char *) body;
                        if (label.size() + 1 != len)
                        {
                            fprintf(stderr, "Error: record not properly terminated\n");
                            goto bad_record;
                        }
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
                default:
                    fprintf(stderr, "Warning: unknown record type %#x\n", type);
                    goto bad_record;
                }
            }
        }
bad_record:;
    }
bad_file:
    delete body;
    fclose(f);

    size_t remapped_base = 0;
    for (map<HWord, size_t>::iterator i = page_map.begin(); i != page_map.end(); i++)
    {
        i->second = remapped_base;
        remapped_base += PAGE_SIZE;
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
    fprintf(stderr, "Usage: %s <file>\n", prog);
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
            vertices[v].pos[1] = accesses[i].seq;
            min_x = min(min_x, vertices[v].pos[0]);
            max_x = max(max_x, vertices[v].pos[0]);
            switch (accesses[i].type)
            {
            case ACCESS_TYPE_READ:
                memcpy(vertices[v].color, color_read, sizeof(color_read));
                break;
            case ACCESS_TYPE_WRITE:
                memcpy(vertices[v].color, color_write, sizeof(color_write));
                break;
            }
            v++;
        }
    }
    assert(v == num_vertices);
    vector<mem_access>().swap(accesses); // free memory for accesses

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, num_vertices * sizeof(vertex), &vertices[0], GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr, "Error initialising GL state\n");
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
        if (i->first != last + PAGE_SIZE)
        {
            glColor4ub(192, 192, 192, 0);
            glVertex2f(i->second, min_y);
            glVertex2f(i->second, max_y);
        }
        else if (xrate < PAGE_SIZE / 8)
        {
            glColor4ub(64, 64, 64, 0);
            glVertex2f(i->second, min_y);
            glVertex2f(i->second, max_y);
        }
        if (xrate < LINE_SIZE / 8)
        {
            glColor4ub(96, 32, 32, 0);
            for (int j = LINE_SIZE; j < PAGE_SIZE; j += LINE_SIZE)
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

            min_x = min(x1, x2);
            max_x = max(x1, x2);
            min_y = min(y1, y2);
            max_y = max(y1, y2);
            glutPostRedisplay();
        }
    }
}

static void reshape(int width, int height)
{
    window_width = width;
    window_height = height;
    glViewport(0, 0, width, height);
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);

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
    glutInitDisplayMode(GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE);
    glutCreateWindow("dg_view");
    glutDisplayFunc(display);
    glutMouseFunc(mouse);
    glutReshapeFunc(reshape);
    glewInit();
    init_gl();
    glutMainLoop();

    return 0;
}
