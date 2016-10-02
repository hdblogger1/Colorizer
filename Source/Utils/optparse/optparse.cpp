/***********************************************************************************************//**
\file    optparse.cpp
\author  hdaniel
\version $Id$

\brief Optparse-based portable, reentrant, embeddable, getopt-like option parser.
 
\details

See header file optparse.h.

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

static char const *MSG_INVALID = "invalid option";
static char const *MSG_MISSING = "option requires an argument";
static char const *MSG_TOOMANY = "option takes no arguments";

int optparse_long_internal( optparse_info *optinfo, char const *optstring, 
                            optparse_longopt const *longopts, int *longindex );
int optparse_internal( optparse_info *optinfo, char const *optstring );

//##################################################################################################
// PUBLIC IMPLEMENTATION
//##################################################################################################

//==================================================================================================
void optparse_init( optparse_info *optinfo, char **argv )
{
    optinfo->argv    = argv;
    optinfo->opterr  = true;
    optinfo->permute = 1;
    optinfo->optind  = 1;
    optinfo->subopt  = 0;
    optinfo->optarg  = 0;
    optinfo->errmsg[0] = '\0';
}

//==================================================================================================
int optparse( optparse_info *optinfo, char const *optstring )
{
    if( *optstring && *optstring == ':' ) { optinfo->opterr = false; }
    return optparse_internal( optinfo, optstring );
}

//==================================================================================================
int optparse_long( optparse_info *optinfo,
                   char const *optstring,
                   optparse_longopt const *longopts,
                   int *longindex )
{
    if( *optstring && *optstring == ':' ) { optinfo->opterr = false; }
    return optparse_long_internal( optinfo, optstring, longopts, longindex );
}

//==================================================================================================
char *optparse_arg( optparse_info *optinfo )
{
    optinfo->subopt = 0;
    char *option = optinfo->argv[optinfo->optind];
    if( option != 0 ) { optinfo->optind++; }
    return option;
}

//##################################################################################################
// PRIVATE IMPLEMENTATION
//##################################################################################################

//==================================================================================================
static int opterror( optparse_info *optinfo, char const *message, char const *data)
{
    unsigned p = 0;
    if( optinfo->opterr )
	{
		while( *message ) { optinfo->errmsg[p++] = *message++; }
		char const *sep = " -- '";
		while( *sep ) { optinfo->errmsg[p++] = *sep++; }
		while( p < sizeof(optinfo->errmsg) - 2 && *data ) { optinfo->errmsg[p++] = *data++; }
		optinfo->errmsg[p++] = '\'';
		optinfo->errmsg[p++] = '\0';
		return '?';
	}
    return (message == MSG_MISSING) ? ':' : '?';
}

//==================================================================================================
static inline int is_dashdash( char const *arg )
{
    return arg && arg[0] == '-' && arg[1] == '-' && arg[2] == '\0';
}

//==================================================================================================
static inline int is_shortopt( char const *arg )
{
    return arg && arg[0] == '-' && arg[1] != '-' && arg[1] != '\0';
}

//==================================================================================================
static void permute( optparse_info *optinfo, int index )
{
    char *nonoption = optinfo->argv[index];
    for( int i = index; i < optinfo->optind - 1; i++)
    {
        optinfo->argv[i] = optinfo->argv[i + 1];
    }
    optinfo->argv[optinfo->optind - 1] = nonoption;
}

//==================================================================================================
static int argtype( char const *optstring, char opt )
{
    if( opt == ':' ) { return -1; }
    for( ; *optstring && opt != *optstring; optstring++ );
    if( !*optstring ) { return -1; }

    int argType = OPTPARSE_NONE;
    if( optstring[1] == ':' )
    {
        argType = (optstring[2] == ':') ? OPTPARSE_OPTIONAL : OPTPARSE_REQUIRED;
    }
    else if( optstring[0] == 'W' && optstring[1] == ';' )
    {
        argType = OPTPARSE_POSIX_2; //POSIX.2 '-W'
    }
    return argType;
}

//==================================================================================================
static int optparse_internal( optparse_info *optinfo, char const *optstring )
{
    optinfo->errmsg[0] = '\0';
    optinfo->optopt = 0;
    optinfo->optarg = 0;

    char *option = optinfo->argv[optinfo->optind];
    if( option == 0 ) 
    {
        return -1;
    } 
    else if( is_dashdash( option ) ) 
    {
        optinfo->optind++; /* consume "--" */
        return -1;
    } 
    else if( !is_shortopt( option ) ) 
    {
        if( optinfo->permute ) 
        {
            int index = optinfo->optind;
            optinfo->optind++;
            int r = optparse_internal( optinfo, optstring );
            permute( optinfo, index );
            optinfo->optind--;
            return r;
        } 
        else 
        {
            return -1;
        }
    }

    option += optinfo->subopt + 1;
    optinfo->optopt = option[0];
    int type = argtype( optstring, option[0] );
    char *next = optinfo->argv[optinfo->optind + 1];

    switch( type ) 
    {
        case -1: {
            optinfo->optind++;
            char str[2] = {option[0]};
            return opterror( optinfo, MSG_INVALID, str );
        }
    
        case OPTPARSE_NONE:
            if (option[1]) { optinfo->subopt++; } 
            else 
            {
                optinfo->subopt = 0;
                optinfo->optind++;
            }
            return option[0];
    
        case OPTPARSE_POSIX_2:
        case OPTPARSE_REQUIRED:
            optinfo->subopt = 0;
            optinfo->optind++;

            if (option[1]) 
            { 
                optinfo->optarg = option + 1; 
            } 
            else if( next && next[0] != '-' ) 
            {
                optinfo->optarg = next;
                optinfo->optind++;
            } 
            else 
            {
                optinfo->optarg = 0;
                char str[2] = {option[0]};
                return opterror( optinfo, MSG_MISSING, str );
            }

            if( type == OPTPARSE_POSIX_2 ) 
            { 
                char str[2] = {option[0]};
                return opterror( optinfo, MSG_INVALID, str ); 
            }

            return option[0];
        
        case OPTPARSE_OPTIONAL:
            optinfo->subopt = 0;
            optinfo->optind++;

            if (option[1]) 
            {
                optinfo->optarg = option + 1;
            } 
            else if( next && next[0] != '-' ) 
            {
                optinfo->optarg = next;
                optinfo->optind++;
		    } 
            else 
            {
                optinfo->optarg = 0;
		    }

            return option[0];
    } // switch...

    return 0;
}

