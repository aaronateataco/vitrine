#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>

#include "diag.h"
#include "scene.h"

#define PI_F 3.14159265358979f

struct Scene {
    GLuint program;
    GLuint vbo;
    GLint  a_pos;
    GLint  a_normal;
    GLint  u_mvp;
    GLint  u_model;
    GLint  u_color;
};

static const char *VERTEX_SRC =
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_normal;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "varying vec3 v_normal;\n"
    "void main() {\n"
    "    v_normal = mat3(u_model[0].xyz, u_model[1].xyz, u_model[2].xyz) * a_normal;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *FRAGMENT_SRC =
    "precision mediump float;\n"
    "varying vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(vec3(0.4, 0.85, 0.6));\n"
    "    float d = max(dot(n, l), 0.0);\n"
    "    float rim = pow(1.0 - max(n.z, 0.0), 2.0) * 0.25;\n"
    "    gl_FragColor = vec4(u_color * (0.32 + 0.68 * d) + rim, 1.0);\n"
    "}\n";

/* Position + normal per vertex, two triangles per face. */
static const GLfloat CUBE[] = {
    /* +Z */ -1,-1, 1, 0,0,1,   1,-1, 1, 0,0,1,   1, 1, 1, 0,0,1,
             -1,-1, 1, 0,0,1,   1, 1, 1, 0,0,1,  -1, 1, 1, 0,0,1,
    /* -Z */  1,-1,-1, 0,0,-1, -1,-1,-1, 0,0,-1, -1, 1,-1, 0,0,-1,
              1,-1,-1, 0,0,-1, -1, 1,-1, 0,0,-1,  1, 1,-1, 0,0,-1,
    /* +X */  1,-1, 1, 1,0,0,   1,-1,-1, 1,0,0,   1, 1,-1, 1,0,0,
              1,-1, 1, 1,0,0,   1, 1,-1, 1,0,0,   1, 1, 1, 1,0,0,
    /* -X */ -1,-1,-1,-1,0,0,  -1,-1, 1,-1,0,0,  -1, 1, 1,-1,0,0,
             -1,-1,-1,-1,0,0,  -1, 1, 1,-1,0,0,  -1, 1,-1,-1,0,0,
    /* +Y */ -1, 1, 1, 0,1,0,   1, 1, 1, 0,1,0,   1, 1,-1, 0,1,0,
             -1, 1, 1, 0,1,0,   1, 1,-1, 0,1,0,  -1, 1,-1, 0,1,0,
    /* -Y */ -1,-1,-1, 0,-1,0,  1,-1,-1, 0,-1,0,  1,-1, 1, 0,-1,0,
             -1,-1,-1, 0,-1,0,  1,-1, 1, 0,-1,0, -1,-1, 1, 0,-1,0,
};

static void mat_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_multiply(float *out, const float *a, const float *b)
{
    float tmp[16];
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[col * 4 + k];
            tmp[col * 4 + row] = sum;
        }
    memcpy(out, tmp, sizeof(tmp));
}

static void mat_perspective(float *m, float fov_y, float aspect, float near_z,
                            float far_z)
{
    float f = 1.0f / tanf(fov_y * 0.5f);

    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far_z + near_z) / (near_z - far_z);
    m[11] = -1.0f;
    m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
}

static void normalise(float *v)
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len <= 1e-6f)
        return;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

static void cross(float *out, const float *a, const float *b)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void mat_look_at(float *m, const float *eye, const float *target)
{
    static const float world_up[3] = { 0.0f, 1.0f, 0.0f };

    float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
    normalise(forward);

    float right[3];
    cross(right, forward, world_up);
    normalise(right);

    float up[3];
    cross(up, right, forward);

    mat_identity(m);
    m[0] = right[0];   m[4] = right[1];   m[8]  = right[2];
    m[1] = up[0];      m[5] = up[1];      m[9]  = up[2];
    m[2] = -forward[0];m[6] = -forward[1];m[10] = -forward[2];
    m[12] = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
    m[13] = -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]);
    m[14] = forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2];
}

