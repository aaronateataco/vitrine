#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>
#include <SDL2/SDL_image.h>

#include "diag.h"
#include "scene.h"

#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf.h"

/* Guards against a pathological model exhausting the heap mid-session. */
#define SCENE_MAX_VERTICES (600 * 1000)

#define PI_F 3.14159265358979f

#define COIN_SEGMENTS 48

struct Scene {
    GLuint medal_program;
    GLuint coin_vbo;
    GLsizei coin_face_vertices;   ///< Front+back, drawn textured.
    GLsizei coin_rim_vertices;    ///< Rim strip, drawn as metal.
    GLuint ribbon_vbo;
    GLsizei ribbon_vertices;
    GLint  m_pos, m_normal, m_uv;
    GLint  m_mvp, m_model, m_tex, m_eye, m_mode, m_tint;

    GLuint program;
    GLuint vbo;
    GLuint model_vbo;
    GLsizei model_vertices;
    float  model_scale;
    float  model_centre[3];
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

static bool setup_medal(Scene *scene);

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

    /* Trophies are optional: if the medal program fails the room still opens
       with the placeholder, rather than taking 3D down entirely. */
    if (!setup_medal(scene))
        diag_logf("medal pipeline unavailable; trophies will not render");

    diag_logf("scene ready (GL_VERSION %s)", (const char *)glGetString(GL_VERSION));
    return scene;
}

bool scene_has_model(const Scene *scene)
{
    return scene && scene->model_vertices > 0;
}

void scene_clear_model(Scene *scene)
{
    if (!scene || !scene->model_vbo)
        return;

    glDeleteBuffers(1, &scene->model_vbo);
    scene->model_vbo = 0;
    scene->model_vertices = 0;
}

/* Appends one primitive's triangles as interleaved position + normal. */
static bool append_primitive(const cgltf_primitive *primitive, float **out,
                             size_t *count, size_t *capacity)
{
    if (primitive->type != cgltf_primitive_type_triangles)
        return true;   /* Skip lines and points rather than failing the load. */

    const cgltf_accessor *positions = NULL;
    const cgltf_accessor *normals = NULL;

    for (cgltf_size i = 0; i < primitive->attributes_count; i++) {
        if (primitive->attributes[i].type == cgltf_attribute_type_position)
            positions = primitive->attributes[i].data;
        else if (primitive->attributes[i].type == cgltf_attribute_type_normal)
            normals = primitive->attributes[i].data;
    }

    if (!positions)
        return true;

    cgltf_size vertex_count = positions->count;
    float *position_data = calloc(vertex_count * 3, sizeof(float));
    float *normal_data = calloc(vertex_count * 3, sizeof(float));
    if (!position_data || !normal_data) {
        free(position_data);
        free(normal_data);
        return false;
    }

    cgltf_accessor_unpack_floats(positions, position_data, vertex_count * 3);
    if (normals && normals->count == vertex_count)
        cgltf_accessor_unpack_floats(normals, normal_data, vertex_count * 3);
    else
        for (cgltf_size i = 0; i < vertex_count; i++)
            normal_data[i * 3 + 1] = 1.0f;   /* Flat-ish fallback. */

    cgltf_size index_count = primitive->indices ? primitive->indices->count
                                                : vertex_count;

    for (cgltf_size i = 0; i < index_count; i++) {
        cgltf_size index = primitive->indices
                               ? cgltf_accessor_read_index(primitive->indices, i)
                               : i;
        if (index >= vertex_count)
            continue;

        if (*count + 1 > SCENE_MAX_VERTICES)
            break;

        if (*count == *capacity) {
            size_t grown = *capacity ? *capacity * 2 : 4096;
            float *bigger = realloc(*out, grown * 6 * sizeof(float));
            if (!bigger) {
                free(position_data);
                free(normal_data);
                return false;
            }
            *out = bigger;
            *capacity = grown;
        }

        float *vertex = *out + (*count) * 6;
        memcpy(vertex, position_data + index * 3, 3 * sizeof(float));
        memcpy(vertex + 3, normal_data + index * 3, 3 * sizeof(float));
        (*count)++;
    }

    free(position_data);
    free(normal_data);
    return true;
}

