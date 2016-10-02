/***********************************************************************************************//**
\file    conutils.h
\author  hdaniel
\version $Id$

\brief iostream manipulators for colored console output. 
 
\details

The following code adds color manipulators to the standard cout/wcout C++ iostream interface used
for console output. In addition, the conutils::console object can be accessed directly to effect
color changes for text output using the more traditional printf() C-style functions.

\history

- 15-Sep-2016: 
	hdaniel: Reformated and moved to conutils namespace.
	hdaniel: Reworked original sources and added manipulators: cleareol, invert, reset, 
	         bg_bright and fg_bright.
	hdaniel: Renamed and reduced the number of color variable constants. There are now two
		        seperate functions for setting foreground and background colors.
    hdaniel: Originated; Based on code by Jaded Hobo, downloaded from: 
		        <http://www.codeproject.com/Articles/9130/Add-Color-to-Console-Projects>

\license

This file contains "Derivative Work" base on "Work" licensed under the Code Project Open License
(CPOL) 1.02. To view a copy of this license visit <http://www.codeproject.com/info/cpol10.aspx>.
***************************************************************************************************/
#ifndef _conutils_h_
#define _conutils_h_

#ifndef WINDOWS_MEAN_AND_LEAN
#  define WINDOWS_MEAN_AND_LEAN
#endif
#include <windows.h>

#include <iostream>
#include <iomanip>

namespace conutils
{
    static const WORD bgMask(BACKGROUND_BLUE|BACKGROUND_GREEN|BACKGROUND_RED|BACKGROUND_INTENSITY);
    static const WORD fgMask(FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED|FOREGROUND_INTENSITY);

    static const WORD black  ( 0 );                              // 0x00
    static const WORD blue   ( FOREGROUND_BLUE  );               // 0x01
    static const WORD green  ( FOREGROUND_GREEN );               // 0x02
    static const WORD cyan   ( green   | blue );                 // 0x03
    static const WORD red    ( FOREGROUND_RED   );               // 0x04
    static const WORD magenta( red     | blue );                 // 0x05
    static const WORD yellow ( red     | green );                // 0x06
    static const WORD white  ( red     | green | blue );         // 0x07
    static const WORD gray   ( black   | FOREGROUND_INTENSITY ); // 0x08
       
    static class _tag_console
    {
        public:
            _tag_console() 
			{ 
			    /* According to MSDN, the handles returned by GetStdHandle on stdout 
                 * (STD_OUTPUT_HANDLE) or stderr (STD_ERROR_HANDLE) point to the same active screen
                 * buffer for the console.
			    */
				m_hConsole = ::GetStdHandle( STD_OUTPUT_HANDLE );
				_UpdateConsoleInfo();
				m_wDefAttr = m_csbi.wAttributes;
			}
            
			void set_default_attribute( WORD defAttr ) { m_wDefAttr = defAttr; }

			WORD get_default_attribute() { return m_wDefAttr; }

            void clear()
            {
                COORD coordScreen = { 0, 0 };
                _UpdateConsoleInfo(); 
                ::FillConsoleOutputCharacter( m_hConsole, ' ', m_dwConSize, coordScreen, &m_cCharsWritten ); 
                ::FillConsoleOutputAttribute( m_hConsole, m_csbi.wAttributes, m_dwConSize, coordScreen, &m_cCharsWritten ); 
                ::SetConsoleCursorPosition( m_hConsole, coordScreen ); 
            }
            
			void clear_eol()
            {
				_UpdateConsoleInfo();
				COORD coordStart = m_csbi.dwCursorPosition;
				DWORD nchars     = m_csbi.dwSize.X - coordStart.X;

				::FillConsoleOutputCharacter( m_hConsole, ' ', nchars, coordStart, &m_cCharsWritten );
 				::FillConsoleOutputAttribute( m_hConsole, m_csbi.wAttributes, nchars, coordStart, &m_cCharsWritten );
			}
            
			void clear_eol( WORD bgColor )
            {
				_UpdateConsoleInfo();
				COORD coordStart = m_csbi.dwCursorPosition;
				DWORD nchars     = m_csbi.dwSize.X - coordStart.X;

				bgColor &= bgMask;
				::FillConsoleOutputCharacter( m_hConsole, ' ', nchars, coordStart, &m_cCharsWritten );
 				::FillConsoleOutputAttribute( m_hConsole, bgColor, nchars, coordStart, &m_cCharsWritten );
			}

