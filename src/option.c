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

#include <ctype.h>
#include <unistd.h>

#include "definitions.h"
#include "stream.h"
#include "unicode.h"
#include "optionMacros.h"
#include "option.h"
#include "util.h"
#include "buffer.h"
#include "dispatch.h"

#define VERSION_STRING "1.4"

typedef struct {
	const char *name;
	const char *description;
	const char *sequence;
} Color;

Color colors[] = {
	{ "black", N_("Black"), "\033[0;30m" },
	{ "red", N_("Red"), "\033[0;31m" },
	{ "green", N_("Green"), "\033[0;32m" },
	{ "brown", N_("Brown"), "\033[0;33m" },
	{ "blue", N_("Blue"), "\033[0;34m" },
	{ "magenta", N_("Magenta"), "\033[0;35m" },
	{ "cyan", N_("Cyan"), "\033[0;36m" },
	{ "gray", N_("Gray"), "\033[0;37m" },
	{ "dgray", N_("Dark gray"), "\033[0;30;1m" },
	{ "bred", N_("Bright red"), "\033[0;31;1m" },
	{ "bgreen", N_("Bright green"), "\033[0;32;1m" },
	{ "yellow", N_("Yellow"), "\033[0;33;1m" },
	{ "bblue", N_("Bright blue"), "\033[0;34;1m" },
	{ "bmagenta", N_("Bright magenta"), "\033[0;35;1m" },
	{ "bcyan", N_("Bright cyan"), "\033[0;36;1m" },
	{ "white", N_("White"), "\033[0;37;1m" },
	{ NULL, NULL, NULL}
};

/** Compare two strings, ignoring case.
	@param a The first string to compare.
	@param b The first string to compare.
	@return an integer smaller, equal, or larger than zero to indicate that @a a
		sorts before, the same, or after @a b.
*/
int strCaseCmp(const char *a, const char *b) {
	int ca, cb;
	while ((ca = tolower(*a++)) == (cb = tolower(*b++)) && ca != 0) {}

	return ca == cb ? 0 : (ca < cb ? -1 : 1);
}

