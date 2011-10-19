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

#ifndef FILE_H
#define FILE_H

/* Note: files opened for write can also be read. However, not vice versa. */
typedef enum {
	FILE_READ,
	FILE_WRITE
} FileMode;

typedef enum {
	EOF_NO,
	EOF_COMING,
	EOF_HIT
} EOFState;

#ifdef PAGE_SIZE
#define FILE_BUFFER_SIZE PAGE_SIZE
#else
#define FILE_BUFFER_SIZE 4096
#endif

typedef struct {
	/* FD this struct is buffering */
	int fd;
	/* 0, or the error code of the failed operation. Further operations will
	   fail immediately. */
	int errNo;
	/* Current mode associated with the File. */
	FileMode mode;

	/* Buffer containing data from the file. */
	char buffer[FILE_BUFFER_SIZE];
	int bufferFill;
	/* Current index in the buffer. */
	int bufferIndex;

	/* Flag to indicate whether filling the buffer hit end of file. */
	EOFState eof;
#ifdef NO_MINUS_A
	/* Flag to indicate whether to escape non-printable characters. */
	int escapeNonPrint;
#endif
} File;

File *fileWrapFD(int fd, FileMode mode);
File *fileOpen(const char *name, FileMode mode);
int fileGetc(File *file);
int fileUngetc(File *file, int c);
int fileClose(File *file);
int fileFlush(File *file);
int filePutc(File *file, int c);
int fileWrite(File *file, const char *buffer, int bytes);
int filePuts(File *file, const char *string);
int fileRewind(File *file, FileMode mode);
int fileError(File *file);
int fileGetErrno(File *file);
int fileEof(File *file);

#endif
