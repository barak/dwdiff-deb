/* Copyright (C) 2015 G.P. Halkes
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
#ifndef PROFILE_H
#define PROFILE_H

typedef struct ProfileOption {
	char *arg;
	struct ProfileOption *next;
} ProfileOption;

ProfileOption *loadProfile(const char *name, const char *profile);
void applyProfileOptions(ProfileOption *options, void (*parse)(int argc, char *argv[]), char *argv0);

#endif
