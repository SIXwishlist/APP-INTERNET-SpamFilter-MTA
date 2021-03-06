#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_RXMACRO              /* include macrospace info         */
#define INCL_RXFUNC               /* include external function  info */
#define INCL_REXXSAA
#include <os2.h>
#include <rexxsaa.h>
#include <stdio.h>
#include <string.h>
#include <types.h>
#include <sys\socket.h>
#include <unistd.h>
#include <sys\un.h>
#include <util.h>
#include <debug.h>

#define INVALID_ROUTINE		40           /* Raise Rexx error           */
#define VALID_ROUTINE		0            /* Successful completion      */

#define BUILDRXSTRING(t, s) { \
  strcpy((t)->strptr,(s));\
  (t)->strlength = strlen((s)); \
}

RexxFunctionHandler rxsfLoadFuncs;
RexxFunctionHandler rxsfDropFuncs;
RexxFunctionHandler rxsfOpen;
RexxFunctionHandler rxsfClose;
RexxFunctionHandler rxsfSend;
RexxFunctionHandler rxsfRecv;
RexxFunctionHandler rxsfRequest;

static PSZ RxFncTable[] =
{
  "rxsfLoadFuncs",
  "rxsfDropFuncs",
  "rxsfOpen",
  "rxsfClose",
  "rxsfSend",
  "rxsfRecv",
  "rxsfRequest"
};


static BOOL _rxstrtoi(RXSTRING rxsValue, int *piValue)
{
  PSZ      pszValue = RXSTRPTR( rxsValue );
  PCHAR    pcEnd;

  if ( !RXVALIDSTRING( rxsValue ) )
    return FALSE;

  *piValue = (int)strtol( pszValue, &pcEnd, 10 );

  return ( pcEnd != pszValue ) && ( errno == 0 );
}

static BOOL _setRxValue(PSZ pszName, ULONG cbValue, PCHAR pcValue)
{
  SHVBLOCK	sBlock;

  strupr( pszName );

  sBlock.shvnext = NULL;
  MAKERXSTRING( sBlock.shvname, pszName, strlen( pszName ) );
  sBlock.shvvalue.strptr = pcValue;
  sBlock.shvvalue.strlength = cbValue;
  sBlock.shvnamelen = sBlock.shvname.strlength;
  sBlock.shvvaluelen = sBlock.shvvalue.strlength;
  sBlock.shvcode = RXSHV_SET;
  sBlock.shvret = 0;

  return RexxVariablePool( &sBlock ) != RXSHV_BADN;
}

static int _connOpen(PSZ pszSocket)
{
  struct sockaddr_un   stUn;
  int                  iSocket;

  stUn.sun_path[sizeof(stUn.sun_path) - 1] = '\0';
  _snprintf( stUn.sun_path, sizeof(stUn.sun_path) - 1, "\\socket\\%s",
             ( pszSocket == NULL || *pszSocket == '\0' )
                ? "SPAMFILTER" : pszSocket );
  stUn.sun_len = sizeof(stUn);
  stUn.sun_family = AF_UNIX;

  iSocket = socket( PF_UNIX, SOCK_STREAM, 0 );

  if ( ( iSocket != -1 ) &&
       ( connect( iSocket, (struct sockaddr *)&stUn, SUN_LEN( &stUn ) ) == -1 ) )
  {
    debug( "connect() failed, error: %d", sock_errno() );
    soclose( iSocket );
    iSocket = -1;
  }

  return iSocket;
}

static BOOL _connClose(int iSocket)
{
  shutdown( iSocket, 1 );
  return soclose( iSocket ) == 0;
}


ULONG rxsfLoadFuncs(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
                    PSZ pszQueue, RXSTRING *prxstrRet)
{
  ULONG			ulIdx;
 
  prxstrRet->strlength = 0;
  if ( cArgs > 0 )
    return INVALID_ROUTINE;
 
  for( ulIdx = 0; ulIdx < ARRAY_SIZE(RxFncTable); ulIdx++ )
    RexxRegisterFunctionDll( RxFncTable[ulIdx], "RXSF", RxFncTable[ulIdx] );

  debugInit();
  return VALID_ROUTINE;
}

ULONG rxsfDropFuncs(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
                    PSZ pszQueue, RXSTRING *prxstrRet)
{
  ULONG			ulIdx;
 
  prxstrRet->strlength = 0;
  if ( cArgs > 0 )
    return INVALID_ROUTINE;
 
  for( ulIdx = 0; ulIdx < sizeof(RxFncTable)/sizeof(PSZ); ulIdx++ )
    RexxDeregisterFunction( RxFncTable[ulIdx] );

  debug( "Done. Allocated memory: %d", debugMemUsed() );
  debugDone();
  return VALID_ROUTINE;
}