			void set_attribute( WORD attr )
            {
                _UpdateConsoleInfo();
                m_csbi.wAttributes = attr; 
                ::SetConsoleTextAttribute( m_hConsole, m_csbi.wAttributes );
            }
            
			void set_attribute( WORD attr, WORD mask )
            {
                _UpdateConsoleInfo();
                m_csbi.wAttributes &= mask; 
                m_csbi.wAttributes |= attr; 
                ::SetConsoleTextAttribute( m_hConsole, m_csbi.wAttributes );
            }

			WORD get_attribute()
            {
                _UpdateConsoleInfo();
                return m_csbi.wAttributes;
            }

			void reset() { set_attribute( m_wDefAttr ); }

			void invert()
			{
                _UpdateConsoleInfo();
                m_csbi.wAttributes = ((m_csbi.wAttributes & 0x0F) << 4) | ((m_csbi.wAttributes & 0xF0) >> 4); 
                ::SetConsoleTextAttribute( m_hConsole, m_csbi.wAttributes );
			}
			
			void bright( bool fForeground =true )
			{
                _UpdateConsoleInfo();
                m_csbi.wAttributes |= fForeground ? FOREGROUND_INTENSITY : BACKGROUND_INTENSITY; 
                ::SetConsoleTextAttribute( m_hConsole, m_csbi.wAttributes );
			}
            
        private:
            void _UpdateConsoleInfo()
            {
                ::GetConsoleScreenBufferInfo( m_hConsole, &m_csbi );
                m_dwConSize = m_csbi.dwSize.X * m_csbi.dwSize.Y; 
            }
                
            HANDLE                      m_hConsole;
            DWORD                       m_cCharsWritten; 
            CONSOLE_SCREEN_BUFFER_INFO  m_csbi; 
            DWORD                       m_dwConSize;
			WORD                        m_wDefAttr;
    } console;
    
    // attribute/color setting helpers
    struct _tag_setattr { _tag_setattr( WORD arg ) : _arg(arg) { } WORD _arg; };
    struct _tag_setfgnd { _tag_setfgnd( WORD arg ) : _arg(arg) { } WORD _arg; };
    struct _tag_setbgnd { _tag_setbgnd( WORD arg ) : _arg(arg) { } WORD _arg; };

    inline _tag_setattr setattr( WORD attr ) { return _tag_setattr( attr ); }
	inline _tag_setfgnd setfgnd( WORD fgColor ) { return _tag_setfgnd( fgColor ); }
	inline _tag_setbgnd setbgnd( WORD bgColor ) { return _tag_setbgnd( bgColor ); }

    // narrow manipulators
    inline std::ostream& operator<<( std::ostream& os, _tag_setattr& m )
        { console.set_attribute( m._arg ); return os; }
        
    inline std::ostream& operator<<( std::ostream& os, _tag_setfgnd& m )
        { console.set_attribute( m._arg, bgMask ); return os; }

    inline std::ostream& operator<<( std::ostream& os, _tag_setbgnd& m )
        { console.set_attribute( m._arg << 4, fgMask ); return os; }

	inline std::ostream& clear( std::ostream& os )    { os.flush(); console.clear(); return os; };
    inline std::ostream& cleareol( std::ostream& os ) { os.flush(); console.clear_eol(); return os; };
    inline std::ostream& invert( std::ostream& os )   { os.flush(); console.invert(); return os; };
    inline std::ostream& reset( std::ostream& os )    { os.flush(); console.reset(); return os; };
                      
    inline std::ostream& fg_bright( std::ostream& os )  { os.flush(); console.bright( true ); return os; };
    inline std::ostream& fg_red( std::ostream& os )     { os << std::flush << setfgnd( red ); return os; }
    inline std::ostream& fg_green( std::ostream& os )   { os << std::flush << setfgnd( green ); return os; } 
    inline std::ostream& fg_blue( std::ostream& os )    { os << std::flush << setfgnd( blue ); return os; }   
    inline std::ostream& fg_white( std::ostream& os )   { os << std::flush << setfgnd( white ); return os; }
    inline std::ostream& fg_cyan( std::ostream& os )    { os << std::flush << setfgnd( cyan ); return os; }  
    inline std::ostream& fg_magenta( std::ostream& os ) { os << std::flush << setfgnd( magenta ); return os; }
    inline std::ostream& fg_yellow( std::ostream& os )  { os << std::flush << setfgnd( yellow ); return os; }
    inline std::ostream& fg_black( std::ostream& os )   { os << std::flush << setfgnd( black ); return os; }
    inline std::ostream& fg_gray( std::ostream& os )    { os << std::flush << setfgnd( gray ); return os; }   
    
