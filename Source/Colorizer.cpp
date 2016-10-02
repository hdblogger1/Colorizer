/***********************************************************************************************//**
\file    Colorizer.cpp
\author  hdaniel
\version $Id$

\brief Spawn a console process with colorized output for standard output handles.
 
\details



\history

- 15-Sep-2016: 
    hdaniel: Originated; 

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
#include <exception>
#include <string>
#include <sstream>

#include<windows.h>

#include "Utils\utils.h"
#include "Utils\conutils.h"

#define OPTPARSE_IMPLEMENT
#include "Utils\optparse.h"

#define PIPE_BUFFER_SIZE  255
#define CLOSEHANDLE(h)    if( h && h != INVALID_HANDLE_VALUE )  { ::CloseHandle( h ); h = 0; }

#define CR_STATUS_SUCCESS    0
#define CR_STATUS_ERROR     -1
#define CR_STATUS_WINAPI    -2
#define CR_STATUS_ABORTED   -3

BOOL  ResumeChildAndWaitForExit( PROCESS_INFORMATION& piChild, DWORD dwTimeoutOnceSignaled_ms );
DWORD WINAPI ReadAndPutOutputThread( LPVOID lpvThreadParam );
DWORD WINAPI GetAndWriteInputThread( LPVOID lpvThreadParam );

//==================================================================================================
enum EIoThreadType { StdOutRead, StdErrRead, StdInWrite, NUM_EIOTHREADTYPES };

//==================================================================================================
struct SOutputThreadInfo { HANDLE hReadPipe; EIoThreadType eType; };

//==================================================================================================
struct exit_exception : public std::exception 
{ 
    exit_exception( char const *szMsg, int code =CR_STATUS_ERROR ) 
        : std::exception( szMsg ), m_code( code ) { }

    virtual int code() const { return m_code; }

private:
    int m_code;
};

//=== GLOBALS ======================================================================================
HANDLE  g_hStdIn           = NULL; // Handle to parents std input.
BOOL    g_fRunThreads      = TRUE;
WORD    g_defaultAttr      = conutils::console.get_attribute();
WORD    g_soutColor        = g_defaultAttr;
WORD    g_serrColor        = g_defaultAttr;
bool    g_fLineMode        = false;
bool    g_fSkipLastEol     = false;

utils::Mutex       g_mutex;
utils::Event       g_abortChildEvent;
std::exception_ptr g_threadExceptions[NUM_EIOTHREADTYPES];

std::stringstream  g_ssErr;  // used for error message construction

//==================================================================================================
inline void ExitProgram( int code, std::string const &errMsg ) 
{ 
    g_fRunThreads = FALSE;
    throw exit_exception( errMsg.c_str(), code );
}

//==================================================================================================
inline void ThreadAbortChildProcess( EIoThreadType threadType, 
	                                 int errCode, std::string const &errMsg )
{
    g_threadExceptions[threadType] 
        = copy_exception( exit_exception( errMsg.c_str(), errCode ) );

    /* force child process to close which will cause the rest of our threads to exit when the child
     * side pipe handles disconnect.
	*/
    g_abortChildEvent.Signal();
}

//==================================================================================================
// Get the systems error message associated with dwErrorCode.
//==================================================================================================
std::string GetApiErrorString( DWORD dwErrorCode, std::string const& apiNameStr )
{
    LPVOID pFormatBuffer;

    ::FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, dwErrorCode,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPSTR)&pFormatBuffer, 0, NULL );

    /* remove the trailing \r\n from the returned message */
    char *pCrlf = ::strchr( (char*)pFormatBuffer, '\r' );
    if( pCrlf ) { *pCrlf = '\0'; }

    std::stringstream ss;
    ss << "[WINAPI - " << apiNameStr << "](" << (int)dwErrorCode << ") " << (char*)pFormatBuffer;

    ::LocalFree( pFormatBuffer );
	return ss.str();
}

//==================================================================================================
class CIoRedirectionManager
{
public:
	CIoRedirectionManager()
		: m_hStdOutWrite(0), m_hStdErrWrite(0), m_hStdInRead(0)
		, m_hStdOutRead(0) , m_hStdErrRead(0) , m_hStdInWrite(0)
	{ 
	}

	~CIoRedirectionManager() { DestroyPipeHandles(); }
	
