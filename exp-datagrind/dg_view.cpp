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
#include <map>

#include "dg_record.h"

using namespace std;

typedef uintptr_t HWord;

typedef enum
{
    EVENT_TYPE_READ,
    EVENT_TYPE_WRITE
} event_type;

struct event
{
    HWord addr;
    event_type type;

    event() {}
    event(HWord addr, event_type type) : addr(addr), type(type) {}
};

#define PAGE_SIZE 4096
#define LINE_SIZE 64

static vector<event> events;
static map<HWord, size_t> page_map;

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
            if (len > body_size)
            {
                delete body;
                body = new uint8_t[len];
                body_size = len;
            }
            if (fread(body, 1, len, f) != len)
            {
                fprintf(stderr, "Warning: short record at end of file\n");
                break;
            }

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
                        events.push_back(event(addr, type == DG_R_READ ? EVENT_TYPE_READ : EVENT_TYPE_WRITE));
                        page_map[page_round_down(addr)] = 0;
                    }
                    break;
                default:
                    fprintf(stderr, "Error: unknown record type %#x\n", type);
                    goto bad_file;
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

static void init_gl(void)
{
    GLuint vbo;
    GLubyte color_read[4] = {0, 255, 0, 255};
    GLubyte color_write[4] = {0, 0, 255, 255};
    vertex *start = NULL;
    vector<vertex> vertices(events.size());
    min_x = HUGE_VALF;
    max_x = -HUGE_VALF;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (size_t i = 0; i < events.size(); i++)
    {
        vertices[i].pos[0] = remap_address(events[i].addr);
        vertices[i].pos[1] = i;
        min_x = min(min_x, vertices[i].pos[0]);
        max_x = max(max_x, vertices[i].pos[0]);
        switch (events[i].type)
        {
        case EVENT_TYPE_READ:
            memcpy(vertices[i].color, color_read, sizeof(color_read));
            break;
        case EVENT_TYPE_WRITE:
            memcpy(vertices[i].color, color_write, sizeof(color_write));
            break;
        }
    }
    glBufferData(GL_ARRAY_BUFFER, events.size() * sizeof(vertex), &vertices[0], GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr, "Error initialising GL state\n");
        exit(1);
    }

    glVertexPointer(2, GL_FLOAT, sizeof(vertex), &start->pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex), &start->color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    min_y = -1.0f;
    max_y = events.size();
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
            glColor3ub(192, 192, 192);
            glVertex2f(i->second, 0.0f);
            glVertex2f(i->second, events.size());
        }
        else if (xrate < PAGE_SIZE / 8)
        {
            glColor3ub(64, 64, 64);
            glVertex2f(i->second, 0.0f);
            glVertex2f(i->second, events.size());
        }
        if (xrate < LINE_SIZE / 8)
        {
            glColor3ub(96, 32, 32);
            for (int j = LINE_SIZE; j < PAGE_SIZE; j += LINE_SIZE)
            {
                glVertex2f(i->second + j, 0.0f);
                glVertex2f(i->second + j, events.size());
            }
        }
        last = i->first;
    }
    glEnd();

    glDrawArrays(GL_POINTS, 0, events.size());

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

    glutInitWindowSize(800, 600);
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