// socket = rxsfOpen( [name] )
//
// Opens connection to the program (local IPC socket).
// Name: local socket name witout leading part "\socket\". By default name is
// "SPAMFILTER". Returns socket handle or -1 on error.

ULONG rxsfOpen(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
               PSZ pszQueue, RXSTRING *prxstrRet)
{
  CHAR                 acBuf[16];

  ltoa( _connOpen( cArgs == 0 ? NULL : RXSTRPTR( aArgs[0] ) ), &acBuf, 10 );
  BUILDRXSTRING( prxstrRet, &acBuf );

  return VALID_ROUTINE;
}

// ret = rxsfClose( socket )
//
// Closes connection created by rxsfOpen().
// Return: The value OK indicates success; the value ERROR indicates an error.

ULONG rxsfClose(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
               PSZ pszQueue, RXSTRING *prxstrRet)
{
  int        iSocket;

  if ( cArgs != 1 || !_rxstrtoi( aArgs[0], &iSocket ) )
    return INVALID_ROUTINE;

  BUILDRXSTRING( prxstrRet, _connClose( iSocket ) ? "OK:" : "ERROR:" );
  return VALID_ROUTINE;
}

// rxsfSend( socket, data[, flags] )
//
// Flags: one or more separated by spaces: MSG_DONTROUTE, MSG_DONTWAIT, MSG_OOB.
// Return: Number of bytes that is added to the send buffer;
//         the value -1 indicates an error.

ULONG rxsfSend(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
               PSZ pszQueue, RXSTRING *prxstrRet)
{
  int        iSocket;
  int        iFlags = 0;

  if ( ( cArgs < 2 ) || ( cArgs > 3 ) || !_rxstrtoi( aArgs[0], &iSocket ) ||
       !RXVALIDSTRING( aArgs[1] ) )
    return INVALID_ROUTINE;

  if ( ( cArgs == 3 ) && RXVALIDSTRING( aArgs[2] ) )
  {
    ULONG    cbFlags = aArgs[2].strlength;
    PCHAR    pcFlags = aArgs[2].strptr;
    ULONG    cbFlag;
    PCHAR    pcFlag;

    while( utilStrCutWord( &cbFlags, &pcFlags, &cbFlag, &pcFlag ) )
    {
      switch( utilStrWordIndex( "MSG_DONTROUTE MSG_DONTWAIT MSG_OOB", cbFlag,
                                pcFlag ) )
      {
        case -1:
          return INVALID_ROUTINE;

        case 0:
          iFlags |= MSG_DONTROUTE;
          break;

        case 1:
          iFlags |= MSG_DONTWAIT;
          break;

        case 2:
          iFlags |= MSG_OOB;
          break;
      }
    }
  }

  itoa( send( iSocket, aArgs[1].strptr, aArgs[1].strlength, iFlags ),
        prxstrRet->strptr, 10 );
  prxstrRet->strlength = strlen( prxstrRet->strptr );

  return VALID_ROUTINE;
}

// rxsfRecv( socket, varName[, flags] )
//
// Flags: one or more separated by spaces: MSG_DONTWAIT, MSG_OOB, MSG_PEEK,
//        MSG_WAITALL.
// Return: The value OK indicates success; the value ERROR indicates an error.