/** Convert a string from the input format to an internally usable string.
	@param string A @a Token with the string to be converted.
	@param descr A description of the string to be included in error messages.
	@return The length of the resulting string.

	The use of this function processes escape characters. The converted
	characters are written in the original string.
*/
static size_t parseEscapes(char *string, const char *descr) {
	size_t maxReadPosition = strlen(string);
	size_t readPosition = 0, writePosition = 0;
	size_t i;

	while(readPosition < maxReadPosition) {
		if (string[readPosition] == '\\') {
			readPosition++;

			if (readPosition == maxReadPosition) {
				fatal(_("Single backslash at end of %s argument\n"), descr);
			}
			switch(string[readPosition++]) {
				case 'n':
					string[writePosition++] = '\n';
					break;
				case 'r':
					string[writePosition++] = '\r';
					break;
				case '\'':
					string[writePosition++] = '\'';
					break;
				case '\\':
					string[writePosition++] = '\\';
					break;
				case 't':
					string[writePosition++] = '\t';
					break;
				case 'b':
					string[writePosition++] = '\b';
					break;
				case 'f':
					string[writePosition++] = '\f';
					break;
				case 'a':
					string[writePosition++] = '\a';
					break;
				case 'v':
					string[writePosition++] = '\v';
					break;
				case '?':
					string[writePosition++] = '\?';
					break;
				case '"':
					string[writePosition++] = '"';
					break;
				case 'x': {
					/* Hexadecimal escapes */
					unsigned int value = 0;
					/* Read at most two characters, or as many as are valid. */
					for (i = 0; i < 2 && (readPosition + i) < maxReadPosition && isxdigit(string[readPosition + i]); i++) {
						value <<= 4;
						if (isdigit(string[readPosition + i]))
							value += (int) (string[readPosition + i] - '0');
						else
							value += (int) (tolower(string[readPosition + i]) - 'a') + 10;
						if (value > UCHAR_MAX)
							/* TRANSLATORS:
							   The %s argument is a long option name without preceding dashes. */
							fatal(_("Invalid hexadecimal escape sequence in %s argument\n"), descr);
					}
					readPosition += i;

					if (i == 0)
						/* TRANSLATORS:
						   The %s argument is a long option name without preceding dashes. */
						fatal(_("Invalid hexadecimal escape sequence in %s argument\n"), descr);

					string[writePosition++] = (char) value;
					break;
				}
				case '0':
				case '1':
				case '2': {
					/* Octal escapes */
					int value = (int)(string[readPosition - 1] - '0');
					for (i = 0; i < 2 && readPosition + i < maxReadPosition && string[readPosition + i] >= '0' && string[readPosition + i] <= '7'; i++)
						value = value * 8 + (int)(string[readPosition + i] - '0');

					readPosition += i;

					string[writePosition++] = (char) value;
					break;
				}
#ifdef USE_UNICODE
				case 'u':
				case 'U': {
					UChar32 value = 0;
					size_t chars = string[readPosition - 1] == 'U' ? 8 : 4;

					if (maxReadPosition < readPosition + chars)
						/* TRANSLATORS:
						   The %c argument will be either 'u' or 'U'. The %s argument is a long
					       option name without preceding dashes. */
						fatal(_("Too short \\%c escape in %s argument\n"), string[readPosition - 1], descr);
					for (i = 0; i < chars; i++) {
						if (!isxdigit(string[readPosition + i]))
							/* TRANSLATORS:
							   The %c argument will be either 'u' or 'U'. The %s argument is a long
							   option name without preceding dashes. */
							fatal(_("Too short \\%c escape in %s argument\n"), string[readPosition - 1], descr);
						value <<= 4;
						if (isdigit(string[readPosition + i]))
							value += (int) (string[readPosition + i] - '0');
						else
							value += (int) (tolower(string[readPosition + i]) - 'a') + 10;
					}

					if (value > 0x10FFFFL)
						/* TRANSLATORS:
						   The %s argument is a long option name without preceding dashes. */
						fatal(_("\\U escape out of range in %s argument\n"), descr);

					if ((value & 0xF800L) == 0xD800L)
						/* TRANSLATORS:
						   The %s argument is a long option name without preceding dashes. */
						fatal(_("\\%c escapes for surrogate codepoints are not allowed in %s argument\n"), string[readPosition - 1], descr);

					/* The conversion won't overwrite subsequent characters because
					   \uxxxx is already the as long as the max utf-8 length */
					writePosition += convertToUTF8(value, string + writePosition);
					break;
				}
#endif
				default:
					string[writePosition++] = string[readPosition];
					break;
			}
		} else {
			string[writePosition++] = string[readPosition++];
		}
	}
	/* Terminate string. */
	string[writePosition] = 0;
	return writePosition;
}

static void noDefaultMarkers(void) {
	/* Note: then lenghts are 0 by default. */
	if (option.addStart == NULL)
		option.addStart = "";
	if (option.addStop == NULL)
		option.addStop = "";
	if (option.delStart == NULL)
		option.delStart = "";
	if (option.delStop == NULL)
		option.delStop = "";
}


/*===============================================================*/
/* Single character (SC) versions of the addCharacters and checkOverlap routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

/** Add all characters to the specified list or bitmap.
	@param chars The string with characters to add to the list or bitmap.
	@param list The list to add to.
	@param bitmap. The bitmap to add to.

	If in UTF-8 mode, the characters will be added to the list. Otherwise to
	the bitmap.
*/
void addCharactersSC(const char *chars, size_t length, CHARLIST *list, char bitmap[BITMASK_SIZE]) {
	size_t i;

	/* Suppress unused parameter warning in semi-portable way. */
	(void) list;

	for (i = 0; i < length; i++) {
		SET_BIT(bitmap, chars[i]);
	}
}

/** Check for overlap in whitespace and delimiter sets. */
void checkOverlapSC(void) {
	int i;

	if (!option.whitespaceSet)
		return;

	for (i = 0; i <= UCHAR_MAX; i++) {
		if (!TEST_BIT(option.delimiters, i))
			continue;

		if (TEST_BIT(option.whitespace, i))
			fatal(_("Whitespace and delimiter sets overlap\n"));
	}
}

