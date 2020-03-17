/*
 *  Copyright (C) 2020 Michael Goldschmidt
 *
 *  This file is part of wsd/wscat.
 *
 *  wsd/wscat is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  wsd/wscat is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with wsd/wscat.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "uri.h"

int
parse_uri(char *uri_arg, uri_t *uri)
{
     char *c = uri_arg;
     char *begin = c;
     while (*c != '\0' && *c != ':')
          c++;
     if ('\0' == *c || begin == c)
          return (-1);
     uri->scheme.len = (unsigned int)(c - begin);
     uri->scheme.p = begin;
     c++;           /* Skip colon */
     if (*c == '\0' || *c != '/')
          return (-1);
     c++;           /* Skip 1st slash */
     if (*c == '\0' || *c != '/')
          return (-1);
     c++;           /* Skip 2nd slash */
     begin = c;
     while (*c != '\0' && *c != ':' && *c != '/')
          c++;
     if (begin == c)
          return (-1);
     uri->host.p = begin;
     uri->host.len = (unsigned int)(c - begin);
     if (*c == '\0')
          goto empty_path;
     if (*c == ':') {
          c++;           /* Skip colon */
          begin = c;
          while (*c != '\0' && *c != '/')
               c++;
          if (begin == c)
               return (-1);
          uri->port.p = begin;
          uri->port.len = (unsigned int)(c - begin);
          if (*c == '\0')
               goto empty_path;
     }
     if (*c != '/')
          return (-1);
     begin = c;
     while (*c != '\0' && *c != '#')
          c++;
     if (begin == c)
          goto empty_path;
     if (*(begin + 1) == '/')
          return (-1); /* See section 3.3 RFC3986 */
     uri->path.p = begin;
     uri->path.len = (unsigned int)(c - begin);
     return 0;
empty_path:
     uri->path.p = "/"; /* Normalised due to section 5.3.1 RFC7230 */
     uri->path.len = 1;
     return 0;
}
