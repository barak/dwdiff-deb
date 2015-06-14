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

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "profile.h"
#include "definitions.h"
#include "util.h"

#define SPACE " \t\v\f\n\r"

static void stripSpaces(char *text, size_t length, char **first, size_t *stripped_length) {
	while (length > 0 && strchr(SPACE, *text) != NULL) {
		text++;
		length--;
	}
	while (length > 0 && strchr(SPACE, text[length - 1]) != NULL) {
		length--;
	}
	*first = text;
	*stripped_length = length;
}

ProfileOption *loadProfile(const char *name, const char *profile) {
	ProfileOption *profileOptions = NULL;

	struct passwd pwEntry;
	struct passwd *result;
	char buffer[4096];

	int error;
	const char *dir;
	char *pathname;
	FILE *file;

	char *line = NULL;
	size_t n = 0;
	ssize_t bytesRead;
	int lineNumber = 0;
	bool saveProfile = false;

	if ((error = getpwuid_r(getuid(), &pwEntry, buffer, sizeof(buffer), &result)) != 0 || result != &pwEntry) {
		const char *env = getenv("HOME");
		if (env == NULL || strlen(env) == 0) {
			errno = ENOENT;
			return NULL;
		}
		dir = env;
	} else {
		dir = pwEntry.pw_dir;
	}

	if ((pathname = malloc(strlen(dir) + 1 + strlen(name) + 1)) == NULL)
		return NULL;
	strcpy(pathname, dir);
	strcat(pathname, "/");
	strcat(pathname, name);

	if ((file = fopen(pathname, "r")) == NULL) {
		if (errno == ENOENT && strcmp(profile, "default") == 0)
			return NULL;
		fatal(_("Could not open configuration file %s: %s\n"), pathname, strerror(errno));
	}

	bool profileFound = false;
	while ((bytesRead = getline(&line, &n, file)) > 0) {
		char *first, *space;
		size_t len;
		lineNumber++;

		stripSpaces(line, strlen(line), &first, &len);

		// Skip empty lines
		if (len == 0) continue;
		// Skip comments
		if (first[0] == '#') continue;

		// Check for profile headers.
		if (first[0] == '[' && first[len - 1] == ']') {
			stripSpaces(first + 1, len - 2, &first, &len);
			if (len == 0) {
				saveProfile = false;
				fprintf(stderr, _("Warning: empty profile name on line %s:%d\n"), pathname, lineNumber);
				continue;
			}
			first[len] = 0;
			saveProfile = strcmp(profile, first) == 0;
			profileFound |= saveProfile;
			continue;
		}

		// If we're not interested in this profile, skip.
		if (!saveProfile)
			continue;

		ProfileOption *new_option = malloc(sizeof(ProfileOption));
		if (new_option == NULL)
			outOfMemory();

		// Find the first space, or end of line.
		space = first;
		while (space < first + len && strchr(SPACE, *space) == NULL) space++;

		// Copy the first word as the option name. This keeps space for copying
		// in the argument as well.
		if ((new_option->arg = malloc(len + 3)) == NULL)
			outOfMemory();
		new_option->arg[0] = '-';
		new_option->arg[1] = '-';
		memcpy(new_option->arg + 2, first, space - first);
		size_t arg_len = space - first + 2;
		new_option->arg[arg_len] = 0;

		// If there is no text left, the option has no value, otherwise copy the value.
		if (space != first + len) {
			stripSpaces(space, len - (space - first), &first, &len);
			// Check for quotes.
			if (len > 0 && first[0] == first[len - 1] && (first[0] == '"' || first[0] == '\'')) {
				first++;
				len -= 2;
			}
			new_option->arg[arg_len++] = '=';
			memcpy(new_option->arg + arg_len, first, len);
			arg_len += len;
			new_option->arg[arg_len] = 0;
		}
		new_option->next = profileOptions;
		profileOptions = new_option;
	}

	// Reverse the option list.
	{
		ProfileOption *ptr, *prev = NULL;
		ptr = profileOptions;
		while (ptr != NULL) {
			ProfileOption *next = ptr->next;
			if (next == NULL)
				profileOptions = ptr;
			ptr->next = prev;
			prev = ptr;
			ptr = next;
		}
	}

	free(pathname);
	fclose(file);

	if (!profileFound && strcmp(profile, "default") != 0)
		fatal("Profile %s not found\n", profile);
	return profileOptions;
}

void applyProfileOptions(ProfileOption *options, void (*parse)(int argc, char *argv[]), char *argv0) {
	ProfileOption *ptr = options;
	while (ptr != NULL) {
		char *argv[3];
		argv[0] = argv0;
		argv[1] = ptr->arg;
		argv[2] = NULL;
		parse(2, argv);
		ptr = ptr->next;
	}
}