static void mat_rotate_y(float *m, float radians)
{
    mat_identity(m);
    m[0] = cosf(radians);
    m[2] = -sinf(radians);
    m[8] = sinf(radians);
    m[10] = cosf(radians);
}

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
        diag_logf("shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

Scene *scene_create(void)
{
    GLuint vertex = compile(GL_VERTEX_SHADER, VERTEX_SRC);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    if (!vertex || !fragment)
        return NULL;

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    /* Shaders are reference-counted by the program once attached. */
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = { 0 };
        glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
        diag_logf("program link failed: %s", log);
        glDeleteProgram(program);
        return NULL;
    }

    Scene *scene = calloc(1, sizeof(*scene));
    if (!scene) {
        glDeleteProgram(program);
        return NULL;
    }

    scene->program = program;
    scene->a_pos = glGetAttribLocation(program, "a_pos");
    scene->a_normal = glGetAttribLocation(program, "a_normal");
    scene->u_mvp = glGetUniformLocation(program, "u_mvp");
    scene->u_model = glGetUniformLocation(program, "u_model");
    scene->u_color = glGetUniformLocation(program, "u_color");

    glGenBuffers(1, &scene->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE), CUBE, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    diag_logf("scene ready (GL_VERSION %s)", (const char *)glGetString(GL_VERSION));
    return scene;
}

void scene_destroy(Scene *scene)
{
    if (!scene)
        return;

    if (scene->vbo)
        glDeleteBuffers(1, &scene->vbo);
    if (scene->program)
        glDeleteProgram(scene->program);

    free(scene);
}

void scene_camera_reset(SceneCamera *camera)
{
    camera->yaw = 0.6f;
    camera->pitch = 0.35f;
    camera->distance = 6.0f;
    camera->target[0] = camera->target[1] = camera->target[2] = 0.0f;
}

void scene_camera_orbit(SceneCamera *camera, float dyaw, float dpitch, float dzoom)
{
    camera->yaw += dyaw;
    camera->pitch += dpitch;

    /* Stop short of the poles, where the up vector degenerates. */
    const float limit = PI_F * 0.49f;
    if (camera->pitch > limit)
        camera->pitch = limit;
    if (camera->pitch < -limit)
        camera->pitch = -limit;

    camera->distance += dzoom;
    if (camera->distance < 2.0f)
        camera->distance = 2.0f;
    if (camera->distance > 24.0f)
        camera->distance = 24.0f;
}

void scene_draw(Scene *scene, Render *render, const SceneCamera *camera,
                SDL_Rect viewport, float seconds)
{
    if (!scene)
        return;

    /* Let SDL finish its own batch before touching raw GL state. */
    SDL_RenderFlush(render->renderer);

    int output_h = 0;
    SDL_GetRendererOutputSize(render->renderer, NULL, &output_h);

    /* GL's origin is bottom-left; SDL works top-down. */
    glViewport(viewport.x, output_h - viewport.y - viewport.h, viewport.w, viewport.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.x, output_h - viewport.y - viewport.h, viewport.w, viewport.h);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

    float eye[3] = {
        camera->target[0] + camera->distance * cosf(camera->pitch) * sinf(camera->yaw),
        camera->target[1] + camera->distance * sinf(camera->pitch),
        camera->target[2] + camera->distance * cosf(camera->pitch) * cosf(camera->yaw),
    };

    float projection[16];
    float view[16];
    float model[16];
    float mvp[16];

    mat_perspective(projection, 50.0f * PI_F / 180.0f,
                    (float)viewport.w / (float)viewport.h, 0.1f, 100.0f);
    mat_look_at(view, eye, camera->target);
    mat_rotate_y(model, seconds * 0.6f);

    mat_multiply(mvp, projection, view);
    mat_multiply(mvp, mvp, model);

    glUseProgram(scene->program);
    glUniformMatrix4fv(scene->u_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(scene->u_model, 1, GL_FALSE, model);
    glUniform3f(scene->u_color, 0.35f, 0.66f, 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glEnableVertexAttribArray((GLuint)scene->a_pos);
    glVertexAttribPointer((GLuint)scene->a_pos, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray((GLuint)scene->a_normal);
    glVertexAttribPointer((GLuint)scene->a_normal, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));

    glDrawArrays(GL_TRIANGLES, 0, sizeof(CUBE) / (6 * sizeof(GLfloat)));

    /* Hand the context back in the shape SDL expects to find it. */
    glDisableVertexAttribArray((GLuint)scene->a_pos);
    glDisableVertexAttribArray((GLuint)scene->a_normal);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
}
