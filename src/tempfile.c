/* Copyright (C) 2006-2008 G.P. Halkes
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

/* #define LEAVE_FILES */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "definitions.h"
#include "stream.h"

static TempFile files[6];
static int openIndex = 0;

#ifndef LEAVE_FILES
static bool inited;

/** Remove up all the created files. */
static void cleanup(void) {
	int i;
	for (i = 0; i < openIndex; i++) {
		fileClose(files[i].stream->data.file);
		remove(files[i].name);
	}
}
#endif

/** Create a temporary file. */
TempFile *tempFile(void) {
#ifdef LEAVE_FILES
	/* In case we are testing and want to see the temporary files,
	   we use these names. */
	static const char *names[6] = { "oldTokens", "oldDiffTokens", "oldWhitespace", "newTokens", "newDiffTokens", "newWhitespace" };

	strcpy(files[openIndex].name, names[openIndex]);
	if ((files[openIndex].stream = newFileStream(fileOpen(files[openIndex].name, FILE_WRITE))) == NULL)
		return NULL;
#else
	/* Create temporary file. */
	int fd;

	if (!inited) {
		/* Make sure the umask is set so that we don't introduce a security risk. */
		umask(~S_IRWXU);
		/* Make sure we will remove temporary files on exit. */
		atexit(cleanup);
		inited = true;
	}

	strcpy(files[openIndex].name, TEMPLATE);
	if ((fd = mkstemp(files[openIndex].name)) < 0)
		return NULL;
	if ((files[openIndex].stream = newFileStream(fileWrapFD(fd, FILE_WRITE))) == NULL)
		return NULL;
#endif
	return files + openIndex++;
}
