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

using namespace std;

typedef uintptr_t Addr;

typedef enum
{
    EVENT_TYPE_READ,
    EVENT_TYPE_WRITE
} event_type;

struct event
{
    Addr addr;
    event_type type;

    event() {}
    event(Addr addr, event_type type) : addr(addr), type(type) {}
};

#define PAGE_SIZE 4096

static vector<event> events;
static map<Addr, size_t> page_map;

template<typename T> T page_round_down(T x)
{
    return x & ~(PAGE_SIZE - 1);
}

static void load(const char *filename)
{
    Addr record[2];

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Could not open `%s'.\n", filename);
        exit(1);
    }
    while (fread(record, sizeof(uintptr_t), 2, f) == 2)
    {
        int size = record[0] >> 2;
        int type = record[0] & 3;
        // printf("%d %#zx,%d\n", type, record[1], size);
        events.push_back(event(record[1], type == 1 ? EVENT_TYPE_READ : EVENT_TYPE_WRITE));

        page_map[page_round_down(events.back().addr)] = 0;
    }
    fclose(f);

    size_t remapped_base = 0;
    for (map<Addr, size_t>::iterator i = page_map.begin(); i != page_map.end(); i++)
    {
        i->second = remapped_base;
        remapped_base += PAGE_SIZE;
    }
}

static size_t remap_address(Addr a)
{
    Addr base = page_round_down(a);
    map<Addr, size_t>::const_iterator it = page_map.find(base);
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

static void init_gl(void)
{
    GLuint vbo;
    GLubyte color_read[4] = {0, 255, 0, 255};
    GLubyte color_write[4] = {0, 0, 255, 255};
    vertex *start = NULL;
    vector<vertex> vertices(events.size());
    GLfloat min_pos = HUGE_VALF, max_pos = -HUGE_VALF;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (size_t i = 0; i < events.size(); i++)
    {
        vertices[i].pos[0] = remap_address(events[i].addr);
        vertices[i].pos[1] = i;
        min_pos = min(min_pos, vertices[i].pos[0]);
        max_pos = max(max_pos, vertices[i].pos[0]);
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

    glVertexPointer(2, GL_FLOAT, sizeof(vertex), &start->pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex), &start->color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glLoadIdentity();
    glOrtho(min_pos, max_pos, events.size(), 0.0, -1.0, 1.0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, events.size());
    glutSwapBuffers();
}

static void idle(void)
{
    glutPostRedisplay();
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
    glutIdleFunc(idle);
    glewInit();
    init_gl();
    glutMainLoop();

    return 0;
}
