#pragma once

#include <stddef.h>

#define MAX_SECTION_LENGTH (256)

#define SEPERATOR (char)('/')

// copy the (null-terminated) path into buffer. null-terminate
// the buffered path. the assumed buffer size is 256 bytes.
void pathn_buffer(char *buffer, const char *path);

// copy n bytes of path into buffer. null-terminate
// the buffered path. the assumed buffer size is 256 bytes.
void pathn_buffer_n(char *buffer, const char *path, size_t n);

// return if the given path starts with a leading '/'
int path_is_root(const char *path);

// return pointer to buffer with subsection copied into. advance path_remaining to the
// beginning of the next section, if last, NULL
char *copy_first_section(const char *pathname, char **path_remaining, char *buffer);