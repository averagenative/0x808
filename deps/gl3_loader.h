/*
 * gl3_loader.h — Minimal OpenGL 3.3 Core function loader for Windows.
 *
 * On Windows, opengl32.dll only exports GL 1.1 functions. GL 2.0+ functions
 * must be loaded at runtime via SDL_GL_GetProcAddress.
 *
 * This header suppresses SDL_opengl_glext.h (via NO_SDL_GLEXT), defines the
 * GL constants and types we need, and provides function pointers loaded at
 * runtime.
 *
 * Usage:
 *   1. Include this header BEFORE nuklear.h / nuklear_sdl_gl3.h
 *   2. Call gl3_loader_init() AFTER creating the GL context
 *
 * On non-Windows platforms this is a no-op (just sets GL_GLEXT_PROTOTYPES).
 */

#ifndef GL3_LOADER_H
#define GL3_LOADER_H

#ifdef _WIN32

/* Prevent SDL_opengl.h from including the massive glext header */
#define NO_SDL_GLEXT

/* Redirect GL 1.2+ functions declared in SDL_opengl.h so they don't
 * conflict with our function pointer variables of the same name. */
#define glActiveTexture  _gl3_sdl_glActiveTexture
#define glBlendEquation  _gl3_sdl_glBlendEquation

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

/* Undo redirects — our function pointer globals will use the real names */
#undef glActiveTexture
#undef glBlendEquation

/* ── GL types not in base gl.h ─────────────────────────────────────────── */

typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

/* ── GL constants ──────────────────────────────────────────────────────── */

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER         0x8893
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW                  0x88E0
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY                   0x88B9
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER              0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS               0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                  0x8B82
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD                     0x8006
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                     0x84C0
#endif

/* ── Function pointer types ────────────────────────────────────────────── */

