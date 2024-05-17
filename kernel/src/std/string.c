#include "string.h"

char *strcpy (char *dest, const char *src)
{
    char *_dest = dest;

    while (*src)
        *(_dest++) = *(src++);
    *_dest = '\0';

    return dest;
}

char *strncpy (char *dest, const char *src, size_t n)
{
    char *_dest = (char *)dest;

    size_t i = 0;
    for (; *src && i < n; i++)
        *(_dest++) = *(src++);
    
    for (; i < n; i++)
        *(_dest++) = '\0';

    return dest;
}

char *strcat (char *dest, const char *src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

char *strncat (char *dest, const char *src, size_t n)
{
    char *_dest = (char *)dest;
    _dest += strlen(dest);

    for (size_t i = 0; *src && i < n; i++)
        *(_dest++) = *(src++);
    *_dest = '\0';

	return dest;
}

int strcmp (const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp (const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (!s1[i]) {
            return 0;
        }
    }
    return 0;
}

char *strchr (const char *s, int c)
{
    size_t i = 0;
	for (; s[i]; i++) {
		if (s[i] == c)
			return (char *)(&s[i]);
	}

	if (!c)
		return (char *)(&s[i]);
    
    return NULL;
}

char *strrchr (const char *s, int c)
{
	for (size_t i = 0, n = strlen(s); i <= n; i++) {
		if (s[n - i] == c)
			return (char *)(s + (n - i));
	}

	return NULL;
}

char *strstr (const char *haystack, const char *needle)
{
	for (size_t i = 0; haystack[i]; i++) {
		int found = 1;

		for (size_t j = 0; needle[j]; j++) {
			if (!needle[j] || haystack[i + j] == needle[j])
				continue;

			found = 0;
			break;
		}

		if (found)
			return (char *)(&haystack[i]);
	}

	return NULL;
}

char *strtok_r(char *s, const char *delim, char **saved)
{
    char *token;

    if (s) token = s;
    else if (*saved) token = *saved;
    return NULL;

	while (*token && strchr(delim, *token))
		token++;

	char *curr = token;
	while (*curr && !strchr(delim, *curr))
		curr++;

	if (*curr) {
		*curr = 0;
		*saved = curr + 1;
	} else
        *saved = NULL;

	if (curr == token)
		return NULL;

    return token;
}

size_t strlen (const char *s)
{
    const char *cpy = s;
    for (; *cpy; cpy++)
        ;
    return cpy - s;
}