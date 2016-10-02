/***********************************************************************************************//**
\file    optparse.h
\author  hdaniel
\version $Id$

\brief Optparse-based portable, reentrant, embeddable, getopt-like option parser.

\details

The POSIX getopt() option parser has three fatal flaws. These flaws are solved by Optparse.

1) Parser state is stored entirely in global variables, some of which are static and inaccessible.
This means only one thread can use getopt(). It also means it's not possible to recursively parse
nested sub-arguments while in the middle of argument parsing. Optparse fixes this by storing all
state on a local struct.
    
2) The POSIX standard provides no way to properly reset the parser. This means for portable code
that getopt() is only good for one run, over one argv with one optstring. It also means subcommand
options cannot be processed with getopt(). Most implementations provide a method to reset the
parser, but it's not portable. Optparse provides an optparse_arg() function for stepping over
subcommands and continuing parsing of options with another optstring. The Optparse struct itself can
be passed around to subcommand handlers for additional subcommand option parsing. A full reset can
be achieved by with an additional optparse_init().
 
3) Error messages are printed to stderr. This can be disabled with opterr, but the messages
themselves are still inaccessible. Optparse solves this by writing an error message in its errmsg
field. The downside to Optparse is that this error message will always be in English rather than the
current locale.

Optparse should be familiar with anyone accustomed to getopt(), and it could be a nearly drop-in
replacement. The optstring is the same and the fields have the same names as the getopt() global
variables (optarg, optind, optopt).

Optparse also supports GNU-style long options with optparse_long(). The interface is slightly 
different and simpler than getopt_long().

By default, argv is permuted as it is parsed, moving non-option arguments to the end. This can be 
disabled by setting the 'permute' field to 0 after initialization.

\history

- 15-Sep-2016: 
    hdaniel: Added posix.2 '-W' support.
    hdaniel: Added include option to remove long option support.
    hdaniel: Arguments and their options no longer have to be in the same argv element.
    hdaniel: Refactored to an 'include-only' implementation and wrapped in namespace.
    hdaniel: Reformated and made to work with VS2010.
    hdaniel: Originated; Based on implementation from <https://github.com/skeeto/optparse>

\license

This file is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in
source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any
and all copyright interest in the software to the public domain. We make this dedication for the 
benefit of the public at large and to the detriment of our heirs and successors. We intend this 
dedication to be an overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
***************************************************************************************************/
#ifndef OPTPARSE_H
#define OPTPARSE_H

namespace optutils
{
	struct optparse_info
	{
		char **argv;
		int  permute;
		int  optind;
		int  optopt;
        bool opterr;
		char *optarg;
		char errmsg[64];
		int  subopt;
	};

	enum optparse_argtype 
    { 
        OPTPARSE_NONE, 
        OPTPARSE_REQUIRED, 
        OPTPARSE_OPTIONAL,
        OPTPARSE_POSIX_2
    };

	struct optparse_longopt 
	{
		char const            *name;
		enum optparse_argtype argtype;
        int                   *flag;
		int                   val;
	};

    #define OPTPARSE_LONGOPT_LAST    {0,(optutils::optparse_argtype)0,0,0}

	/**
	 * Initialize the parser state.
	 */
	void optparse_init( optparse_info *optinfo, char **argv );

	/**
	 * Read the next option in the argv array.
	 * @param optstring a getopt()-formatted option string.
	 * @return the next option character, -1 for done, or '?' for error
	 *
	 * Just like getopt(), a character followed by no colons means no argument. One colon means the
     * option has a required argument. Two colons means the option takes an optional argument.
	 */
	int optparse( optparse_info *optinfo, char const *optstring );

	/**
	 * Used for stepping over non-option arguments.
	 * @return the next non-option argument, or -1 for no more arguments
	 *
	 * Argument parsing can continue with optparse() after using this function. That would be used
     * to parse the options for the subcommand returned by optparse_arg(). This function allows you
     * to ignore the value of optind.
	 */
	char *optparse_arg( optparse_info *optinfo );

	/**
	 * Handles GNU-style long options in addition to getopt() options. This works a lot like GNU's
     * getopt_long(). The last option in longopts must be all zeros, marking the end of the array.
     * The longindex argument may be NULL.
	 */
	int optparse_long( optparse_info *optinfo,
                       char const *optstring,
				       optparse_longopt const *longopts,
				       int *longindex );

#if defined(OPTPARSE_IMPLEMENT) || defined(OPTPARSE_IMPLEMENT_NO_LONG)
#include "optparse\optparse.cpp"
#endif

} // namespace getopts

#endif
/* */