	BOOL DestroyPipeHandles()
	{
		CLOSEHANDLE( m_hStdOutWrite );
		CLOSEHANDLE( m_hStdErrWrite );
		CLOSEHANDLE( m_hStdInRead );

		CLOSEHANDLE( m_hStdOutRead );
		CLOSEHANDLE( m_hStdErrRead );
		CLOSEHANDLE( m_hStdInWrite );

		return TRUE;
	}

	BOOL CreatePipeHandles()
	{
		HANDLE hStdOutTmp, hStdErrTmp, hStdInTmp;
		
		/* Set up the security attributes and create the child-side io pipe handles.
		*/
		SECURITY_ATTRIBUTES sa;
		sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
		sa.lpSecurityDescriptor = NULL;
		sa.bInheritHandle       = TRUE;

		if( !::CreatePipe( &hStdOutTmp, &m_hStdOutWrite, &sa, PIPE_BUFFER_SIZE )
			|| !::CreatePipe( &hStdErrTmp, &m_hStdErrWrite, &sa, PIPE_BUFFER_SIZE )
			|| !::CreatePipe( &m_hStdInRead, &hStdInTmp,    &sa, PIPE_BUFFER_SIZE ) ) 
		{ 
            g_ssErr.str("");
            g_ssErr << "Could not create chid-side pipe handles. " 
                    << GetApiErrorString( ::GetLastError(), "CreatePipe" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}

		/* Create copies of the parent-side pipe handles with Properties set to FALSE to prevent
		 * them from being inherited by the child process and making them uncloseable.
		*/
		HANDLE hProcess = ::GetCurrentProcess();
		if( !::DuplicateHandle( hProcess, hStdOutTmp, hProcess, &m_hStdOutRead, 0, FALSE, DUPLICATE_SAME_ACCESS )
			|| !::DuplicateHandle( hProcess, hStdErrTmp, hProcess, &m_hStdErrRead, 0, FALSE, DUPLICATE_SAME_ACCESS )
			|| !::DuplicateHandle( hProcess, hStdInTmp, hProcess, &m_hStdInWrite, 0, FALSE, DUPLICATE_SAME_ACCESS ) ) 
		{ 
            DWORD dwLastError = ::GetLastError();
			
            g_ssErr.str("");
            g_ssErr << "Could not create chid-side pipe handles. " 
                    << GetApiErrorString( dwLastError, "DuplicateHandle" );
			
            DestroyPipeHandles();
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}

		/* Now that a duplicate set of parent-side pipe handles have been created, close the 
		 * original handles to prevent them from being inhereted by the child process.
		*/
		CLOSEHANDLE( hStdOutTmp );
		CLOSEHANDLE( hStdErrTmp );
		CLOSEHANDLE( hStdInTmp );

		return TRUE;
	}

	BOOL CloseChildSidePipeHandles()
	{
		/* Close child-side pipe handles to make sure that no handles to the pipes are maintained
		 * in this process or else the pipe will not close when the child process exits and the 
		 * ReadFile will hang. 
		 *
		 * Note: This function should be called right after the child process has been created in a
		 * suspended state and before it is resumed.
		*/
		CLOSEHANDLE( m_hStdOutWrite );
		CLOSEHANDLE( m_hStdErrWrite );
		CLOSEHANDLE( m_hStdInRead );

		return TRUE;
	}

	HANDLE GetStdOutWrite() { return m_hStdOutWrite; }
	HANDLE GetStdErrWrite() { return m_hStdErrWrite; }
	HANDLE GetStdInRead()   { return m_hStdInRead; }
	
	HANDLE GetStdOutRead()  { return m_hStdOutRead; }
	HANDLE GetStdErrRead()  { return m_hStdErrRead; }
	HANDLE GetStdInWrite()  { return m_hStdInWrite; }

private:
	HANDLE m_hStdOutWrite, m_hStdErrWrite, m_hStdInRead;  // child-side handles
	HANDLE m_hStdOutRead,  m_hStdErrRead,  m_hStdInWrite; // parent-side handles
};

//==================================================================================================
void ShowHelp()
{
	std::vector<BYTE> helpData;

	DWORD numBytes = utils::CopyResource( NULL, L"TEXT", MAKEINTRESOURCE(101), helpData );
	if( numBytes )
	{
		helpData.push_back(0); // make sure we're NULL terminated
	
		std::cout << reinterpret_cast<const char *>( &helpData[0] );

		std::cout << "\nThe following are some usage examples:\n";
		std::cout << "\n";
		std::cout << "CMD$>set CR_OPTS=-o$05 -e$89\n\n";
		std::cout << "CMD$>cr cmd /c \"echo.LINE1&echo.LINE2&echo.LINE3\"\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE1" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE2" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE3" << conutils::reset << "\n";
		std::cout << "\n";
		std::cout << "CMD$>cr cmd /c \"1>&2 echo.LINE1&echo.LINE2&1>&2 echo.LINE3\"\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE1" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE2" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE3" << conutils::reset << "\n";
		std::cout << "\n";
		std::cout << "CMD$>set CR_OPTS=-o$05 -e$89 -l\n\n";
		std::cout << "CMD$>cr cmd /c \"1>&2 echo.LINE1&echo.LINE2&1>&2 echo.LINE3\"\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE1" << std::setw(73) << " " << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE2" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE3" << std::setw(73) << " " << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x89 ) << "     " << std::setw(73) << " " << conutils::reset << "\n";
		std::cout << "CMD$>set CR_OPTS=-o$05 -e$89 -l -s\n\n";
		std::cout << "CMD$>cr cmd /c \"1>&2 echo.LINE1&echo.LINE2&1>&2 echo.LINE3\"\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE1" << std::setw(73) << " " << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x05 ) << "LINE2" << conutils::reset << "\n";
		std::cout << conutils::setattr( 0x89 ) << "LINE3" << std::setw(73) << " " << conutils::reset << "\n";
		std::cout << "\n";
		std::cout << "CMD$>_\n";
	}
	else
	{
		std::cout << "Sorry, help not available!" << std::endl;
	}
}