bool scene_load_model(Scene *scene, const char *path)
{
    if (!scene || !path || !path[0])
        return false;

    cgltf_options options;
    memset(&options, 0, sizeof(options));

    cgltf_data *data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        diag_logf("model parse failed: %s", path);
        return false;
    }

    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        diag_logf("model buffers failed: %s", path);
        cgltf_free(data);
        return false;
    }

    float *vertices = NULL;
    size_t count = 0;
    size_t capacity = 0;
    bool ok = true;

    for (cgltf_size m = 0; ok && m < data->meshes_count; m++)
        for (cgltf_size p = 0; ok && p < data->meshes[m].primitives_count; p++)
            ok = append_primitive(&data->meshes[m].primitives[p], &vertices,
                                  &count, &capacity);

    cgltf_free(data);

    if (!ok || count < 3) {
        free(vertices);
        diag_logf("model yielded no geometry: %s", path);
        return false;
    }

    /* Normalise so a distance in franchises.json means the same thing for any
       model, whatever units it was authored in. */
    float min[3] = { vertices[0], vertices[1], vertices[2] };
    float max[3] = { vertices[0], vertices[1], vertices[2] };

    for (size_t i = 1; i < count; i++)
        for (int axis = 0; axis < 3; axis++) {
            float value = vertices[i * 6 + axis];
            if (value < min[axis]) min[axis] = value;
            if (value > max[axis]) max[axis] = value;
        }

    float extent = 0.0f;
    for (int axis = 0; axis < 3; axis++) {
        scene->model_centre[axis] = (min[axis] + max[axis]) * 0.5f;
        float span = max[axis] - min[axis];
        if (span > extent)
            extent = span;
    }

    scene->model_scale = extent > 1e-5f ? 2.0f / extent : 1.0f;

    scene_clear_model(scene);
    glGenBuffers(1, &scene->model_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene->model_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * 6 * sizeof(float)),
                 vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    scene->model_vertices = (GLsizei)count;
    free(vertices);

    diag_logf("model loaded: %s (%zu vertices, scale %.3f)", path, count,
              (double)scene->model_scale);
    return true;
}

