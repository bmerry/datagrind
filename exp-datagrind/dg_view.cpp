#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include <vector>

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

    Event() {}
    Event(Addr addr, event_type type) : addr(addr), type(type) {}
};

static vector<event> events;

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
        printf("%d %#zx,%d\n", type, record[1], size);
        events.push_back(event(record[1], type == 1 ? EVENT_TYPE_READ : EVENT_TYPE_WRITE));
    }
    fclose(f);
}

static void usage(const char *prog, int code)
{
    fprintf("Usage: %s <file>\n", prog);
    exit(code);
}

typedef struct
{
    GLuint addr;
    GLuint time;
    GLubyte color[4];
} vertex;

static void init_gl(void)
{
    GLuint vbo;
    vertex *ptr;
    GLubyte color_read[4] = {0, 255, 0, 255};
    GLubyte color_write[4] = {0, 0, 255, 255};
    vertex *start = NULL;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, events.size() * sizeof(vertex), NULL, GL_STATIC_DRAW);
    ptr = (vertex *) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    for (size_t i = 0; i < events.size(); i++)
    {
        ptr[i].addr = events[i].addr >> 4;
        ptr[i].time = i;
        switch (events[i].type)
        {
        case EVENT_TYPE_READ:
            memcpy(ptr[i].color, color_read, sizeof(color_read));
            break;
        case EVENT_TYPE_WRITE:
            memcpy(ptr[i].color, color_write, sizeof(color_write));
            break;
        }
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);

    glVertexPointer(2, GL_UNSIGNED_INT, sizeof(vertex), &start->addr);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex), &start->color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
}

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, events.size());
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
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutCreateWindow("dg_view");
    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glewInit();
    init_gl();
    glutMainLoop();

    return 0;
}
