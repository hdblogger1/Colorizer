/***********************************************************************************************//**
\file    utils.h
\author  hdaniel
\version $Id$

\brief Miscellaneous functions, class and definitions.
 
\details

Miscellaneous functions, class and definitions.

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
#ifndef _utils_h_
#define _utils_h_

#ifndef WINDOWS_MEAN_AND_LEAN
#  define WINDOWS_MEAN_AND_LEAN
#endif
#include <Windows.h>

#include <vector>
#include <string>

namespace utils
{

//==================================================================================================
// Visual Studio 2010 doesn't support std::mutex.
//==================================================================================================
class Mutex
{
public:
         Mutex( void ) { ::InitializeCriticalSection( &m_critSection ); }
         ~Mutex( void ){ ::DeleteCriticalSection( &m_critSection ); }
	void Enter( void ) { ::EnterCriticalSection( &m_critSection ); }
    void Leave( void ) { ::LeaveCriticalSection( &m_critSection ); }

private:
	CRITICAL_SECTION m_critSection;
};

//==================================================================================================
class Event
{
public:
	Event( BOOL fManualReset =TRUE ) {
		m_event = ::CreateEvent( (LPSECURITY_ATTRIBUTES)0, fManualReset, FALSE, NULL );
	}
	~Event() { ::CloseHandle( m_event ); }

	operator HANDLE() { return m_event; }
	BOOL     Signal() { return m_event ? ::SetEvent( m_event ) : FALSE; }
	BOOL     Reset()  { return m_event ? ::ResetEvent( m_event ) : FALSE; }

private:
	HANDLE m_event;
};

//==================================================================================================
// Some basic string utilities every application should have.
//==================================================================================================
inline std::wstring str2wstr( std::string const &string_in ) 
{
	std::vector<wchar_t> buffer;
    buffer.resize( string_in.length() + 1 );
	::MultiByteToWideChar( CP_ACP, 0, string_in.c_str(), -1, &buffer[0], (int)string_in.length() );
	std::wstring wstring_out( &buffer[0] );
	return wstring_out;
}

//--------------------------------------------------------------------------------------------------
inline std::string wstr2str( std::wstring const &wstring_in ) 
{
	std::vector<char> buffer; 
    buffer.resize( wstring_in.length() + 1 );
	::WideCharToMultiByte( CP_ACP, 0, wstring_in.c_str(), -1, &buffer[0], 
		                   (int)wstring_in.length(), NULL, NULL );
	std::string string_out( &buffer[0] );
	return string_out;
}

//--------------------------------------------------------------------------------------------------
inline std::wstring str2wstrU( std::string const &str )
{
    std::wstring wstr;
    int wstrSize = ::MultiByteToWideChar( CP_UTF8, 0, str.c_str(), -1, 0, 0 );
    if( wstrSize > 0 )
    {
        std::vector<wchar_t> buffer( wstrSize );
        ::MultiByteToWideChar( CP_UTF8, 0, str.c_str(), -1, &buffer[0], wstrSize );
        wstr.assign( buffer.begin(), buffer.end() - 1 );
    }
    return wstr;
}

//--------------------------------------------------------------------------------------------------
inline std::string wstr2strU( std::wstring const &wstr )
{
    std::string str;
    int strSize = ::WideCharToMultiByte( CP_UTF8, 0, wstr.c_str(), -1, 0, 0, 0, 0 );
    if( strSize > 0 )
    {
        std::vector<char> buffer( strSize );
        ::WideCharToMultiByte( CP_UTF8, 0, wstr.c_str(), -1, &buffer[0], strSize, 0, 0 );
        str.assign( buffer.begin(), buffer.end() - 1 );
    }
    return str;
}

//--------------------------------------------------------------------------------------------------
inline std::string strvfmt( char const *fmt, va_list args )
{
	int iResult = -1, iLen = 255;
	std::vector<char> vBuffer;
	while( iResult == -1 ) 
    {
		vBuffer.assign( iLen+1, 0 );
        iResult = ::_vsnprintf_s( &vBuffer[0], iLen, iLen-1, fmt, args );
		iLen *= 2;
	}
    std::string ret;
	ret.assign( &vBuffer[0] );
    return ret;
}
//--------------------------------------------------------------------------------------------------
inline std::string strfmt( char const *fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    std::string fmtStr = strvfmt( fmt, args );
    va_end( args );
    return fmtStr;
}

//==================================================================================================
// Microsoft doesn't provide an equivelent narrow ('A') version of CommandLineToArgW.
//==================================================================================================
inline LPSTR* CommandLineToArgvA( LPCSTR lpCmdLine, int *pNumArgs )
{
    if( sizeof(CHAR) > sizeof(WCHAR) ) { return NULL; }

    std::wstring strCmdLineW = str2wstr( lpCmdLine );
    
    int numArgsW = 0;
    LPWSTR *argvW = ::CommandLineToArgvW( strCmdLineW.c_str(), &numArgsW );
    if( !argvW ) { return NULL; }

    LPSTR *argvA = reinterpret_cast<LPSTR*>( argvW );
    for( int i = 0; i < numArgsW; i++ )
    {
        std::string strCmdLineA = wstr2str( argvW[i] );

        /* overwrite wide character string with ascii string. As long as sizeof(CHAR) <=
           sizeof(WCHAR) there shouldn't be any problems 
        */
        ::strcpy( argvA[i], strCmdLineA.c_str() );
    }

    *pNumArgs = numArgsW;

    /* client is still responsible for calling ::LocalFree() on returned argument buffer 
	*/
    return argvA;
}

