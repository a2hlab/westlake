// Android's libGLESv1_CM exports a "...Bounds" variant of each GLES 1 pointer call that also takes
// the array's element count, and AOSP's GL10/GLES11 bindings call those. No real driver has them,
// and Android's own implementation ignores the count and calls the plain function; so do we.
#include <GLES/gl.h>
#include <GLES/glext.h>

GL_API void GL_APIENTRY glColorPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glColorPointer(size, type, stride, ptr); }
GL_API void GL_APIENTRY glNormalPointerBounds(GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glNormalPointer(type, stride, ptr); }
GL_API void GL_APIENTRY glTexCoordPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glTexCoordPointer(size, type, stride, ptr); }
GL_API void GL_APIENTRY glVertexPointerBounds(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glVertexPointer(size, type, stride, ptr); }
GL_API void GL_APIENTRY glPointSizePointerOESBounds(GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glPointSizePointerOES(type, stride, ptr); }
GL_API void GL_APIENTRY glMatrixIndexPointerOESBounds(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glMatrixIndexPointerOES(size, type, stride, ptr); }
GL_API void GL_APIENTRY glWeightPointerOESBounds(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr, GLsizei count)
{ (void) count; glWeightPointerOES(size, type, stride, ptr); }
