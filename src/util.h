/* Copyright (C) 2008 G.P. Halkes
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UTIL_H
#define UTIL_H

#ifdef __GNUC__
void fatal(const char *fmt, ...) __attribute__((format (printf, 1, 2))) __attribute__((noreturn));
void outOfMemory(void) __attribute__((noreturn));
#else
/*@noreturn@*/ void fatal(const char *fmt, ...);
/*@noreturn@*/ void outOfMemory(void);
#endif
int ASCIItolower(int c);

#define PANIC() fatal(_("Program logic error at %s:%d\n"), __FILE__, __LINE__)
#define ASSERT(_condition) do { if (!(_condition)) PANIC(); } while(0)

#endif
