/* Copyright (C) 2006-2011 G.P. Halkes
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
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "definitions.h"
#include "stream.h"
#include "option.h"

static TempFile *files;
static int openIndex = 0;
static bool inited;
static int baseFiles = 6;

/** Remove up all the created files. */
static void cleanup(void) {
	int i;
	for (i = 0; i < openIndex; i++) {
#ifndef LEAVE_FILES
		remove(files[i].name);
#endif
	}
}

/** Create a temporary file. */
TempFile *tempFile(int depth) {
	int fd;

	if (!inited) {
		int steps = 0, context = option.matchContext;

		while (context > 0) {
			steps++;
			context /= 2;
		}

		baseFiles += 2 * option.diffInput;

		if ((files = calloc(baseFiles + 2 * steps, sizeof(TempFile))) == NULL)
			outOfMemory();
		/* Make sure the umask is set so that we don't introduce a security risk. */
		umask(~S_IRWXU);
		/* Make sure we will remove temporary files on exit. */
		atexit(cleanup);
		inited = true;
	}

	/* The part files need to be cleaned up when new part files are needed. */
	if (depth >= 0 && depth + baseFiles < openIndex) {
		int i;
		for (i = baseFiles + depth; i < openIndex; i++) {
			/* Don't close the file here, but let doDiffInternal do that such that
			   it is flushed before diff invocation. */
#ifndef LEAVE_FILES
			remove(files[i].name);
#endif
			free(files[i].stream);
		}
		openIndex = baseFiles + depth;
	}

	strcpy(files[openIndex].name, TEMPLATE);
	if ((fd = mkstemp(files[openIndex].name)) < 0)
		return NULL;
	if ((files[openIndex].stream = newFileStream(fileWrapFD(fd, FILE_WRITE))) == NULL)
		return NULL;
	return files + openIndex++;
}