#ifndef OPTPARSE_IMPLEMENT_NO_LONG

//==================================================================================================
static inline int is_longopt( char const *arg )
{
    return arg && arg[0] == '-' && arg[1] == '-' && arg[2] != '\0';
}

//==================================================================================================
static inline int longopts_end( optparse_longopt const *longopts, int i )
{
    return !longopts[i].name && !longopts[i].val;
}

//==================================================================================================
// Unlike strcmp(), handles options containing "=".
//==================================================================================================
static int longopts_match( char const *name, char const *option )
{
    if( name == 0 ) { return 0; }
    char const *a = option, *n = name;
    for( ; *a && *n && *a != '='; a++, n++ ) 
    { 
        if (*a != *n) { return 0; } 
    }
    return *n == '\0' && (*a == '\0' || *a == '=');
}

//==================================================================================================
// Return the part after "=", or return NULL.
//==================================================================================================
static char* longopts_arg( char *option )
{
    for( ; *option; option++ )
    {
        if( option[0] == '=' ) { return option + 1; }
    }
    return 0;
}

//==================================================================================================
static int long_fallback( optparse_info *optinfo,
                          char const *optstring,
                          optparse_longopt const *longopts,
                          int *longindex )
{
    int result = optparse_internal( optinfo, optstring );
    if( longindex != 0 ) 
    {
        *longindex = -1;
        if( result != -1 )
        {
            for( int i = 0; !longopts_end( longopts, i ); i++ )
            {
                if( longopts[i].val == optinfo->optopt ) 
                { 
                    *longindex = i;
                    break;
                }
            }
        }
    }
    return result;
}

//==================================================================================================
static int optparse_long_internal( optparse_info *optinfo,
                                   char const *optstring,
                                   optparse_longopt const *longopts,
                                   int *longindex )
{
    char *option = optinfo->argv[optinfo->optind];
    
    if( option == 0 ) 
    {
        return -1;
    } 
    else if( is_dashdash( option ) ) 
    {
        optinfo->optind++; /* consume "--" */
        return -1;
    } 
    else if( is_shortopt( option ) ) 
    {
        if( argtype( optstring, option[1] ) != OPTPARSE_POSIX_2 )
        {
            int result = long_fallback( optinfo, optstring, longopts, longindex );
            if( *longindex != -1 ) 
            { 
                optparse_longopt const *pLongOpt = &longopts[*longindex];
                if( pLongOpt->flag == 0 ) { return pLongOpt->val; }
                *pLongOpt->flag = pLongOpt->val;
                return 0;
            }
            return result;
        }
    } 
    else if( !is_longopt( option ) ) 
    {
        if( optinfo->permute ) 
        {
            int index = optinfo->optind;
            optinfo->optind++;
            int r = optparse_long_internal( optinfo, optstring, longopts, longindex );
            permute( optinfo, index );
            optinfo->optind--;
            return r;
        } 
        else 
        {
            return -1;
        }
    }

    /* Parse as long option. */
    optinfo->errmsg[0] = '\0';
    optinfo->optopt = 0;
    optinfo->optarg = 0;
    option += 2; /* skip "--" or "-W" */
    optinfo->optind++;

    for( int i = 0; !longopts_end( longopts, i ); i++ )
    {
        char const *name = longopts[i].name;
        if( longopts_match( name, option ) ) 
        {
            if( longindex ) { *longindex = i; }
            optinfo->optopt = longopts[i].val;
            char *arg = longopts_arg( option );

            if( arg != 0 ) 
            {
                if( longopts[i].argtype == OPTPARSE_NONE ) 
                {
                    return opterror( optinfo, MSG_TOOMANY, name );
                }
                optinfo->optarg = arg;
            }
            else if( longopts[i].argtype != OPTPARSE_NONE ) 
            {
                char *arg = optinfo->argv[optinfo->optind];
                if( arg && arg[0] != '-' )
                {
                    optinfo->optarg = arg;
                    optinfo->optind++;
                }
                else if( longopts[i].argtype == OPTPARSE_REQUIRED )
                {
                    return opterror( optinfo, MSG_MISSING, name ); 
                }
            }

            if( longopts[i].flag == 0 ) { return longopts[i].val; }
            *longopts[i].flag = longopts[i].val;

            return 0;
        } // if...
    } // for....

    return opterror( optinfo, MSG_INVALID, option );
}

#endif // ifndef OPTPARSE_IMPLEMENT_NO_LONG
/* */