//==================================================================================================
// Get the handle of the module you're running in without any a-priori knowledge.
// See the following link for details: 
//      http://www.dotnet247.com/247reference/msgs/13/65259.aspxinline 
//==================================================================================================
// External definition used by GetCurrentModule()
#if _MSC_VER >= 1300 // for VC 7.0
    // from ATL 7.0 sources
#   ifndef _delayimp_h
extern "C" IMAGE_DOS_HEADER __ImageBase;
#   endif
#endif
//--------------------------------------------------------------------------------------------------
inline HMODULE GetCurrentModule()
{
#if _MSC_VER < 1300
    // earlier than .NET compiler (VC 6.0)
    MEMORY_BASIC_INFORMATION mbi;
    static int dummy;
    VirtualQuery( &dummy, &mbi, sizeof( mbi ) );
     
    return reinterpret_cast<HMODULE>(mbi.AllocationBase);
     
#else
    // >= VC 7.0 (from ATL 7.0 sources)
    return reinterpret_cast<HMODULE>( &__ImageBase );

#endif
}

//==================================================================================================
inline LPVOID GetResourceData( HMODULE hLibrary, LPCTSTR lpType, LPCTSTR lpName, DWORD* pResSize )
{
    LPVOID pvData;
    
    if( hLibrary == NULL || hLibrary == INVALID_HANDLE_VALUE ) { return NULL; }

	// First we will try and find the resource we wish to get the data contents pointer to
	HRSRC hResource = ::FindResource( hLibrary, lpName, lpType );
    if( hResource == NULL ) { return NULL; }

	// If it was found, we can get a handle to it
	HGLOBAL hData = LoadResource( hLibrary, hResource );
	if( hData == NULL) { return NULL; }

	// Lock the resource to get the pointer to its contents
	pvData = ::LockResource( hData );
	if( pvData == NULL ) { return NULL; }

	// We have a handle to it, so get its size
    if( pResSize ) { *pResSize = ::SizeofResource( hLibrary, hResource ); }

	// Success; data contains a pointer to the resources data
	return pvData;
}

//==================================================================================================
inline DWORD CopyResource( LPCTSTR szResFilepath, LPCTSTR lpType, 
	                       LPCTSTR lpName, std::vector<BYTE>& resData )
{
    HMODULE hLibrary;
    DWORD   dwResSize;

    resData.clear();

    // If szResFilepath is NULL use current module
    if( szResFilepath == NULL ) 
		{ hLibrary = GetCurrentModule(); }
    else                        
		{ hLibrary = ::LoadLibraryEx( szResFilepath, 0, LOAD_LIBRARY_AS_DATAFILE ); }

    if( hLibrary == NULL ) { return 0; }

    LPVOID pvData = GetResourceData( hLibrary, lpType, lpName, &dwResSize );
    if( pvData )
    {
        resData.resize( dwResSize, 0 );
        ::memcpy( &resData[0], pvData, dwResSize );
    }

    // Close library
    if( hLibrary ) { ::FreeLibrary( hLibrary ); }

    return resData.size();
}

} // namespace utils

#endif // ifndef _utils_h_
/* */