typedef GLuint    (APIENTRY *PFN_glCreateProgram)(void);
typedef GLuint    (APIENTRY *PFN_glCreateShader)(GLenum type);
typedef void      (APIENTRY *PFN_glShaderSource)(GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
typedef void      (APIENTRY *PFN_glCompileShader)(GLuint shader);
typedef void      (APIENTRY *PFN_glGetShaderiv)(GLuint shader, GLenum pname, GLint *params);
typedef void      (APIENTRY *PFN_glAttachShader)(GLuint program, GLuint shader);
typedef void      (APIENTRY *PFN_glLinkProgram)(GLuint program);
typedef void      (APIENTRY *PFN_glGetProgramiv)(GLuint program, GLenum pname, GLint *params);
typedef GLint     (APIENTRY *PFN_glGetUniformLocation)(GLuint program, const GLchar *name);
typedef GLint     (APIENTRY *PFN_glGetAttribLocation)(GLuint program, const GLchar *name);
typedef void      (APIENTRY *PFN_glDetachShader)(GLuint program, GLuint shader);
typedef void      (APIENTRY *PFN_glDeleteShader)(GLuint shader);
typedef void      (APIENTRY *PFN_glDeleteProgram)(GLuint program);
typedef void      (APIENTRY *PFN_glGenBuffers)(GLsizei n, GLuint *buffers);
typedef void      (APIENTRY *PFN_glDeleteBuffers)(GLsizei n, const GLuint *buffers);
typedef void      (APIENTRY *PFN_glBindBuffer)(GLenum target, GLuint buffer);
typedef void      (APIENTRY *PFN_glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void*     (APIENTRY *PFN_glMapBuffer)(GLenum target, GLenum access);
typedef GLboolean (APIENTRY *PFN_glUnmapBuffer)(GLenum target);
typedef void      (APIENTRY *PFN_glGenVertexArrays)(GLsizei n, GLuint *arrays);
typedef void      (APIENTRY *PFN_glDeleteVertexArrays)(GLsizei n, const GLuint *arrays);
typedef void      (APIENTRY *PFN_glBindVertexArray)(GLuint array);
typedef void      (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint index);
typedef void      (APIENTRY *PFN_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void      (APIENTRY *PFN_glUseProgram)(GLuint program);
typedef void      (APIENTRY *PFN_glUniform1i)(GLint location, GLint v0);
typedef void      (APIENTRY *PFN_glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void      (APIENTRY *PFN_glActiveTexture)(GLenum texture);
typedef void      (APIENTRY *PFN_glBlendEquation)(GLenum mode);

/* ── Function pointer globals ──────────────────────────────────────────── */

#ifdef GL3_LOADER_IMPLEMENTATION
#define GL3_EXTERN
#else
#define GL3_EXTERN extern
#endif

GL3_EXTERN PFN_glCreateProgram           glCreateProgram;
GL3_EXTERN PFN_glCreateShader            glCreateShader;
GL3_EXTERN PFN_glShaderSource            glShaderSource;
GL3_EXTERN PFN_glCompileShader           glCompileShader;
GL3_EXTERN PFN_glGetShaderiv             glGetShaderiv;
GL3_EXTERN PFN_glAttachShader            glAttachShader;
GL3_EXTERN PFN_glLinkProgram             glLinkProgram;
GL3_EXTERN PFN_glGetProgramiv            glGetProgramiv;
GL3_EXTERN PFN_glGetUniformLocation      glGetUniformLocation;
GL3_EXTERN PFN_glGetAttribLocation       glGetAttribLocation;
GL3_EXTERN PFN_glDetachShader            glDetachShader;
GL3_EXTERN PFN_glDeleteShader            glDeleteShader;
GL3_EXTERN PFN_glDeleteProgram           glDeleteProgram;
GL3_EXTERN PFN_glGenBuffers              glGenBuffers;
GL3_EXTERN PFN_glDeleteBuffers           glDeleteBuffers;
GL3_EXTERN PFN_glBindBuffer              glBindBuffer;
GL3_EXTERN PFN_glBufferData              glBufferData;
GL3_EXTERN PFN_glMapBuffer               glMapBuffer;
GL3_EXTERN PFN_glUnmapBuffer             glUnmapBuffer;
GL3_EXTERN PFN_glGenVertexArrays         glGenVertexArrays;
GL3_EXTERN PFN_glDeleteVertexArrays      glDeleteVertexArrays;
GL3_EXTERN PFN_glBindVertexArray         glBindVertexArray;
GL3_EXTERN PFN_glEnableVertexAttribArray glEnableVertexAttribArray;
GL3_EXTERN PFN_glVertexAttribPointer     glVertexAttribPointer;
GL3_EXTERN PFN_glUseProgram              glUseProgram;
GL3_EXTERN PFN_glUniform1i               glUniform1i;
GL3_EXTERN PFN_glUniformMatrix4fv        glUniformMatrix4fv;
GL3_EXTERN PFN_glActiveTexture           glActiveTexture;
GL3_EXTERN PFN_glBlendEquation           glBlendEquation;

#undef GL3_EXTERN

/* ── Loader function ───────────────────────────────────────────────────── */

#ifdef GL3_LOADER_IMPLEMENTATION

#define GL3_LOAD(name) \
    name = (PFN_##name)SDL_GL_GetProcAddress(#name)

static void gl3_loader_init(void)
{
    GL3_LOAD(glCreateProgram);
    GL3_LOAD(glCreateShader);
    GL3_LOAD(glShaderSource);
    GL3_LOAD(glCompileShader);
    GL3_LOAD(glGetShaderiv);
    GL3_LOAD(glAttachShader);
    GL3_LOAD(glLinkProgram);
    GL3_LOAD(glGetProgramiv);
    GL3_LOAD(glGetUniformLocation);
    GL3_LOAD(glGetAttribLocation);
    GL3_LOAD(glDetachShader);
    GL3_LOAD(glDeleteShader);
    GL3_LOAD(glDeleteProgram);
    GL3_LOAD(glGenBuffers);
    GL3_LOAD(glDeleteBuffers);
    GL3_LOAD(glBindBuffer);
    GL3_LOAD(glBufferData);
    GL3_LOAD(glMapBuffer);
    GL3_LOAD(glUnmapBuffer);
    GL3_LOAD(glGenVertexArrays);
    GL3_LOAD(glDeleteVertexArrays);
    GL3_LOAD(glBindVertexArray);
    GL3_LOAD(glEnableVertexAttribArray);
    GL3_LOAD(glVertexAttribPointer);
    GL3_LOAD(glUseProgram);
    GL3_LOAD(glUniform1i);
    GL3_LOAD(glUniformMatrix4fv);
    GL3_LOAD(glActiveTexture);
    GL3_LOAD(glBlendEquation);
}

#undef GL3_LOAD

#else

static inline void gl3_loader_init(void) {}

#endif /* GL3_LOADER_IMPLEMENTATION */

#else /* !_WIN32 */

/* On Linux/macOS, GL functions are available via system headers */
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>

static inline void gl3_loader_init(void) {}

#endif /* _WIN32 */

#endif /* GL3_LOADER_H */