void setPunctuationSC(void) {
	int i;
	for (i = 0; i <= UCHAR_MAX; i++) {
		if (!ispunct(i))
			continue;
		SET_BIT(option.delimiters, i);
	}
}

void initOptionsSC(void) {}

void postProcessOptionsSC(void) {
	if (!option.whitespaceSet) {
		int i;
		for (i = 0; i <= UCHAR_MAX; i++) {
			if (!TEST_BIT(option.delimiters, i) && isspace(i))
				SET_BIT(option.whitespace, i);
		}
	}

#ifndef NO_MINUS_A
	/* Not needed if there is no -a option, as option.transliterate will always
	   be true then. */

	if (!TEST_BIT(option.whitespace, '\n'))
		option.transliterate = true;
#endif
}

#ifdef USE_UNICODE
/*===============================================================*/
/* UTF-8 versions of the addCharacters and checkOverlap routines.
   Descriptions can be found in the definition of the DispatchTable struct. */

static void reallocList(CharList *list) {
	list->allocated *= 2;
	ASSERT(list->allocated > 0);
	if ((list->list = realloc(list->list, list->allocated * sizeof(UTF16Buffer))) == NULL)
		outOfMemory();
}

/** Add all characters to the specified list.
	@param chars The string with characters to add to the list or bitmap.
	@param list The list to add to.
	@param bitmap. The bitmap to add to (unused).
*/
void addCharactersUTF8(const char *chars, size_t length, CHARLIST *list, char bitmap[BITMASK_SIZE]) {
	Stream *charStream;
	CharData cluster;

	/* Suppress unused parameter warning in semi-portable way. */
	(void) bitmap;

	UTF16BufferInit(&cluster.UTF8Char.original);
	UTF16BufferInit(&cluster.UTF8Char.converted);
	charStream = newStringStream(chars, length);

	while (getCluster(charStream, &cluster.UTF8Char.original)) {
		int i;

		decomposeChar(&cluster);

		/* Check for duplicates */
		for (i = 0; i < list->used; i++)
			if (compareUTF16Buffer(&cluster.UTF8Char.converted, &list->list[i]) == 0)
				break; /* continue won't work because we're in a for-loop */
		if (i != list->used)
			continue;

		if (list->allocated == list->used)
			reallocList(list);
		list->list[list->used].length = cluster.UTF8Char.converted.length;
		list->list[list->used].data = malloc(cluster.UTF8Char.converted.length * sizeof(UChar));
		if (list->list[list->used].data == NULL)
			outOfMemory();
		memcpy(list->list[list->used].data, cluster.UTF8Char.converted.data, cluster.UTF8Char.converted.length * sizeof(UChar));
		list->used++;
	}
	free(charStream);
}

/** Check for overlap in whitespace and delimiter sets. */
void checkOverlapUTF8(void) {
	int i, j;

	if (!option.whitespaceSet)
		return;

	for (i = 0, j = 0; i < option.delimiterList.used; i++) {
		for (; j < option.whitespaceList.used && compareUTF16Buffer(&option.delimiterList.list[i], &option.whitespaceList.list[j]) > 0; j++) /* NO-OP */;

		if (j == option.whitespaceList.used)
			break;

		if (compareUTF16Buffer(&option.delimiterList.list[i], &option.whitespaceList.list[j]) == 0)
			fatal(_("Whitespace and delimiter sets overlap\n"));
	}

	if (option.punctuationMask == 0)
		return;

	for (i = 0; i < option.whitespaceList.used; i++) {
		if (isUTF16Punct(&option.whitespaceList.list[i]))
			fatal(_("Whitespace and delimiter sets overlap\n"));
	}
	return;
}

void setPunctuationUTF8(void) {
	option.punctuationMask = U_GC_P_MASK | U_GC_S_MASK;
}

void initOptionsUTF8(void) {
	option.whitespaceList.allocated = 16;
	reallocList(&option.whitespaceList);
	option.delimiterList.allocated = 16;
	reallocList(&option.delimiterList);
}