//==================================================================================================
bool ProcessCommandLine( char const *options )
{
	bool fIntensify = false;
	
	int  argc;
	char **argv = utils::CommandLineToArgvA( options, &argc );

	int opt;
    optutils::optparse_info optInfo;
    optutils::optparse_init( &optInfo, argv );
	optInfo.optind = 0; /* we don't have a program name in argv[0] so start options at index 0 */
	while( (opt = optutils::optparse( &optInfo, "e:lo:s" )) != EOF )
	{
		switch( opt )
		{
			case 'e': { // stderr color
				int val;
				if( *optInfo.optarg == '$' ) { ::sscanf( &optInfo.optarg[1], "%x", &val ); }
				else                         { val = ::atoi( optInfo.optarg ); }
				g_serrColor = (WORD)(val & 0xFF );
			} break;

			case 'l':   // line mode
				g_fLineMode = true;
				break;

			case 'o': { // stdout color
				int val;
				if( *optInfo.optarg == '$' ) { ::sscanf( &optInfo.optarg[1], "%x", &val ); }
				else                         { val = ::atoi( optInfo.optarg ); }
				g_soutColor = (WORD)val;
			} break;

			case 's':   // skip coloring last newline
				g_fSkipLastEol = true;
				break;

			default:
				/* ignore invalid/unknown options */
				break;
		}
	}

	/* ignore any extra arguments */

    if( argv ) { ::LocalFree( argv ); }
	return true;
}