void scene_destroy(Scene *scene)
{
    if (!scene)
        return;

    scene_clear_model(scene);

    if (scene->coin_vbo)
        glDeleteBuffers(1, &scene->coin_vbo);
    if (scene->ribbon_vbo)
        glDeleteBuffers(1, &scene->ribbon_vbo);
    if (scene->medal_program)
        glDeleteProgram(scene->medal_program);

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

    if (scene_has_model(scene)) {
        /* Centre and normalise before the spin, so it turns about its middle. */
        float fit[16];
        mat_identity(fit);
        fit[0] = fit[5] = fit[10] = scene->model_scale;
        fit[12] = -scene->model_centre[0] * scene->model_scale;
        fit[13] = -scene->model_centre[1] * scene->model_scale;
        fit[14] = -scene->model_centre[2] * scene->model_scale;
        mat_multiply(model, model, fit);
    }

    mat_multiply(mvp, projection, view);
    mat_multiply(mvp, mvp, model);

    glUseProgram(scene->program);
    glUniformMatrix4fv(scene->u_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(scene->u_model, 1, GL_FALSE, model);
    glUniform3f(scene->u_color, 0.35f, 0.66f, 1.0f);

    bool use_model = scene_has_model(scene);
    glBindBuffer(GL_ARRAY_BUFFER, use_model ? scene->model_vbo : scene->vbo);
    glEnableVertexAttribArray((GLuint)scene->a_pos);
    glVertexAttribPointer((GLuint)scene->a_pos, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray((GLuint)scene->a_normal);
    glVertexAttribPointer((GLuint)scene->a_normal, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));

    glDrawArrays(GL_TRIANGLES, 0,
                 use_model ? scene->model_vertices
                           : (GLsizei)(sizeof(CUBE) / (6 * sizeof(GLfloat))));

    /* Hand the context back in the shape SDL expects to find it. */
    glDisableVertexAttribArray((GLuint)scene->a_pos);
    glDisableVertexAttribArray((GLuint)scene->a_normal);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
}

/* ---- Trophy Room -------------------------------------------------------- */

static const char *MEDAL_VERTEX_SRC =
    "attribute vec3 a_pos;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "varying vec3 v_normal;\n"
    "varying vec2 v_uv;\n"
    "varying vec3 v_world;\n"
    "void main() {\n"
    "    v_normal = mat3(u_model[0].xyz, u_model[1].xyz, u_model[2].xyz) * a_normal;\n"
    "    v_uv = a_uv;\n"
    "    v_world = (u_model * vec4(a_pos, 1.0)).xyz;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *MEDAL_FRAGMENT_SRC =
    "precision mediump float;\n"
    "varying vec3 v_normal;\n"
    "varying vec2 v_uv;\n"
    "varying vec3 v_world;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_mode;\n"   /* 0 badge face, 1 metal rim, 2 fabric ribbon */
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 v = normalize(u_eye - v_world);\n"
    "    vec3 l = normalize(vec3(0.5, 0.9, 0.7));\n"
    "    vec3 h = normalize(l + v);\n"
    "    float d = max(dot(n, l), 0.0);\n"
    "    float spec = pow(max(dot(n, h), 0.0), 48.0);\n"
    "    vec3 base;\n"
    "    float metal;\n"
    "    if (u_mode < 0.5) {\n"
    "        base = texture2D(u_tex, v_uv).rgb;\n"
    "        metal = 0.35;\n"
    "    } else if (u_mode < 1.5) {\n"
    "        base = u_tint;\n"
    "        metal = 1.0;\n"
    "    } else {\n"
    /* Two crossed sine bands read as a woven fabric at this scale. */
    "        float warp = 0.5 + 0.5 * sin(v_uv.y * 140.0);\n"
    "        float weft = 0.5 + 0.5 * sin(v_uv.x * 70.0);\n"
    "        base = u_tint * (0.78 + 0.22 * warp * weft);\n"
    "        metal = 0.08;\n"
    "    }\n"
    "    gl_FragColor = vec4(base * (0.34 + 0.66 * d)\n"
    "                        + spec * metal * vec3(1.0, 0.94, 0.78), 1.0);\n"
    "}\n";

static void push_vertex(float *out, size_t *n, float px, float py, float pz,
                        float nx, float ny, float nz, float u, float v)
{
    float *dst = out + (*n) * 8;
    dst[0] = px; dst[1] = py; dst[2] = pz;
    dst[3] = nx; dst[4] = ny; dst[5] = nz;
    dst[6] = u;  dst[7] = v;
    (*n)++;
}

/* A coin: two textured faces plus a rim strip, generated once at startup. */
static bool build_coin(Scene *scene)
{
    const float radius = 1.0f;
    const float half = 0.11f;

    size_t max_vertices = (size_t)COIN_SEGMENTS * 12;
    float *data = calloc(max_vertices * 8, sizeof(float));
    if (!data)
        return false;

    size_t n = 0;

    for (int i = 0; i < COIN_SEGMENTS; i++) {
        float a0 = (float)i / COIN_SEGMENTS * 2.0f * PI_F;
        float a1 = (float)(i + 1) / COIN_SEGMENTS * 2.0f * PI_F;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        /* Front face, badge mapped across the disc. */
        push_vertex(data, &n, 0, 0, half, 0, 0, 1, 0.5f, 0.5f);
        push_vertex(data, &n, c0 * radius, s0 * radius, half, 0, 0, 1,
                    0.5f + 0.5f * c0, 0.5f - 0.5f * s0);
        push_vertex(data, &n, c1 * radius, s1 * radius, half, 0, 0, 1,
                    0.5f + 0.5f * c1, 0.5f - 0.5f * s1);

        /* Back face, wound the other way so it survives backface culling. */
        push_vertex(data, &n, 0, 0, -half, 0, 0, -1, 0.5f, 0.5f);
        push_vertex(data, &n, c1 * radius, s1 * radius, -half, 0, 0, -1,
                    0.5f - 0.5f * c1, 0.5f - 0.5f * s1);
        push_vertex(data, &n, c0 * radius, s0 * radius, -half, 0, 0, -1,
                    0.5f - 0.5f * c0, 0.5f - 0.5f * s0);
    }

    scene->coin_face_vertices = (GLsizei)n;

    for (int i = 0; i < COIN_SEGMENTS; i++) {
        float a0 = (float)i / COIN_SEGMENTS * 2.0f * PI_F;
        float a1 = (float)(i + 1) / COIN_SEGMENTS * 2.0f * PI_F;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        push_vertex(data, &n, c0 * radius, s0 * radius,  half, c0, s0, 0, 0, 0);
        push_vertex(data, &n, c0 * radius, s0 * radius, -half, c0, s0, 0, 0, 1);
        push_vertex(data, &n, c1 * radius, s1 * radius, -half, c1, s1, 0, 1, 1);

        push_vertex(data, &n, c0 * radius, s0 * radius,  half, c0, s0, 0, 0, 0);
        push_vertex(data, &n, c1 * radius, s1 * radius, -half, c1, s1, 0, 1, 1);
        push_vertex(data, &n, c1 * radius, s1 * radius,  half, c1, s1, 0, 1, 0);
    }

    scene->coin_rim_vertices = (GLsizei)n - scene->coin_face_vertices;

    glGenBuffers(1, &scene->coin_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene->coin_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 8 * sizeof(float)), data,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(data);
    return true;
}

/* A ribbon: a tapering strip that twists as it rises, attached above the coin. */
static bool build_ribbon(Scene *scene)
{
    enum { STEPS = 24 };

    float *data = calloc(STEPS * 6 * 8, sizeof(float));
    if (!data)
        return false;

    size_t n = 0;

    for (int i = 0; i < STEPS; i++) {
        float t0 = (float)i / STEPS;
        float t1 = (float)(i + 1) / STEPS;

        float y0 = 1.0f + t0 * 1.5f;
        float y1 = 1.0f + t1 * 1.5f;
        float w0 = 0.34f - 0.10f * t0;
        float w1 = 0.34f - 0.10f * t1;

        /* The twist is what stops it reading as a flat rectangle. */
        float a0 = t0 * 1.6f;
        float a1 = t1 * 1.6f;
        float x0 = sinf(a0) * 0.20f, z0 = cosf(a0) * 0.10f;
        float x1 = sinf(a1) * 0.20f, z1 = cosf(a1) * 0.10f;

        push_vertex(data, &n, x0 - w0, y0, z0, 0, 0, 1, 0.0f, t0);
        push_vertex(data, &n, x0 + w0, y0, z0, 0, 0, 1, 1.0f, t0);
        push_vertex(data, &n, x1 + w1, y1, z1, 0, 0, 1, 1.0f, t1);

        push_vertex(data, &n, x0 - w0, y0, z0, 0, 0, 1, 0.0f, t0);
        push_vertex(data, &n, x1 + w1, y1, z1, 0, 0, 1, 1.0f, t1);
        push_vertex(data, &n, x1 - w1, y1, z1, 0, 0, 1, 0.0f, t1);
    }

    scene->ribbon_vertices = (GLsizei)n;

    glGenBuffers(1, &scene->ribbon_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene->ribbon_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 8 * sizeof(float)), data,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(data);
    return true;
}

unsigned scene_load_texture(const char *path)
{
    SDL_Surface *raw = IMG_Load(path);
    if (!raw)
        return 0;

    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (!rgba)
        return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    SDL_FreeSurface(rgba);
    return texture;
}

void scene_free_texture(unsigned texture)
{
    if (texture)
        glDeleteTextures(1, (const GLuint *)&texture);
}

static void bind_medal_attributes(const Scene *scene, GLuint vbo)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray((GLuint)scene->m_pos);
    glVertexAttribPointer((GLuint)scene->m_pos, 3, GL_FLOAT, GL_FALSE,
                          8 * sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray((GLuint)scene->m_normal);
    glVertexAttribPointer((GLuint)scene->m_normal, 3, GL_FLOAT, GL_FALSE,
                          8 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)scene->m_uv);
    glVertexAttribPointer((GLuint)scene->m_uv, 2, GL_FLOAT, GL_FALSE,
                          8 * sizeof(GLfloat), (void *)(6 * sizeof(GLfloat)));
}

