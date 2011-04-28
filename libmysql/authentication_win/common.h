/* Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef COMMON_H
#define COMMON_H

#include <my_global.h>
#include <windows.h>
#include <sspi.h>              // for CtxtHandle
#include <mysql/plugin_auth.h> // for MYSQL_PLUGIN_VIO

/// Maximum length of the target service name.
#define MAX_SERVICE_NAME_LENGTH  1024


/** Debugging and error reporting infrastructure ***************************/

/*
  Note: We use plugin local logging and error reporting mechanisms until
  WL#2940 (plugin service: error reporting) is available.
*/

#undef INFO
#undef WARNING
#undef ERROR

struct error_log_level
{
  typedef enum {INFO, WARNING, ERROR}  type;
};

#undef  DBUG_ASSERT
#ifndef DBUG_OFF
#define DBUG_ASSERT(X)  assert(X)
#else
#define DBUG_ASSERT(X)  do {} while (0)
#endif

extern "C" int opt_auth_win_client_log;

/*
  Note1: Double level of indirection in definition of DBUG_PRINT allows to
  temporary redefine or disable DBUG_PRINT macro and then easily return to
  the original definition (in terms of DBUG_PRINT_DO).

  Note2: DBUG_PRINT() can use printf-like format string like this:

    DBUG_PRINT(Keyword, ("format string", args));

  The implementation should handle it correctly. Currently it is passed 
  to fprintf() (see debug_msg() function).
*/

#ifndef DBUG_OFF
#define DBUG_PRINT_DO(Keyword, Msg) \
  do { \
    if (2 > opt_auth_win_client_log) break; \
    fprintf(stderr, "winauth: %s: ", Keyword); \
    debug_msg Msg; \
  } while (0)
#else
#define DBUG_PRINT_DO(K, M)  do {} while (0)
#endif

#undef  DBUG_PRINT
#define DBUG_PRINT(Keyword, Msg)  DBUG_PRINT_DO(Keyword, Msg)

/*
  If DEBUG_ERROR_LOG is defined then error logging happens only
  in debug-copiled code. Otherwise ERROR_LOG() expands to 
  error_log_print() even in production code. Note that in client
  plugin, error_log_print() will print nothing if opt_auth_win_clinet_log
  is 0.
*/
#if defined(DEBUG_ERROR_LOG) && defined(DBUG_OFF)
#define ERROR_LOG(Level, Msg)     do {} while (0)
#else
#define ERROR_LOG(Level, Msg)     error_log_print< error_log_level::Level > Msg
#endif

inline
void debug_msg(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  fflush(stderr);
  va_end(args);
}


void error_log_vprint(error_log_level::type level,
                        const char *fmt, va_list args);

template <error_log_level::type Level>
void error_log_print(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  error_log_vprint(Level, fmt, args);
  va_end(args);
}

typedef char Error_message_buf[1024];
const char* get_last_error_message(Error_message_buf);


/** Blob class *************************************************************/

typedef unsigned char byte;

/**
  Class representing a region of memory (e.g., a string or binary buffer).

  @note This class does not allocate memory. It merely describes a region
  of memory which must be allocated externally (if it is dynamic memory).
*/

class Blob
{
  byte   *m_ptr;  ///< Pointer to the first byte of the memory region.
  size_t  m_len;  ///< Length of the memory region.

public:

  Blob(): m_ptr(NULL), m_len(0)
  {}

  Blob(const byte *ptr, const size_t len)
  : m_ptr(const_cast<byte*>(ptr)), m_len(len)
  {}

  Blob(const char *str): m_ptr((byte*)str)
  {
    m_len= strlen(str);
  }

  byte*  ptr() const
  {
    return m_ptr;
  }

  size_t len() const
  {
    return m_len;
  }

  byte operator[](unsigned pos) const
  {
    return pos < len() ? m_ptr[pos] : 0x00;
  }

  bool is_null() const
  {
    return m_ptr == NULL;
  }
};


/** Connection class *******************************************************/

/**
  Convenience wrapper around MYSQL_PLUGIN_VIO object providing basic
  read/write operations.
*/

class Connection
{
  MYSQL_PLUGIN_VIO *m_vio;    ///< Pointer to @c MYSQL_PLUGIN_VIO structure.

  /**
    If non-zero, indicates that connection is broken. If this has happened
    because of failed operation, stores non-zero error code from that failure.
  */
  int               m_error;

public:

  Connection(MYSQL_PLUGIN_VIO *vio);
  int write(const Blob&);
  Blob read();

  int error() const
  {
    return m_error;
  }
};


/** Sid class **************************************************************/

/**
  Class for storing and manipulating Windows security identifiers (SIDs).
*/

class Sid
{
  TOKEN_USER    *m_data;  ///< Pointer to structure holding identifier's data.
  SID_NAME_USE   m_type;  ///< Type of identified entity.

public:

  Sid(const wchar_t*);
  Sid(HANDLE sec_token);
  ~Sid();

  bool is_valid(void) const;

  bool is_group(void) const
  {
    return m_type == SidTypeGroup
           || m_type == SidTypeWellKnownGroup
           || m_type == SidTypeAlias;
  }

  bool is_user(void) const
  {
    return m_type == SidTypeUser;
  }

  bool operator==(const Sid&);

  operator PSID() const
  {
    return (PSID)m_data->User.Sid;
  }

#ifndef DBUG_OFF

private:
    char *m_as_string;  ///< Cached string representation of the SID.
public:
    const char* as_string();

#endif
};


/** UPN class **************************************************************/

/**
  An object of this class obtains and stores User Principal Name of the
  account under which current process is running.
*/

class UPN
{
  char   *m_buf;  ///< Pointer to UPN in utf8 representation.
  size_t  m_len;  ///< Length of the name.

public:

  UPN();
  ~UPN();

  bool is_valid() const
  {
    return m_len > 0;
  }

  const Blob as_blob() const
  {
    return m_len ? Blob((byte*)m_buf, m_len) : Blob();
  }

  const char* as_string() const
  {
    return (const char*)m_buf;
  }

};


char*     wchar_to_utf8(const wchar_t*, size_t*);
wchar_t*  utf8_to_wchar(const char*, size_t*);

#endif