//==================================================================================================
int main( int argc, char **argv )
{
	CIoRedirectionManager ioMgr;
	PROCESS_INFORMATION   pi;
	SOutputThreadInfo     otiStdOut, otiStdErr;
    
	HANDLE  hThreads[3] = {0};
	int     errLevel = 0;

	/* If app is ran without options, display help and exit.
	*/
	if( argc == 1 ) { ShowHelp(); return 0; }

	try
	{
		/* Get std input handle so you can close it and force the ReadFile() to fail when you want
			* the input thread to exit.
		*/
		if( (g_hStdIn = ::GetStdHandle( STD_INPUT_HANDLE )) == INVALID_HANDLE_VALUE )
		{
			g_ssErr.str("");
			g_ssErr << "Could not get standard input handle. " 
					<< GetApiErrorString( ::GetLastError(), "GetStdHandle" );
			
			ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}

		/* Parse CR_OPTS environment variable and set global options accordingly. If CR_OPTS does not
		 * exist, add it to the environment and configure global option with the default values.
		*/
		char const *pCrOpts = ::getenv( "CR_OPTS" );
		if( NULL == pCrOpts )
		{
			char options[20];
			::sprintf( options, "CR_OPTS=-e%d", conutils::red );
			//::sprintf( options, "CR_OPTS=-e%d -l -s", FOREGROUND(conutils::red) | BACKGROUND(conutils::white) );
			::_putenv( options );
			pCrOpts = ::getenv( "CR_OPTS" );
		}
		ProcessCommandLine( pCrOpts );

		/* Create parent-side and client-side pipe handles
		*/
		ioMgr.CreatePipeHandles();

		/* Construct target application's command line by skipping over our application name.
		*/
		bool    fInQuote = false;
		wchar_t *cmdLineArgs = ::GetCommandLineW();

		if( *cmdLineArgs == L'\"' ) { fInQuote = true; cmdLineArgs++; }
		while( *cmdLineArgs )
		{
			if( fInQuote && *cmdLineArgs == L'\"' ) { cmdLineArgs++; fInQuote = false; }
			if( !fInQuote && *cmdLineArgs == L' ' ) 
			{ 
				while( *cmdLineArgs == L' ' ) { cmdLineArgs++; }
				break; 
			}
			cmdLineArgs++;
		}

		/* Launch the process were redirecting in suspended mode so we can start up the stdio
		 * monitoring threads before resuming it.
		*/
		STARTUPINFO si;
		::ZeroMemory( &si, sizeof(STARTUPINFO) );
		si.cb         = sizeof(STARTUPINFO);
		si.dwFlags    = STARTF_USESTDHANDLES;
		si.hStdOutput = ioMgr.GetStdOutWrite();
		si.hStdError  = ioMgr.GetStdErrWrite();
		si.hStdInput  = ioMgr.GetStdInRead();

		if( !::CreateProcessW( NULL, cmdLineArgs, NULL, NULL, TRUE, 
			                   CREATE_SUSPENDED, NULL, NULL, &si, &pi ) )
		{
            g_ssErr.str("");
            g_ssErr << "Could not create child process. " 
                    << GetApiErrorString( ::GetLastError(), "CreateProcess" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}

		/* Close child-side pipe handles as they are no longer needed in parent-side.
		*/
		ioMgr.CloseChildSidePipeHandles();

		/* Lauch the monitoring threads for child stdio. When the child process exits, the write
		 * end of the output pipes should close causing ReadFile() to return ERROR_BROKEN_PIPE,
		 * causing the output monitoring threads to exit. In order to signal that the input
		 * monitoring thread should shut down, we just need to close the std input handle gotten
		 * earilier. This causes ReadConsole to return with a nonzero (success) result with 
		 * lpNumberOfCharsRead set to zero. The subsequent WriteFile will then immediatly fail
		 * with ERROR_NO_DATA causing the thread to exit.
		*/
		DWORD dwThreadId;
		otiStdOut.hReadPipe = ioMgr.GetStdOutRead();
		otiStdOut.eType     = StdOutRead;
		otiStdErr.hReadPipe = ioMgr.GetStdErrRead();
		otiStdErr.eType     = StdErrRead;

        if( !(hThreads[1] = CreateThread( NULL, 0, ReadAndPutOutputThread, 
			                              (LPVOID)&otiStdOut, 0, &dwThreadId ))
            || !(hThreads[2] = CreateThread( NULL, 0, ReadAndPutOutputThread, 
			                                 (LPVOID)&otiStdErr, 0, &dwThreadId )) )
        {
            g_ssErr.str("");
            g_ssErr << "Could not create monitoring threads for child stdout/stderr. " 
                    << GetApiErrorString( ::GetLastError(), "CreateThread" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
        }

        /* Create the stdin thread last so its ReadFile() on stdin is not done untill the other
         * threads have been created successfully. 
        */
		hThreads[0] = CreateThread( NULL, 0, GetAndWriteInputThread, (LPVOID)ioMgr.GetStdInWrite(), 0, &dwThreadId );
		if( !hThreads[0] ) 
        { 
            g_ssErr.str("");
            g_ssErr << "Could not create monitoring thread for parent stdin. " 
                    << GetApiErrorString( ::GetLastError(), "CreateThread" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
        }

		/* Resume child process and wait for it to exit.
		*/
		ResumeChildAndWaitForExit( pi, 5000 );

		/* Redirection is complete so force the stdin thread to exit by closing the stdin handle.
		*/
		::CloseHandle( g_hStdIn );

		/* Signal threads to stop monitoring for child process i/o and wait for the threads to die.
		*/
		g_fRunThreads = FALSE;
		if( ::WaitForMultipleObjects( 3, &hThreads[0], TRUE, INFINITE ) == WAIT_FAILED )
		{ 
            g_ssErr.str("");
            g_ssErr << "Failed waiting for monitor threads to die. " 
                    << GetApiErrorString( ::GetLastError(), "WaitForMultipleObjects" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}

        /* Rethrow transported exceptions from threads
		*/
        for( int i = 0; i < NUM_EIOTHREADTYPES; i++ )
        {
            if( !(g_threadExceptions[i] == nullptr) ) 
            { 
                rethrow_exception( g_threadExceptions[i] ); 
            }
        }

        DWORD dwExitCode;
		if( 0 == ::GetExitCodeProcess( pi.hProcess, &dwExitCode ) )
		{
            g_ssErr.str("");
            g_ssErr << "Exit code for child processes in unavailiable. " 
                    << GetApiErrorString( ::GetLastError(), "GetExitCodeProcess" );
			
            ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
		}
        errLevel = (int)dwExitCode;
	}
	catch( exit_exception& except )
	{
		errLevel = 255; /* note: largest dos error level is 255 */
        std::cerr << "(" << except.code() << ") " << except.what() << std::endl;
	}

	conutils::console.set_attribute( g_defaultAttr );

	/* Cleanup any open handles for stdio, monitor threads or the child process.
	*/
	ioMgr.DestroyPipeHandles();
	for( int i=0; i < 3; i++ ) { CLOSEHANDLE( hThreads[i] ); }
	CLOSEHANDLE( pi.hThread );
	CLOSEHANDLE( pi.hProcess );

	return errLevel;
}

//==================================================================================================
// Callback used by the call to EnumWindows() in ResumeChildAndWaitForExit.
//==================================================================================================
BOOL CALLBACK TerminateChildEnum( HWND hwnd, LPARAM lParam )
{
    DWORD dwID ;
    ::GetWindowThreadProcessId( hwnd, &dwID ) ;
    if( dwID == (DWORD)lParam ) { ::PostMessage( hwnd, WM_CLOSE, 0, 0 ); }

    return TRUE ;
}

//==================================================================================================
// Resume child process and wait for it to exit. If an error in one of our monitoring threads
// occurs, an abort event will be signaled indicating we should request the child process exit. If
// this request fails try force the termination of the child process. 
//
// This function always returns TRUE. If the child process cannot be resumed or it can't force the
// child process to exit when requested, an exit_exception is thrown.
//==================================================================================================
BOOL ResumeChildAndWaitForExit( PROCESS_INFORMATION& piChild, DWORD dwTimeoutOnceSignaled_ms )
{
	HANDLE hWaitHandles[2];

	hWaitHandles[0] = g_abortChildEvent;
	hWaitHandles[1] = piChild.hThread;

	if( (DWORD)-1 == ::ResumeThread( piChild.hThread ) ) 
    { 
        g_ssErr.str("");
        g_ssErr << "Could not resume child process. " 
                << GetApiErrorString( ::GetLastError(), "ResumeThread" );
			
        ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
    }
	
	DWORD dwStatus = ::WaitForMultipleObjects( 2, hWaitHandles, FALSE, INFINITE );
	if( dwStatus == WAIT_FAILED ) 
    { 
        g_ssErr.str("");
        g_ssErr << "Failed waiting for child process to exit. " 
                << GetApiErrorString( ::GetLastError(), "WaitForMultipleObjects" );
			
        ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
    }

	if( dwStatus == WAIT_OBJECT_0 )
	{
		/* Post WM_CLOSE to all windows whose PID matches our child processes PID
		*/
		::EnumWindows((WNDENUMPROC)TerminateChildEnum, (LPARAM)piChild.dwProcessId );
		if( WAIT_OBJECT_0 != ::WaitForSingleObject( piChild.hThread, dwTimeoutOnceSignaled_ms ) )
		{
            if( !::TerminateProcess( piChild.hProcess, 0 ) )
            {
                g_ssErr.str("");
                g_ssErr << "Could not force terminate child process. " 
                        << GetApiErrorString( ::GetLastError(), "TerminateProcess" );
			
                ExitProgram( CR_STATUS_WINAPI, g_ssErr.str() );
            }
		} 
	}

	return TRUE;
}

//==================================================================================================
//
//==================================================================================================
char* lineTok( char** begin )
{
    if( !begin || !*begin || !**begin ) { return 0; }
    
    char* p   = *begin;  
    
    /* skip leading newline, adjust begin if \r\r\n is encountered */
    if( p[0] == '\r' ) 
    {
        if( p[1] == '\r' && p[2] == '\n' ) { p+=3; (*begin)++; }
        else if( p[1] == '\n' )            { p+=2; }
        else                               { p++; }
    }

    /* find trailing newline */
    while( *p )
    {
        if( p[0] == '\r' ) 
		{ 
			if( p[1] == '\n' || p[1] == '\r' && p[2] == '\n' ) { break; } 
		}
        p++;
    }
    
    return p;
}

//==================================================================================================
// Monitors the child process and relay output to the consoles stdout/stderr. The thread ends when 
// the global g_fRunThreads variable is set to false after the main thread has detected that the 
// child process has exited.
//
// If the thread is waiting on a ReadFile() operation to complete, the termination of the child
// process will result in the child-side pipe handles being closed. This in-turn will cause the
// blocking ReadFile() operation to complete with a ERROR_BROKEN_PIPE error and the thread to exit.
//
// There's a slight hickup when writing to stdout/stderr in that depending on how the child 
// process writes to its stdout/stderr streams, the ReadFile from this thread could return before
// all the text from the child stream has be written out. In this case, the thread may have to
// execute multiple calls to ReadFile to get the remainder of the data. The hickup is where the
// other thread has also returned from ReadFile and takes ownership of the global mutex before this
// thread has had a chance to finish reading all of the data from the stream. When this happens the
// output from the two stream may get mixed up while being sent to the console. The only way I can
// think to prevent this situation from occuring is to have a single thread which issues 
// PeekNamedPipe/ReadFile on each stream and only switch which stream is being read from when it 
// has determined that more data is available.
//==================================================================================================
DWORD WINAPI ReadAndPutOutputThread( LPVOID lpvThreadParam )
{
    BYTE lpBuffer[PIPE_BUFFER_SIZE];
    DWORD nBytesRead, nBytesWritten;

	WORD outputAttr;
	WORD lineAttr;
    
	SOutputThreadInfo *pOti = (SOutputThreadInfo*)lpvThreadParam;

	HANDLE hPipeRead = pOti->hReadPipe;
	HANDLE hStdWrite = ::GetStdHandle( STD_OUTPUT_HANDLE );

	if( pOti->eType == StdOutRead )      { outputAttr = g_soutColor; }
	else if( pOti->eType == StdErrRead ) { outputAttr = g_serrColor; }

	if( g_fLineMode ) { lineAttr = outputAttr; }
	else              { lineAttr = g_defaultAttr; }

	if( pOti->eType == StdOutRead )      
	{ 
		int i = 0; 
	}

	DWORD nBytesAvailable = 0;
    while( 1 )
    {
		/* Check to see if we have any more data to read. Need to do this so thread doesn't exit 
		 * untill all data has been read from pipe. 
		*/
		::PeekNamedPipe( hPipeRead, NULL, NULL, NULL, &nBytesAvailable, NULL );		
        if( !g_fRunThreads && !nBytesAvailable ) { break; }

        nBytesRead = 0;
		if( !::ReadFile( hPipeRead, lpBuffer, sizeof(lpBuffer)-1, &nBytesRead, NULL ) || !nBytesRead )
        {
            /* ERROR_BROKEN_PIPE means child-side pipe handle has been closed and is the normal
			 * exit path.
			*/
            DWORD dwLastError = ::GetLastError();
			if( dwLastError != ERROR_BROKEN_PIPE )
            { 
                g_ssErr.str("");
                g_ssErr << "Could not read from output side of " 
                        << ((pOti->eType == StdOutRead) ? "StdOutRead" : "StdErrRead") << " pipe. " 
                        << GetApiErrorString( dwLastError, "ReadFile" );
                
                ThreadAbortChildProcess( pOti->eType, CR_STATUS_WINAPI, g_ssErr.str() );
            }
            break;
        }
		lpBuffer[nBytesRead] = 0;

		/* Write lpBuffer to the console one line at a time, where the line termination characters
		 * of the current line are written with the next line. If there is no 'next line' then just
		 * the line termination characters are written.
		 *
		 * After the text for the current line has been written, the background of the remainder of
		 * the line is set based on g_fLineMode. If true, the current background attribute is used,
		 * if false, the default background attribute. On the last line, where just the termination
		 * characters are written, the background attribute is set based on g_fSkipLastEol. If 
		 * g_fSkipLastEol is true, the background attribute is set to the default background 
		 * attribute. if g_fSkipLastEol is false, the current background attribute is used.
		*/
		g_mutex.Enter();
		conutils::console.set_attribute( outputAttr );

		BYTE *begin = &lpBuffer[0];
		BYTE *end = (BYTE*)lineTok( (char**)&begin );

        while( end != NULL )
        {
			if( !::WriteFile( hStdWrite, begin, (end - begin), &nBytesWritten, NULL ) )
			{
				g_ssErr.str("");
				g_ssErr << "Could not write to " 
						<< ((pOti->eType == StdOutRead) ? "stdout" : "stderr") << ". " 
						<< GetApiErrorString( ::GetLastError(), "WriteFile" );
                
				ThreadAbortChildProcess( pOti->eType, CR_STATUS_WINAPI, g_ssErr.str() );
			}
			begin = end;
            end = (BYTE*)lineTok( (char**)&begin );
			
			conutils::console.clear_eol( lineAttr );
            
			if( end == NULL )
			{
				if( nBytesWritten == 2 && g_fSkipLastEol )
					{ conutils::console.clear_eol( g_defaultAttr ); }
				else
					{ conutils::console.clear_eol( lineAttr ); }
			}
        }

		conutils::console.set_attribute( g_defaultAttr );
		g_mutex.Leave();
    }

	return 1;
}

//==================================================================================================
// Monitors the console for input and relay std input to the child process. The thread ends when 
// the global g_fRunThreads variable is set to false. 
//
// Because the thread may be waiting on ReadFile() to return when the main thread detects that the
// child process has exited, the main thread will also close the handle to its std input. When the 
// std input handle is closed, any pending ReadFile() operation on that handle will complete 
// setting nBytesRead to 0. This in turn causes the subsequent WriteFile() to fail with
// ERROR_NO_DATA and allows the thread to exit.
//==================================================================================================
DWORD WINAPI GetAndWriteInputThread( LPVOID lpvThreadParam )
{
    BYTE read_buff[PIPE_BUFFER_SIZE];
    DWORD nBytesRead,nBytesWritten;
	HANDLE hPipeWrite = (HANDLE)lpvThreadParam;

    /* Get input from our console and send it to child through the pipe.
	*/
    while( g_fRunThreads )
    {
        if( !::ReadFile( g_hStdIn, read_buff, sizeof(read_buff) - 1, &nBytesRead, NULL ) )
		{ 
            g_ssErr.str("");
            g_ssErr << "Could not read from stdin. " 
                    << GetApiErrorString( ::GetLastError(), "ReadFile" );
                
            ThreadAbortChildProcess( StdInWrite, CR_STATUS_WINAPI, g_ssErr.str() );
            break;
        }
        read_buff[nBytesRead] = 0;

        if( !::WriteFile( hPipeWrite, read_buff, nBytesRead, &nBytesWritten, NULL ) )
        {
            /* ERROR_NO_DATA means pipe was closed and is the threads normal exit path.
			*/
			DWORD dwLastError = ::GetLastError();
            if( dwLastError != ERROR_NO_DATA ) 
            { 
                g_ssErr.str("");
                g_ssErr << "Could not write to input side of StdInWrite pipe. " 
                        << GetApiErrorString( dwLastError, "WriteFile" );
                
                ThreadAbortChildProcess( StdInWrite, CR_STATUS_WINAPI, g_ssErr.str() );
            }
            break;
        }
    }

    return 1;
}

/* */