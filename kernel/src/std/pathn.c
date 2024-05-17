#include "pathn.h"
#include "string.h"
#include "macros.h"

inline int path_is_root(const char *path) {
    // . and .. handled by lookup
    return *path == '/';
}

inline void pathn_buffer(char *buffer, const char *path) {
    strncpy(buffer, path, 255);
    buffer += strlen(path);
    *buffer = '\0';
}

inline void pathn_buffer_n(char *buffer, const char *path, size_t n) {
    strncpy(buffer, path, MIN(n, 255));
    *(++buffer) = '\0';
}

char *copy_first_section(const char *pathname, char **path_remaining, char *buffer) {
    // skip leading seperators
    while (*pathname == SEPERATOR) {
        pathname++;
    }

    // pos of first seperator after this section
    const char *next_sep = strchr(pathname, SEPERATOR);

    if (next_sep == NULL) {
        *path_remaining = NULL;
        return strcpy(buffer, pathname);
    } else {
        // extract first section
        size_t length = next_sep - pathname;
        if (length > MAX_SECTION_LENGTH - 1)
            length = MAX_SECTION_LENGTH - 1;
        strncpy(buffer, pathname, length);
        buffer[length] = '\0';

        *path_remaining = (char *)(next_sep);

        return buffer;
    }
}