ULONG rxsfRecv(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
               PSZ pszQueue, RXSTRING *prxstrRet)
{
  int        iSocket;
  int        iFlags = 0;
  PCHAR      pcBuf;
  int        iRC;
  BOOL       fSuccess;

  if ( ( cArgs < 2 ) || ( cArgs > 3 ) || !_rxstrtoi( aArgs[0], &iSocket ) ||
       !RXVALIDSTRING( aArgs[1] ) )
    return INVALID_ROUTINE;

  if ( ( cArgs == 3 ) && RXVALIDSTRING( aArgs[2] ) )
  {
    ULONG    cbFlags = aArgs[2].strlength;
    PCHAR    pcFlags = aArgs[2].strptr;
    ULONG    cbFlag;
    PCHAR    pcFlag;

    while( utilStrCutWord( &cbFlags, &pcFlags, &cbFlag, &pcFlag ) )
    {
      switch( utilStrWordIndex( "MSG_DONTWAIT MSG_OOB MSG_PEEK MSG_WAITALL",
                                cbFlag, pcFlag ) )
      {
        case -1:
          return INVALID_ROUTINE;

        case 0:
          iFlags |= MSG_DONTWAIT;
          break;

        case 1:
          iFlags |= MSG_OOB;
          break;

        case 2:
          iFlags |= MSG_PEEK;
          break;

        case 3:
          iFlags |= MSG_WAITALL;
          break;
      }
    }
  }

  pcBuf = debugMAlloc( 65535 );

  if ( pcBuf == NULL )
  {
    BUILDRXSTRING( prxstrRet, "ERROR: Not enough memory" );
  }
  else
  {
    iRC = recv( iSocket, pcBuf, 65535, iFlags );
    fSuccess = ( iRC == -1 ) ||
               _setRxValue( RXSTRPTR( aArgs[1] ), iRC, pcBuf );
    debugFree( pcBuf );

    if ( !fSuccess )
    {
      BUILDRXSTRING( prxstrRet, "ERROR: Cannot set variable" );
    }
    else
    {
      itoa( iRC, prxstrRet->strptr, 10 );
      prxstrRet->strlength = strlen( prxstrRet->strptr );
    }
  }

  return VALID_ROUTINE;
}

// answer = rxsfRequest( [socket], request )
//
// Sends request to the program and returns an answer. Socket may be a handle
// open by rxsfOpen() or local IPC socket name witout leading "\socket\".
// By default socket is "SPAMFILTER".

ULONG rxsfRequest(PUCHAR puchName, ULONG cArgs, RXSTRING aArgs[],
                  PSZ pszQueue, RXSTRING *prxstrRet)
{
  int        iSocket;
  int        iFlags = 0;
  PCHAR      pcBuf = NULL;
  int        iRC;
  BOOL       fSuccess;
  ULONG      cbRequest;
  PCHAR      pcRequest;
  BOOL       fNewConn = FALSE;

  if ( ( cArgs != 2 ) || !RXVALIDSTRING( aArgs[1] ) )
    return INVALID_ROUTINE;

  pcRequest = aArgs[1].strptr;
  cbRequest = aArgs[1].strlength;

  // Add CRLF if this is not present in a given string.

  if ( ( cbRequest < 2 ) ||
       ( *((PUSHORT)&pcRequest[ cbRequest - 2 ]) != (USHORT)'\n\r' ) )
  {
    pcBuf = debugMAlloc( cbRequest + 4 );
    memcpy( pcBuf, pcRequest, cbRequest );
    *((PULONG)&pcBuf[cbRequest]) = (ULONG)'\0\0\n\r';

    pcRequest = pcBuf;
    cbRequest += 2;
  }

  do
  {
    // First argument (socket) is numerical - try send use this socket handle.

    if ( _rxstrtoi( aArgs[0], &iSocket ) )
    {
      iRC = send( iSocket, pcRequest, cbRequest, 0 );
      if ( ( iRC != -1 ) || ( sock_errno() != SOCENOTSOCK ) )
        break;
    }

    // First argument is not a socket or string - try open socket using first
    // argument as the name.

    iSocket = _connOpen( RXSTRPTR( aArgs[0] ) );
    if ( iSocket == -1 )
    {
      BUILDRXSTRING( prxstrRet, "ERROR: Cannot connect to the program" );
      if ( pcBuf != NULL )
        debugFree( pcBuf );
      return VALID_ROUTINE;
    }
    fNewConn = TRUE;

    // Connection was open - send data.

    iRC = send( iSocket, pcRequest, cbRequest, 0 );
  }
  while( FALSE );

  // Destroy temporary buffer (used for trailing CRLF).
  if ( pcBuf != NULL )
    debugFree( pcBuf );

  if ( iRC == -1 )
  {
    BUILDRXSTRING( prxstrRet, "ERROR: Cannot send a request to the program" );
  }
  else
  {
    // Read answer.

    iRC = recv( iSocket, prxstrRet->strptr, 65535, 0 );
    if ( iRC == -1 )
    {
      BUILDRXSTRING( prxstrRet, "ERROR: Cannot get an answer from the program" );
    }
    else
    {
      // Remove all trailing CRs, LFs, TABs and SPACEs from the answer.

      BUF_RTRIM( iRC, prxstrRet->strptr );
      prxstrRet->strlength = iRC;
    }
  }

  if ( fNewConn )
    _connClose( iSocket );

  return VALID_ROUTINE;
}
