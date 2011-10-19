/* Copyright (C) 2006-2007 G.P. Halkes
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

#ifndef OPTIONMACROS_H
#define OPTIONMACROS_H

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>

/** Define an option parsing function.
	@param name The name of the function to define.
*/
#define PARSE_FUNCTION(name) void name(int argc, char **argv) {\
	char *optArg; \
	int optargind;

/** Indicate the start of option processing.

	This is separte from @a PARSE_FUNCTION so that local variables can be
	defined.
*/
#define OPTIONS \
	for (optargind = 1; optargind < argc; optargind++) { \
		int optInd; \
		optInd = optargind; \
		if (argv[optargind][0] == '-') { \
			size_t optlength; \
			ArgType opttype; \
\
			if (argv[optargind][1] == '-') { \
				if ((optArg = strchr(argv[optargind], '=')) == NULL) { \
					optlength = strlen(argv[optargind]); \
				} else { \
					optlength = optArg - argv[optargind]; \
					optArg++; \
					if (*optArg == 0) { \
						fatal(_("Option %.*s has zero length argument\n"), (int) optlength, argv[optInd]); \
					} \
				} \
				opttype = LONG; \
			} else { \
				optlength = 2; \
				if (argv[optargind][1] != 0 && argv[optargind][2] != 0) \
					optArg = argv[optargind] + 2; \
				else \
					optArg = NULL; \
				opttype = SHORT; \
			} \
			if (optlength > INT_MAX) optlength = INT_MAX; 
/* The last line above is to make sure the cast to int in error messages does
   not overflow. */

/** Signal the start of non-switch option processing. */
#define NO_OPTION } else {

/** Signal the end of option processing. */
#define END_OPTIONS }}

/** Signal the end of the option processing function. */
#define END_FUNCTION }

/** The string containing the current option, including integrated option
		argument, if applicable.
	
	To print the option, use the %.*s format specifier, and include
	optlength, CURRENT_OPTION in the arguments of printf.
*/
#define CURRENT_OPTION argv[optInd]

/** Internal macro to check whether the requirements regarding option arguments
		have been met. */
#define CHECK_ARG(argReq) \
	switch(argReq) { \
		case NO_ARG: \
			if (optArg != NULL) { \
				fatal(_("Option %.*s does not take an argument\n"), (int) optlength, argv[optInd]); \
			} \
			break; \
		case REQUIRED_ARG: \
			if (optArg == NULL && (optargind+1 >= argc)) { \
				fatal(_("Option %.*s requires an argument\n"), (int) optlength, argv[optInd]); \
			} \
			if (optArg == NULL) optArg = argv[++optargind]; \
			break; \
		default: \
			break; \
	}

/** Check for a short style (-o) option.
	@param shortName The name of the short style option.
	@param argReq Whether or not an argument is required/allowed. One of NO_ARG,
		OPTIONAL_ARG or REQUIRED_ARG.
*/
#define SHORT_OPTION(shortName, argReq) if (opttype == SHORT && argv[optargind][1] == shortName) { CHECK_ARG(argReq) {

/** Check for a single dash as option.

	This is usually used to signal standard input/output.
*/
#define SINGLE_DASH SHORT_OPTION('\0', NO_ARG)

/** Check for a double dash as option.

	This is usually used to signal the end of options.
*/
#define DOUBLE_DASH LONG_OPTION("", NO_ARG)

/** Check for a short style (-o) or long style (--option) option.
	@param shortName The name of the short style option.
	@param longName The name of the long style option.
	@param argReq Whether or not an argument is required/allowed. One of NO_ARG,
		OPTIONAL_ARG or REQUIRED_ARG.
*/
#define OPTION(shortName, longName, argReq) if ((opttype == SHORT && argv[optargind][1] == shortName) || (opttype == LONG && strlen(longName) == optlength - 2 && strncmp(argv[optargind] + 2, longName, optlength - 2) == 0)) { CHECK_ARG(argReq) {

/** Check for a long style (--option) option.
	@param longName The name of the long style option.
	@param argReq Whether or not an argument is required/allowed. One of NO_ARG,
		OPTIONAL_ARG or REQUIRED_ARG.
*/
#define LONG_OPTION(longName, argReq) if (opttype == LONG && strlen(longName) == optlength - 2 && strncmp(argv[optargind] + 2, longName, optlength - 2) == 0) { CHECK_ARG(argReq) {

/** Signal the end of processing for the previous (SHORT_|LONG_)OPTION. */
#define END_OPTION } continue; }

/** Check for presence of a short style (-o) option and set the variable if so.
	@param shortName The name of the short style option.
	@param var The variable to set.
*/
#define BOOLEAN_SHORT_OPTION(shortName, var) SHORT_OPTION(shortName, NO_ARG) var = true; END_OPTION

/** Check for presence of a long style (--option) option and set the variable
		if so.
	@param longName The name of the long style option.
	@param var The variable to set.
*/
#define BOOLEAN_LONG_OPTION(longName, var) LONG_OPTION(longName, NO_ARG) var = true; END_OPTION

/** Check for presence of a short style (-o) or long style (--option) option
		and set the variable if so.
	@param shortName The name of the short style option.
	@param longName The name of the long style option.
	@param var The variable to set.
*/
#define BOOLEAN_OPTION(shortName, longName, var) OPTION(shortName, longName, NO_ARG) var = true; END_OPTION

/** Check an option argument for an integer value.
	@param var The variable to store the result in.
	@param min The minimum allowable value.
	@param max The maximum allowable value.
*/
#define PARSE_INT(var, min, max) do {\
	char *endptr; \
	long value; \
	errno = 0; \
	\
	value = strtol(optArg, &endptr, 10); \
	if (*endptr != 0) { \
		fatal("Garbage after value for %.*s option\n", (int) optlength, argv[optInd]); \
	} \
	if (errno != 0 || value < min || value > max) { \
		fatal("Value for %.*s option (%ld) is out of range\n", (int) optlength, argv[optInd], value); \
	} \
	var = (int) value; } while(0)

typedef enum {
	SHORT,
	LONG
} ArgType;

enum {
	NO_ARG,
	OPTIONAL_ARG,
	REQUIRED_ARG
};

#endif
