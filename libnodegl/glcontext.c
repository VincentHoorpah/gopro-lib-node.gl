/*
 * Copyright 2016 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "bstr.h"
#include "glcontext.h"
#include "log.h"
#include "nodegl.h"
#include "utils.h"
#include "glincludes.h"

#include "gldefinitions_data.h"

#ifdef HAVE_PLATFORM_GLX
extern const struct glcontext_class ngli_glcontext_x11_class;
#endif

#ifdef HAVE_PLATFORM_EGL
extern const struct glcontext_class ngli_glcontext_egl_class;
#endif

#ifdef HAVE_PLATFORM_CGL
extern const struct glcontext_class ngli_glcontext_cgl_class;
#endif

#ifdef HAVE_PLATFORM_EAGL
extern const struct glcontext_class ngli_glcontext_eagl_class;
#endif

#ifdef HAVE_PLATFORM_WGL
extern const struct glcontext_class ngli_glcontext_wgl_class;
#endif

static const struct glcontext_class *glcontext_class_map[] = {
#ifdef HAVE_PLATFORM_GLX
    [NGL_GLPLATFORM_GLX] = &ngli_glcontext_x11_class,
#endif
#ifdef HAVE_PLATFORM_EGL
    [NGL_GLPLATFORM_EGL] = &ngli_glcontext_egl_class,
#endif
#ifdef HAVE_PLATFORM_CGL
    [NGL_GLPLATFORM_CGL] = &ngli_glcontext_cgl_class,
#endif
#ifdef HAVE_PLATFORM_EAGL
    [NGL_GLPLATFORM_EAGL] = &ngli_glcontext_eagl_class,
#endif
#ifdef HAVE_PLATFORM_WGL
    [NGL_GLPLATFORM_WGL] = &ngli_glcontext_wgl_class,
#endif
};

static struct glcontext *glcontext_new(void *display, void *window, void *handle, int platform, int api)
{
    struct glcontext *glcontext = NULL;

    if (platform < 0 || platform >= NGLI_ARRAY_NB(glcontext_class_map))
        return NULL;

    glcontext = calloc(1, sizeof(*glcontext));
    if (!glcontext)
        return NULL;

    glcontext->class = glcontext_class_map[platform];
    if (glcontext->class->priv_size) {
        glcontext->priv_data = calloc(1, glcontext->class->priv_size);
        if (!glcontext->priv_data) {
            goto fail;
        }
    }

    glcontext->platform = platform;
    glcontext->api = api;

    if (glcontext->class->init) {
        int ret = glcontext->class->init(glcontext, display, window, handle);
        if (ret < 0)
            goto fail;
    }

    return glcontext;
fail:
    ngli_glcontext_freep(&glcontext);
    return NULL;
}

struct glcontext *ngli_glcontext_new_wrapped(void *display, void *window, void *handle, int platform, int api)
{
    struct glcontext *glcontext;

    if (platform == NGL_GLPLATFORM_AUTO) {
#if defined(TARGET_LINUX)
        platform = NGL_GLPLATFORM_GLX;
#elif defined(TARGET_IPHONE)
        platform = NGL_GLPLATFORM_EAGL;
#elif defined(TARGET_DARWIN)
        platform = NGL_GLPLATFORM_CGL;
#elif defined(TARGET_ANDROID)
        platform = NGL_GLPLATFORM_EGL;
#elif defined(TARGET_MINGW_W64)
        platform = NGL_GLPLATFORM_WGL;
#else
        LOG(ERROR, "Can not determine which GL platform to use");
        return NULL;
#endif
    }

    if (api == NGL_GLAPI_AUTO) {
#if defined(TARGET_IPHONE) || defined(TARGET_ANDROID)
        api = NGL_GLAPI_OPENGLES2;
#else
        api = NGL_GLAPI_OPENGL3;
#endif
    }

    glcontext = glcontext_new(display, window, handle, platform, api);
    if (!glcontext)
        return NULL;

    glcontext->wrapped = 1;

    return glcontext;
}

struct glcontext *ngli_glcontext_new_shared(struct glcontext *other)
{
    struct glcontext *glcontext;

    if (!other)
        return NULL;

    void *display = other->class->get_display(other);
    void *window  = other->class->get_window(other);
    void *handle  = other->class->get_handle(other);

    glcontext = glcontext_new(display, window, handle, other->platform, other->api);

    if (glcontext->class->create) {
        int ret = glcontext->class->create(glcontext, other);
        if (ret < 0)
            ngli_glcontext_freep(&glcontext);
    }

    return glcontext;
}

static int glcontext_load_functions(struct glcontext *glcontext)
{
    const struct glfunctions *gl = &glcontext->funcs;

    for (int i = 0; i < NGLI_ARRAY_NB(gldefinitions); i++) {
        void *func;
        const struct gldefinition *gldefinition = &gldefinitions[i];

        func = ngli_glcontext_get_proc_address(glcontext, gldefinition->name);
        if ((gldefinition->flags & M) && !func) {
            LOG(ERROR, "could not find core function: %s", gldefinition->name);
            return -1;
        }

        *(void **)((uint8_t *)gl + gldefinition->offset) = func;
    }

    return 0;
}

static int glcontext_probe_version(struct glcontext *glcontext)
{
    const struct glfunctions *gl = &glcontext->funcs;

    if (glcontext->api == NGL_GLAPI_OPENGL3) {
        ngli_glGetIntegerv(gl, GL_MAJOR_VERSION, &glcontext->major_version);
        ngli_glGetIntegerv(gl, GL_MINOR_VERSION, &glcontext->minor_version);

        if (glcontext->major_version < 3) {
            LOG(ERROR, "node.gl only supports OpenGL >= 3.0");
            return -1;
        }
    } else if (glcontext->api == NGL_GLAPI_OPENGLES2) {
        glcontext->es = 1;

        const char *gl_version = (const char *)ngli_glGetString(gl, GL_VERSION);
        if (!gl_version) {
            LOG(ERROR, "could not get OpenGL ES version");
            return -1;
        }

        int ret = sscanf(gl_version,
                         "OpenGL ES %d.%d",
                         &glcontext->major_version,
                         &glcontext->minor_version);
        if (ret != 2) {
            LOG(ERROR, "could not parse OpenGL ES version (%s)", gl_version);
            return -1;
        }

        if (glcontext->major_version < 2) {
            LOG(ERROR, "node.gl only supports OpenGL ES >= 2.0");
            return -1;
        }
    } else {
        ngli_assert(0);
    }

    LOG(INFO, "OpenGL%s%d.%d",
        glcontext->api == NGL_GLAPI_OPENGLES2 ? " ES " : " ",
        glcontext->major_version,
        glcontext->minor_version);

    return 0;
}

#define OFFSET(x) offsetof(struct glfunctions, x)
static const struct glfeature {
    const char *name;
    int flag;
    size_t offset;
    int8_t maj_version;
    int8_t min_version;
    int8_t maj_es_version;
    int8_t min_es_version;
    const char **extensions;
    const char **es_extensions;
    const size_t *funcs_offsets;
} glfeatures[] = {
    {
        .name           = "vertex_array_object",
        .flag           = NGLI_FEATURE_VERTEX_ARRAY_OBJECT,
        .maj_version    = 3,
        .min_version    = 0,
        .maj_es_version = 3,
        .min_es_version = 0,
        .extensions     = (const char*[]){"GL_ARB_vertex_array_object", NULL},
        .es_extensions  = (const char*[]){"GL_OES_vertex_array_object", NULL},
        .funcs_offsets  = (const size_t[]){OFFSET(GenVertexArrays),
                                           OFFSET(BindVertexArray),
                                           OFFSET(DeleteVertexArrays),
                                           -1}
    }, {
        .name           = "texture3d",
        .flag           = NGLI_FEATURE_TEXTURE_3D,
        .maj_version    = 2,
        .min_version    = 0,
        .maj_es_version = 3,
        .min_es_version = 0,
        .funcs_offsets  = (const size_t[]){OFFSET(TexImage3D),
                                           OFFSET(TexSubImage3D),
                                           -1}
    }, {
        .name           = "texture_storage",
        .flag           = NGLI_FEATURE_TEXTURE_STORAGE,
        .maj_version    = 4,
        .min_version    = 2,
        .maj_es_version = 3,
        .min_es_version = 1,
        .funcs_offsets  = (const size_t[]){OFFSET(TexStorage2D),
                                           OFFSET(TexStorage3D),
                                           -1}
    }, {
        .name           = "compute_shader",
        .flag           = NGLI_FEATURE_COMPUTE_SHADER,
        .maj_version    = 4,
        .min_version    = 3,
        .maj_es_version = 3,
        .min_es_version = 1,
        .extensions     = (const char*[]){"GL_ARB_compute_shader", NULL},
        .funcs_offsets  = (const size_t[]){OFFSET(DispatchCompute),
                                           OFFSET(MemoryBarrier),
                                           -1}
    }, {
        .name           = "program_interface_query",
        .flag           = NGLI_FEATURE_PROGRAM_INTERFACE_QUERY,
        .maj_version    = 4,
        .min_version    = 3,
        .maj_es_version = 3,
        .min_es_version = 1,
        .extensions     = (const char*[]){"GL_ARB_program_interface_query", NULL},
        .funcs_offsets  = (const size_t[]){OFFSET(GetProgramResourceIndex),
                                           OFFSET(GetProgramResourceiv),
                                           OFFSET(GetProgramResourceLocation),
                                           -1}
    }, {
        .name           = "shader_image_load_store",
        .flag           = NGLI_FEATURE_SHADER_IMAGE_LOAD_STORE,
        .maj_version    = 4,
        .min_version    = 2,
        .maj_es_version = 3,
        .min_es_version = 1,
        .extensions     = (const char*[]){"GL_ARB_shader_image_load_store", NULL},
        .funcs_offsets  = (const size_t[]){OFFSET(BindImageTexture),
                                           -1}
    }, {
        .name           = "shader_storage_buffer_object",
        .flag           = NGLI_FEATURE_SHADER_STORAGE_BUFFER_OBJECT,
        .maj_version    = 4,
        .min_version    = 3,
        .maj_es_version = 3,
        .min_es_version = 1,
        .extensions     = (const char*[]){"GL_ARB_shader_storage_buffer_object", NULL},
        .funcs_offsets  = (const size_t[]){OFFSET(TexStorage2D),
                                           OFFSET(TexStorage3D),
                                           -1}
    },
};

static int glcontext_check_extension(const char *extension,
                                     const struct glfunctions *gl)
{
    GLint nb_extensions;
    ngli_glGetIntegerv(gl, GL_NUM_EXTENSIONS, &nb_extensions);

    for (GLint i = 0; i < nb_extensions; i++) {
        const char *tmp = (const char *)ngli_glGetStringi(gl, GL_EXTENSIONS, i);
        if (!tmp)
            break;
        if (!strcmp(extension, tmp))
            return 1;
    }

    return 0;
}

static int glcontext_check_extensions(struct glcontext *glcontext,
                                      const char **extensions)
{
    const struct glfunctions *gl = &glcontext->funcs;

    if (!extensions || !*extensions)
        return 0;

    if (glcontext->es) {
        const char *gl_extensions = (const char *)ngli_glGetString(gl, GL_EXTENSIONS);
        while (*extensions) {
            if (!ngli_glcontext_check_extension(*extensions, gl_extensions))
                return 0;

            extensions++;
        }
    } else {
        while (*extensions) {
            if (!glcontext_check_extension(*extensions, gl))
                return 0;

            extensions++;
        }
    }

    return 1;
}

static int glcontext_check_functions(struct glcontext *glcontext,
                                     const size_t *funcs_offsets)
{
    const struct glfunctions *gl = &glcontext->funcs;

    if (!funcs_offsets)
        return 1;

    while (*funcs_offsets != -1) {
        void *func_ptr = *(void **)((uint8_t *)gl + *funcs_offsets);
        if (!func_ptr)
            return 0;
        funcs_offsets++;
    }

    return 1;
}

static int glcontext_probe_extensions(struct glcontext *glcontext)
{
    const int es = glcontext->es;
    struct bstr *features_str = ngli_bstr_create();

    if (!features_str)
        return -1;

    for (int i = 0; i < NGLI_ARRAY_NB(glfeatures); i++) {
        const struct glfeature *glfeature = &glfeatures[i];

        int maj_version = es ? glfeature->maj_es_version : glfeature->maj_version;
        int min_version = es ? glfeature->min_es_version : glfeature->min_version;

        if (!(glcontext->major_version >= maj_version &&
              glcontext->minor_version >= min_version)) {
            const char **extensions = es ? glfeature->es_extensions : glfeature->extensions;
            if (!glcontext_check_extensions(glcontext, extensions))
                continue;
        }

        if (!glcontext_check_functions(glcontext, glfeature->funcs_offsets))
            continue;

        ngli_bstr_print(features_str, " %s", glfeature->name);
        glcontext->features |= glfeature->flag;
    }

    LOG(INFO, "OpenGL%s features:%s", es ? " ES" : "", ngli_bstr_strptr(features_str));
    ngli_bstr_freep(&features_str);

    return 0;
}

static int glcontext_probe_settings(struct glcontext *glcontext)
{
    const int es = glcontext->es;
    const struct glfunctions *gl = &glcontext->funcs;

    if (es && glcontext->major_version == 2 && glcontext->minor_version == 0) {
        glcontext->gl_1comp = GL_LUMINANCE;
        glcontext->gl_2comp = GL_LUMINANCE_ALPHA;
    } else {
        glcontext->gl_1comp = GL_RED;
        glcontext->gl_2comp = GL_RG;
    }

    ngli_glGetIntegerv(gl, GL_MAX_TEXTURE_IMAGE_UNITS, &glcontext->max_texture_image_units);

    if (glcontext->features & NGLI_FEATURE_COMPUTE_SHADER) {
        for (int i = 0; i < NGLI_ARRAY_NB(glcontext->max_compute_work_group_counts); i++) {
            ngli_glGetIntegeri_v(gl, GL_MAX_COMPUTE_WORK_GROUP_COUNT,
                                 i, &glcontext->max_compute_work_group_counts[i]);
        }
    }

    return 0;
}

int ngli_glcontext_load_extensions(struct glcontext *glcontext)
{
    if (glcontext->loaded)
        return 0;

    int ret = glcontext_load_functions(glcontext);
    if (ret < 0)
        return ret;

    ret = glcontext_probe_version(glcontext);
    if (ret < 0)
        return ret;

    ret = glcontext_probe_extensions(glcontext);
    if (ret < 0)
        return ret;

    ret = glcontext_probe_settings(glcontext);
    if (ret < 0)
        return ret;

    glcontext->loaded = 1;

    return 0;
}

int ngli_glcontext_make_current(struct glcontext *glcontext, int current)
{
    if (glcontext->class->make_current)
        return glcontext->class->make_current(glcontext, current);

    return 0;
}

void ngli_glcontext_swap_buffers(struct glcontext *glcontext)
{
    if (glcontext->class->swap_buffers)
        glcontext->class->swap_buffers(glcontext);
}

void ngli_glcontext_freep(struct glcontext **glcontextp)
{
    struct glcontext *glcontext;

    if (!glcontextp || !*glcontextp)
        return;

    glcontext = *glcontextp;

    if (glcontext->class->uninit)
        glcontext->class->uninit(glcontext);

    free(glcontext->priv_data);
    free(glcontext);

    *glcontextp = NULL;
}

void *ngli_glcontext_get_proc_address(struct glcontext *glcontext, const char *name)
{
    void *ptr = NULL;

    if (glcontext->class->get_proc_address)
        ptr = glcontext->class->get_proc_address(glcontext, name);

    return ptr;
}

void *ngli_glcontext_get_handle(struct glcontext *glcontext)
{
    void *handle = NULL;

    if (glcontext->class->get_handle)
        handle = glcontext->class->get_handle(glcontext);

    return handle;
}

void *ngli_glcontext_get_texture_cache(struct glcontext *glcontext)
{
    void *texture_cache = NULL;

    if (glcontext->class->get_texture_cache)
        texture_cache = glcontext->class->get_texture_cache(glcontext);

    return texture_cache;
}

int ngli_glcontext_check_extension(const char *extension, const char *extensions)
{
    if (!extension || !extensions)
        return 0;

    size_t len = strlen(extension);
    const char *cur = extensions;
    const char *end = extensions + strlen(extensions);

    while (cur < end) {
        cur = strstr(extensions, extension);
        if (!cur)
            break;

        cur += len;
        if (cur[0] == ' ' || cur[0] == '\0')
            return 1;
    }

    return 0;
}

int ngli_glcontext_check_gl_error(struct glcontext *glcontext)
{
    const struct glfunctions *gl = &glcontext->funcs;

    const GLenum error = ngli_glGetError(gl);
    const char *errorstr = NULL;

    if (!error)
        return error;

    switch (error) {
    case GL_INVALID_ENUM:
        errorstr = "GL_INVALID_ENUM";
        break;
    case GL_INVALID_VALUE:
        errorstr = "GL_INVALID_VALUE";
        break;
    case GL_INVALID_OPERATION:
        errorstr = "GL_INVALID_OPERATION";
        break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        errorstr = "GL_INVALID_FRAMEBUFFER_OPERATION";
        break;
    case GL_OUT_OF_MEMORY:
        errorstr = "GL_OUT_OF_MEMORY";
        break;
    }

    if (errorstr)
        LOG(ERROR, "GL error: %s", errorstr);
    else
        LOG(ERROR, "GL error: %04x", error);

    return error;
}