void postProcessOptionsUTF8(void) {
	qsort(option.delimiterList.list, option.delimiterList.used,	sizeof(UTF16Buffer),
		(int (*)(const void *, const void *)) compareUTF16Buffer);

	qsort(option.whitespaceList.list, option.whitespaceList.used, sizeof(UTF16Buffer),
		(int (*)(const void *, const void *)) compareUTF16Buffer);

	/* Check if all newline characters are considered whitespace (not only newlines
	   followed by something else). If that is not the case, the input needs to be
	   transliterated. */
	if (option.whitespaceSet) {
		UTF16Buffer newline;
		UChar newlineData[1] = { '\n' };
		newline.data = newlineData;
		newline.length = 1;
		if (bsearch(&newline, option.whitespaceList.list, option.whitespaceList.used,
				sizeof(UTF16Buffer), (int (*)(const void *, const void *)) compareUTF16Buffer) == NULL) {
			option.transliterate = true;
		}
	}
}
/*===============================================================*/
#endif

const char *descriptions[] = {
/* Options showing info about the program */
N_("-h, --help                             Print this help message\n"),
N_("-v, --version                          Print version and copyright information\n"),

/* Options changing what is considered (non-)whitespace */
N_("-d <delim>, --delimiters=<delim>       Specifiy delimiters\n"),
N_("-P, --punctuation                      Use punctuation characters as delimiters\n"),
N_("-W <ws>, --white-space=<ws>            Specify whitespace characters\n"),

/* Options changing what is output */
N_("-1, --no-deleted                       Do not print deleted words\n"),
N_("-2, --no-inserted                      Do not print inserted words\n"),
N_("-3, --no-common                        Do not print common words\n"),
N_("-L[<width>], --line-numbers[<width>]   Prepend line numbers\n"),
N_("-C<num>, --context=<num>               Show <num> lines of context\n"),
N_("-s, --statistics                       Print statistics when done\n"),

/* Options changing the matching */
N_("-i, --ignore-case                      Ignore differences in case\n"),
N_("-I, --ignore-formatting                Ignore formatting differences\n"),
N_("-D <option>, --diff-option=<option>    Add <option> to the diff command-line\n"),


/* Options changing the appearence of the output */
N_("-c[<spec>], --color[=<spec>]           Color mode\n"),
N_("-l, --less-mode                        As -p but also overstrike whitespace\n"),
N_("-p, --printer                          Use overstriking and bold text\n"),
N_("-w <string>, --start-delete=<string>   String to mark begin of deleted text\n"),
N_("-x <string>, --stop-delete=<string>    String to mark end of deleted text\n"),
N_("-y <string>, --start-insert=<string>   String to mark begin of inserted text\n"),
N_("-z <string>, --stop-insert=<string>    String to mark end of inserted text\n"),

NULL};

PARSE_FUNCTION(parseCmdLine)

	int noOptionCount = 0;
	/* Initialise options to correct values */
	memset(&option, 0, sizeof(option));
	option.printDeleted = option.printAdded = option.printCommon = true;

	initOptions();
	ONLY_UNICODE(option.decomposition = UNORM_NFD;)

	option.needStartStop = true;
#ifdef NO_MINUS_A
	option.transliterate = true;