void scene_draw_medals(Scene *scene, Render *render, const SceneCamera *camera,
                       SDL_Rect viewport, float seconds,
                       const unsigned *textures, size_t count, size_t focus)
{
    if (!scene || !scene->medal_program || count == 0)
        return;

    SDL_RenderFlush(render->renderer);

    int output_h = 0;
    SDL_GetRendererOutputSize(render->renderer, NULL, &output_h);
    int gl_y = output_h - viewport.y - viewport.h;

    glViewport(viewport.x, gl_y, viewport.w, viewport.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.x, gl_y, viewport.w, viewport.h);
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
    mat_perspective(projection, 50.0f * PI_F / 180.0f,
                    (float)viewport.w / (float)viewport.h, 0.1f, 100.0f);
    mat_look_at(view, eye, camera->target);

    glUseProgram(scene->medal_program);
    glUniform3f(scene->m_eye, eye[0], eye[1], eye[2]);
    glUniform1i(scene->m_tex, 0);
    glActiveTexture(GL_TEXTURE0);

    const float spacing = 2.8f;
    float origin = -((float)count - 1.0f) * spacing * 0.5f;

    for (size_t i = 0; i < count; i++) {
        bool focused = (i == focus);

        float translate[16];
        mat_identity(translate);
        translate[12] = origin + (float)i * spacing;
        translate[13] = focused ? 0.25f : 0.0f;
        translate[14] = focused ? 0.8f : 0.0f;

        /* Only the focused medal spins; a wall of spinning coins is noise. */
        float spin[16];
        mat_rotate_y(spin, focused ? seconds * 0.9f : 0.35f);

        float model[16];
        float mvp[16];
        mat_multiply(model, translate, spin);
        mat_multiply(mvp, projection, view);
        mat_multiply(mvp, mvp, model);

        glUniformMatrix4fv(scene->m_mvp, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(scene->m_model, 1, GL_FALSE, model);

        /* Ribbon first: it sits behind the coin from most angles. */
        if (scene->ribbon_vbo) {
            bind_medal_attributes(scene, scene->ribbon_vbo);
            glUniform1f(scene->m_mode, 2.0f);
            glUniform3f(scene->m_tint, focused ? 0.72f : 0.48f, 0.12f, 0.18f);
            glDrawArrays(GL_TRIANGLES, 0, scene->ribbon_vertices);
        }

        bind_medal_attributes(scene, scene->coin_vbo);

        glBindTexture(GL_TEXTURE_2D, i < count ? (GLuint)textures[i] : 0);
        glUniform1f(scene->m_mode, textures[i] ? 0.0f : 1.0f);
        glUniform3f(scene->m_tint, 0.85f, 0.68f, 0.30f);
        glDrawArrays(GL_TRIANGLES, 0, scene->coin_face_vertices);

        glUniform1f(scene->m_mode, 1.0f);
        glUniform3f(scene->m_tint, 0.85f, 0.68f, 0.30f);
        glDrawArrays(GL_TRIANGLES, scene->coin_face_vertices,
                     scene->coin_rim_vertices);
    }

    glDisableVertexAttribArray((GLuint)scene->m_pos);
    glDisableVertexAttribArray((GLuint)scene->m_normal);
    glDisableVertexAttribArray((GLuint)scene->m_uv);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
}

static bool setup_medal(Scene *scene)
{
    GLuint vertex = compile(GL_VERTEX_SHADER, MEDAL_VERTEX_SRC);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, MEDAL_FRAGMENT_SRC);
    if (!vertex || !fragment)
        return false;

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = { 0 };
        glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
        diag_logf("medal program link failed: %s", log);
        glDeleteProgram(program);
        return false;
    }

    scene->medal_program = program;
    scene->m_pos = glGetAttribLocation(program, "a_pos");
    scene->m_normal = glGetAttribLocation(program, "a_normal");
    scene->m_uv = glGetAttribLocation(program, "a_uv");
    scene->m_mvp = glGetUniformLocation(program, "u_mvp");
    scene->m_model = glGetUniformLocation(program, "u_model");
    scene->m_tex = glGetUniformLocation(program, "u_tex");
    scene->m_eye = glGetUniformLocation(program, "u_eye");
    scene->m_mode = glGetUniformLocation(program, "u_mode");
    scene->m_tint = glGetUniformLocation(program, "u_tint");

    return build_coin(scene) && build_ribbon(scene);
}