    inline std::ostream& bg_bright( std::ostream& os )  { os.flush(); console.bright( false ); return os; };
	inline std::ostream& bg_red( std::ostream& os )     { os << std::flush << setbgnd( red ); return os; } 
    inline std::ostream& bg_green( std::ostream& os )   { os << std::flush << setbgnd( green ); return os; }
    inline std::ostream& bg_blue( std::ostream& os )    { os << std::flush << setbgnd( blue ); return os; } 
    inline std::ostream& bg_white( std::ostream& os )   { os << std::flush << setbgnd( white ); return os; }   
    inline std::ostream& bg_cyan( std::ostream& os )    { os << std::flush << setbgnd( cyan ); return os; }  
    inline std::ostream& bg_magenta( std::ostream& os ) { os << std::flush << setbgnd( magenta ); return os; }
    inline std::ostream& bg_yellow( std::ostream& os )  { os << std::flush << setbgnd( yellow ); return os; } 
    inline std::ostream& bg_black( std::ostream& os )   { os << std::flush << setbgnd( black ); return os; }
    inline std::ostream& bg_gray( std::ostream& os )    { os << std::flush << setbgnd( gray ); return os; }
        
    // wide manipulators
    inline std::wostream& operator<<( std::wostream& os, _tag_setattr& m )
        { console.set_attribute( m._arg ); return os; }
        
    inline std::wostream& operator<<( std::wostream& os, _tag_setfgnd& m )
        { console.set_attribute( m._arg, fgMask ); return os; }

    inline std::wostream& operator<<( std::wostream& os, _tag_setbgnd& m )
        { console.set_attribute( m._arg << 4, bgMask ); return os; }

    inline std::wostream& clear( std::wostream& os )    { os.flush(); console.clear(); return os; };
    inline std::wostream& cleareol( std::wostream& os ) { os.flush(); console.clear_eol(); return os; };
    inline std::wostream& invert( std::wostream& os )   { os.flush(); console.invert(); return os; };
    inline std::wostream& reset( std::wostream& os )    { os.flush(); console.reset(); return os; };

    inline std::wostream& fg_bright( std::wostream& os )  { os.flush(); console.bright( true ); return os; };
    inline std::wostream& fg_red( std::wostream& os )     { os << std::flush << setfgnd( red ); return os; }
    inline std::wostream& fg_green( std::wostream& os )   { os << std::flush << setfgnd( green ); return os; } 
    inline std::wostream& fg_blue( std::wostream& os )    { os << std::flush << setfgnd( blue ); return os; }   
    inline std::wostream& fg_white( std::wostream& os )   { os << std::flush << setfgnd( white ); return os; }
    inline std::wostream& fg_cyan( std::wostream& os )    { os << std::flush << setfgnd( cyan ); return os; }  
    inline std::wostream& fg_magenta( std::wostream& os ) { os << std::flush << setfgnd( magenta ); return os; }
    inline std::wostream& fg_yellow( std::wostream& os )  { os << std::flush << setfgnd( yellow ); return os; }
    inline std::wostream& fg_black( std::wostream& os )   { os << std::flush << setfgnd( black ); return os; }
    inline std::wostream& fg_gray( std::wostream& os )    { os << std::flush << setfgnd( gray ); return os; }   
    
    inline std::wostream& bg_bright( std::wostream& os )  { os.flush(); console.bright( false ); return os; };
	inline std::wostream& bg_red( std::wostream& os )     { os << std::flush << setbgnd( red ); return os; } 
    inline std::wostream& bg_green( std::wostream& os )   { os << std::flush << setbgnd( green ); return os; }
    inline std::wostream& bg_blue( std::wostream& os )    { os << std::flush << setbgnd( blue ); return os; } 
    inline std::wostream& bg_white( std::wostream& os )   { os << std::flush << setbgnd( white ); return os; }   
    inline std::wostream& bg_cyan( std::wostream& os )    { os << std::flush << setbgnd( cyan ); return os; }  
    inline std::wostream& bg_magenta( std::wostream& os ) { os << std::flush << setbgnd( magenta ); return os; }
    inline std::wostream& bg_yellow( std::wostream& os )  { os << std::flush << setbgnd( yellow ); return os; } 
    inline std::wostream& bg_black( std::wostream& os )   { os << std::flush << setbgnd( black ); return os; }
    inline std::wostream& bg_gray( std::wostream& os )    { os << std::flush << setbgnd( gray ); return os; }

} // namespace conutils
#endif //ifndef _conutils_h_