#endif

	OPTIONS
		OPTION('d', "delimiters", REQUIRED_ARG)
			size_t length = parseEscapes(optArg, "delimiters");
			addCharacters(optArg, length, SWITCH_UNICODE(&option.delimiterList, NULL), option.delimiters);
		END_OPTION
		OPTION('P', "punctuation", NO_ARG)
			setPunctuation();
		END_OPTION
		OPTION('W', "white-space", REQUIRED_ARG)
			size_t length = parseEscapes(optArg, "whitespace");
			option.whitespaceSet = true;
			addCharacters(optArg, length, SWITCH_UNICODE(&option.whitespaceList, NULL), option.whitespace);
		END_OPTION
		OPTION('h', "help", NO_ARG)
				int i = 0;
				printf(_("Usage: dwdiff [OPTIONS] <OLD FILE> <NEW FILE>\n"));
				while (descriptions[i] != NULL)
					fputs(_(descriptions[i++]), stdout);
				exit(EXIT_SUCCESS);
		END_OPTION
		OPTION('v', "version", NO_ARG)
			fputs("dwdiff " VERSION_STRING "\n", stdout);
			fputs(
				/* TRANSLATORS:
				   - If the (C) symbol (that is the c in a circle) is not available,
					 leave as it as is. (Unicode code point 0x00A9)
				   - G.P. Halkes is name and should be left as is. */
				_("Copyright (C) 2006-2008 G.P. Halkes\nLicensed under the GNU General Public License version 3\n"), stdout);
			exit(EXIT_SUCCESS);
		END_OPTION
		OPTION('1', "no-deleted", NO_ARG)
			option.printDeleted = false;
			if (!option.printAdded)
				option.needMarkers = true;
			if (!option.printCommon || !option.printAdded)
				option.needStartStop = false;
		END_OPTION
		OPTION('2', "no-inserted", NO_ARG)
			option.printAdded = false;
			if (!option.printDeleted)
				option.needMarkers = true;
			if (!option.printCommon || !option.printDeleted)
				option.needStartStop = false;
		END_OPTION
		OPTION('3', "no-common", NO_ARG)
			option.printCommon = false;
			option.needMarkers = true;
		END_OPTION
		OPTION('i', "ignore-case", NO_ARG)
			option.ignoreCase = true;
		END_OPTION
#ifdef USE_UNICODE
		OPTION('I', "ignore-formatting", NO_ARG)
			if (UTF8Mode)
				option.decomposition = UNORM_NFKD;
			else
				fatal(_("Option %.*s is only supported for UTF-8 mode\n"), (int) optlength, CURRENT_OPTION);
#else
		OPTION('I', "ignore-formatting", NO_ARG)
			fatal(_("Support for option %.*s is not compiled into this version of dwdiff\n"), (int) optlength, CURRENT_OPTION);
