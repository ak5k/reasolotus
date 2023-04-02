/******************************************************************************
/ configvar.cpp
/
/ Copyright (c) 2020 Christian Fillion
/ https://cfillion.ca
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights
to / use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies / of the Software, and to permit persons to whom the Software is
furnished to / do so, subject to the following conditions:
/
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/

#include <cstdio>
#include <reaper_plugin_functions.h>
#include <string>

#include "configvar.h"

#ifdef _WIN32
std::wstring widen(const char*, UINT codepage = CP_UTF8);
inline std::wstring widen(const std::string& str, UINT codepage = CP_UTF8)
{
    return widen(str.c_str(), codepage);
}
std::wstring widen(const char* input, const UINT codepage)
{
    const int size =
        MultiByteToWideChar(codepage, 0, input, -1, nullptr, 0) - 1;

    std::wstring output(size, 0);
    MultiByteToWideChar(codepage, 0, input, -1, &output[0], size);

    return output;
}
#define widen_cstr(cstr) widen(cstr).c_str()
#else
#include <swell/swell.h>
#define widen_cstr(cstr) cstr
#endif

template <>
void ConfigVar<int>::save()
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", *m_addr);
    WritePrivateProfileString(
        widen_cstr("REAPER"),
        widen_cstr(m_name),
        widen_cstr(buf),
        widen_cstr(get_ini_file()));
}
