#include "stdafx.h"

#include <time.h>

char* timestamp(string64& dest)
{
    string64 temp;

    /* Set time zone from TZ environment variable. If TZ is not set,
     * the operating system is queried to obtain the default value
     * for the variable.
     */
    _tzset();
    u32 it;

    // date
    _strdate(temp);
    for (it = 0; it < xr_strlen(temp); it++)
        if ('/' == temp[it])
            temp[it] = '-';
    strconcat(sizeof(dest), dest, temp, "_");

    // time
    _strtime(temp);
    for (it = 0; it < xr_strlen(temp); it++)
        if (':' == temp[it])
            temp[it] = '-';
    strcat(dest, temp);
    return dest;
}

char* xr_strdup(const char* string)
{
    VERIFY(string);
    size_t len = strlen(string) + 1;
    char* memory = (char*)Memory.mem_alloc(len);
    CopyMemory(memory, string, len);
    return memory;
}

void get_token_id(xr_token* tokens, LPCSTR key,
                  std::function<void(int)> foundFn,
                  std::function<void()> notFoundFn)
{
    const xr_token* tok = tokens;
    while (tok->name)
    {
        if (stricmp(tok->name, key) == 0)
        {
            foundFn(tok->id);
            return;
        }
        tok++;
    }

    if (notFoundFn)
        notFoundFn();
}