#endif
		END_OPTION
		OPTION('s', "statistics", NO_ARG)
			option.statistics = true;
		END_OPTION
		OPTION('a', "autopager", NO_ARG)
			fatal(_("Option %.*s is not supported\n"), (int) optlength, CURRENT_OPTION);
		END_OPTION
		OPTION('p', "printer", NO_ARG)
			option.printer = true;
			noDefaultMarkers();
		END_OPTION
		OPTION('l', "less", NO_ARG)
			option.less = true;
			noDefaultMarkers();
		END_OPTION
		OPTION('t', "terminal", NO_ARG)
			fatal(_("Option %.*s is not supported\n"), (int) optlength, CURRENT_OPTION);
		END_OPTION
		OPTION('w', "start-delete", REQUIRED_ARG)
			option.delStartLen = parseEscapes(optArg, "start-delete");
			option.delStart = optArg;
		END_OPTION
		OPTION('x', "stop-delete", REQUIRED_ARG)
			option.delStopLen = parseEscapes(optArg, "stop-delete");
			option.delStop = optArg;
		END_OPTION
		OPTION('y', "start-insert", REQUIRED_ARG)
			option.addStartLen = parseEscapes(optArg, "start-insert");
			option.addStart = optArg;
		END_OPTION
		OPTION('z', "stop-insert", REQUIRED_ARG)
			option.addStopLen = parseEscapes(optArg, "stop-insert");
			option.addStop = optArg;
		END_OPTION
		OPTION('n', "avoid-wraps", NO_ARG)
			fatal(_("Option %.*s is not supported\n"), (int) optlength, CURRENT_OPTION);
		END_OPTION
		SINGLE_DASH
			switch (noOptionCount++) {
				case 0:
					option.oldFile.input = newFileStream(fileWrapFD(STDIN_FILENO, FILE_READ));
					break;
				case 1:
					if (option.oldFile.name == NULL)
						fatal(_("Can't read both files from standard input\n"));
					option.newFile.input = newFileStream(fileWrapFD(STDIN_FILENO, FILE_READ));
					break;
				default:
					fatal(_("Too many files to compare\n"));
			}
		END_OPTION
		DOUBLE_DASH
			/* Read all further options as file names, and stop processing*/
			for (optInd++; optInd < argc; optInd++) {
				switch (noOptionCount++) {
					case 0:
						option.oldFile.name = CURRENT_OPTION;
						break;
					case 1:
						option.newFile.name = CURRENT_OPTION;
						break;
					default:
						fatal(_("Too many files to compare\n"));
				}
			}
			break;
		END_OPTION
		OPTION('D', "diff-option", REQUIRED_ARG)
			option.diffOption = optArg;
		END_OPTION
		OPTION('c', "color", OPTIONAL_ARG)
			option.colorMode = true;
			if (optArg != NULL) {
				char *comma;
				int i;

				if (strCaseCmp(optArg, "list") == 0) {
					fputs(
						/* TRANSLATORS:
						   "Name" and "Description" are table headings for the color name list.
						   Make sure you keep the alignment of the headings over the text. */
						_("Name            Description\n"), stdout);
					fputs("--              --\n", stdout);
					for (i = 0; colors[i].name != NULL; i++)
						printf("%-15s %s\n", colors[i].name, _(colors[i].description));

					exit(EXIT_SUCCESS);
				}

				comma = strchr(optArg, ',');
				if (comma != NULL && strrchr(optArg, ',') != comma)
					fatal(_("Invalid color specification %s\n"), optArg);

				if (comma != NULL)
					*comma++ = 0;

				if (optArg[0] == 0) { /* , has already been converted to 0 */
					option.delColor = colors[9].sequence;
				} else {
					for (i = 0; colors[i].name != NULL; i++) {
						if (strCaseCmp(optArg, colors[i].name) == 0) {
							option.delColor = colors[i].sequence;
							break;
						}
					}
					if (colors[i].name == NULL)
						fatal(_("Invalid color %s\n"), optArg);
				}

				if (comma == NULL) {
					option.addColor = colors[10].sequence;
				} else {
					for (i = 0; colors[i].name != NULL; i++) {
						if (strCaseCmp(comma, colors[i].name) == 0) {
							option.addColor = colors[i].sequence;
							break;
						}
					}
					if (colors[i].name == NULL)
						fatal(_("Invalid color %s\n"), comma);
				}
			} else {
					option.delColor = colors[9].sequence;
					option.addColor = colors[10].sequence;
			}
			option.delColorLen = strlen(option.delColor);
			option.addColorLen = strlen(option.addColor);
			noDefaultMarkers();
		END_OPTION
		OPTION('L', "line-numbers", OPTIONAL_ARG)
			if (optArg != NULL)
				PARSE_INT(option.lineNumbers, 1, INT_MAX);
			else
				option.lineNumbers = DEFAULT_LINENUMBER_WIDTH;
		END_OPTION
		OPTION('C', "context", REQUIRED_ARG)
			PARSE_INT(option.contextLines, 0, INT_MAX);
			option.context = true;
			initContextBuffers();
		END_OPTION
		fatal(_("Option %.*s does not exist\n"), (int) optlength, CURRENT_OPTION);
	NO_OPTION
		switch (noOptionCount++) {
			case 0:
				option.oldFile.name = CURRENT_OPTION;
				break;
			case 1:
				option.newFile.name = CURRENT_OPTION;
				break;
			default:
				fatal(_("Too many files to compare\n"));
		}
	END_OPTIONS
	/* Check that we have something to work with. */
	if (noOptionCount != 2)
		fatal(_("Need two files to compare\n"));

	/* Check and set some values */
	if (option.delStart == NULL) {
		option.delStart = "[-";
		option.delStartLen = 2;
	}
	if (option.delStop == NULL) {
		option.delStop = "-]";
		option.delStopLen = 2;
	}
	if (option.addStart == NULL) {
		option.addStart = "{+";
		option.addStartLen = 2;
	}
	if (option.addStop == NULL) {
		option.addStop = "+}";
		option.addStopLen = 2;
	}

	if (!option.printCommon && !option.printAdded && !option.printDeleted)
		option.needMarkers = false;

	if ((!option.printAdded + !option.printDeleted + !option.printCommon) == 2)
		option.needStartStop = false;

	postProcessOptions();

	checkOverlap();
END_FUNCTION
