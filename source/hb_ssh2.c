
#include "hb_ssh2.h"

static ULONG hb_ssh2_getAddr( const char *szName )
{
   ULONG ulAddr = inet_addr( szName );

   if( ulAddr == INADDR_NONE )
   {
      struct hostent *Host = gethostbyname( szName );

      if( Host )
         return ( *( UINT * ) Host->h_addr_list[0] );
      else
         return INADDR_NONE;
   }

   return ulAddr;
}

int hb_ssh2_WaitSocket( int socket_fd, LIBSSH2_SESSION * session )
{
   struct timeval timeout;
   int rc;
   fd_set fd;
   fd_set *writefd = NULL;
   fd_set *readfd = NULL;
   int dir;

   timeout.tv_sec = 10;
   timeout.tv_usec = 0;

   FD_ZERO( &fd );

   FD_SET( socket_fd, &fd );

   /* now make sure we wait in the correct direction */
   dir = libssh2_session_block_directions( session );

   if( dir & LIBSSH2_SESSION_BLOCK_INBOUND )
      readfd = &fd;

   if( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND )
      writefd = &fd;

   rc = select( socket_fd + 1, readfd, writefd, NULL, &timeout );

   return rc;
}

HB_SSH2_SESSION *hb_ssh2_init( const char *hostname, int iPort, int iNonBlocking )
{

   HB_SSH2_SESSION *pSess = ( HB_SSH2_SESSION * ) malloc( sizeof( HB_SSH2_SESSION ) );
   unsigned long hostaddr;
   struct sockaddr_in sin;
   int rc;
#ifdef WIN32
   WSADATA wsadata;
#endif

   memset( pSess, 0, sizeof( HB_SSH2_SESSION ) );

#ifdef WIN32
   rc = WSAStartup( MAKEWORD( 2, 0 ), &wsadata );
   if( rc != 0 )
   {
      pSess->iRes = -99;
      return pSess;
   }
#endif

   rc = libssh2_init( 0 );

   if( rc != 0 )
   {
      pSess->iRes = -1;
      return pSess;
   }

   hostaddr = hb_ssh2_getAddr( hostname );
   if( hostaddr == INADDR_NONE )
   {
      pSess->iRes = -2;
      return pSess;
   }
   pSess->sock = socket( AF_INET, SOCK_STREAM, 0 );

   sin.sin_family = AF_INET;
   sin.sin_port = htons( iPort );
   sin.sin_addr.s_addr = hostaddr;
   if( connect( pSess->sock, ( struct sockaddr * ) ( &sin ),
               sizeof( struct sockaddr_in ) ) != 0 )
   {
      pSess->iRes = -3;
      return pSess;
   }

   /* Create a session instance */
   pSess->session = libssh2_session_init(  );

   if( !pSess->session )
   {
      pSess->iRes = -4;
      return pSess;
   }

   libssh2_session_set_blocking( pSess->session, !iNonBlocking );
   pSess->iNonBlocking = iNonBlocking;

   while( ( rc =
               libssh2_session_handshake( pSess->session,
                     pSess->sock ) ) == LIBSSH2_ERROR_EAGAIN );
   if( rc )
   {
      pSess->iRes = -5;
      return pSess;
   }

   return pSess;
}

void hb_ssh2_close( HB_SSH2_SESSION * pSess )
{

   hb_ssh2_FtpClose( pSess );
   hb_ssh2_CloseChannel( pSess );

   if( pSess->session )
   {
      libssh2_session_disconnect( pSess->session,
            "Normal Shutdown, Thank you for playing" );
      libssh2_session_free( pSess->session );
      pSess->session = NULL;
   }

   if( pSess->sock )
   {
#ifdef WIN32
      closesocket( pSess->sock );
#else
      close( pSess->sock );
#endif
      pSess->sock = 0;
   }

   free( pSess );

   libssh2_exit(  );
}

int hb_ssh2_LoginPass( HB_SSH2_SESSION * pSess, const char *pLogin, const char *pPass )
{
   int rc;

   while( ( rc =
               libssh2_userauth_password( pSess->session, pLogin,
                     pPass ) ) == LIBSSH2_ERROR_EAGAIN );
   pSess->iRes = rc;
   if( rc )
      return 1;
   return 0;
}

void hb_ssh2_OpenChannel( HB_SSH2_SESSION * pSess )
{
   /* Exec non-blocking on the remove host */
   while( ( pSess->channel =
               libssh2_channel_open_session( pSess->session ) ) == NULL &&
         libssh2_session_last_error( pSess->session, NULL, NULL,
               0 ) == LIBSSH2_ERROR_EAGAIN )
   {
      hb_ssh2_WaitSocket( pSess->sock, pSess->session );
   }
   pSess->iRes = ( pSess->channel == NULL );
}

void hb_ssh2_CloseChannel( HB_SSH2_SESSION * pSess )
{

   if( !pSess->channel )
      return;
   while( libssh2_channel_close( pSess->channel ) == LIBSSH2_ERROR_EAGAIN )
      hb_ssh2_WaitSocket( pSess->sock, pSess->session );

#if 0
   {
      int exitcode = 127;
      char *exitsignal = ( char * ) "none";

      if( rc == 0 )
      {
         exitcode = libssh2_channel_get_exit_status( pSess->channel );
         libssh2_channel_get_exit_signal( pSess->channel, &exitsignal,
               NULL, NULL, NULL, NULL, NULL );
      }

      if( exitsignal )
         fprintf( stderr, "\nGot signal: %s\n", exitsignal );
      else
         fprintf( stderr, "\nEXIT: %d\n", exitcode );
   }
#endif

   libssh2_channel_free( pSess->channel );
   pSess->channel = NULL;
}

void hb_ssh2_Exec( HB_SSH2_SESSION * pSess, const char *commandline )
{
   int rc;

   while( ( rc = libssh2_channel_exec( pSess->channel,
                     commandline ) ) == LIBSSH2_ERROR_EAGAIN )
      hb_ssh2_WaitSocket( pSess->sock, pSess->session );
   if( rc != 0 )
      pSess->iRes = -1;
   return;
}

char * hb_ssh2_ChannelRead( HB_SSH2_SESSION * pSess )
{

   int nBuffSize = 4000;
   char buffer[nBuffSize], *pOut = NULL;
   int iBytesRead = 0, iBytesReadAll = 0;
   int iOutFirst = 1;
   int rc;

   for( ;; )
   {
      do
      {
         rc = libssh2_channel_read( pSess->channel, buffer+iBytesRead, nBuffSize-iBytesRead );

         if( rc > 0 )
         {
            iBytesRead += rc;
         }
      }
      while( rc > 0 && iBytesRead < nBuffSize );

      if( iOutFirst )
      {
         pOut = (char*) malloc( iBytesRead + 1 );
         memcpy( pOut, buffer, iBytesRead );
         iOutFirst = 0;
      }
      else
      {
         pOut = ( char * ) realloc( pOut, iBytesReadAll + iBytesRead + 1 );
         memcpy( pOut+iBytesReadAll, buffer, iBytesRead );
      }
      iBytesReadAll += iBytesRead;
      pOut[iBytesReadAll] = '\0';
      iBytesRead = 0;

      /* this is due to blocking that would occur otherwise so we loop on
         this condition */
      if( rc == LIBSSH2_ERROR_EAGAIN )
      {
         hb_ssh2_WaitSocket( pSess->sock, pSess->session );
      }
      else
         break;
   }

   return pOut;
}

void hb_ssh2_FtpInit( HB_SSH2_SESSION * pSess )
{

   while( ( pSess->sftp_session =
               libssh2_sftp_init( pSess->session ) ) == NULL &&
         libssh2_session_last_error( pSess->session, NULL, NULL,
               0 ) == LIBSSH2_ERROR_EAGAIN )
   {
      hb_ssh2_WaitSocket( pSess->sock, pSess->session );
   }
   pSess->iRes = ( pSess->sftp_session == NULL );
}

void hb_ssh2_FtpOpenDir( HB_SSH2_SESSION * pSess, const char *sftppath )
{
   pSess->sftp_handle = libssh2_sftp_opendir( pSess->sftp_session, sftppath );
   pSess->iRes = ( pSess->sftp_handle == NULL );
}

void hb_ssh2_FtpClose( HB_SSH2_SESSION * pSess )
{
   if( pSess->sftp_handle )
      libssh2_sftp_close( pSess->sftp_handle );
   pSess->sftp_handle = NULL;
}

void hb_ssh2_FtpOpenFile( HB_SSH2_SESSION * pSess, const char *sftppath,
      unsigned long ulFlags, long lMode )
{
   pSess->sftp_handle =
         libssh2_sftp_open( pSess->sftp_session, sftppath, ulFlags, lMode );
   pSess->iRes = ( pSess->sftp_handle == NULL );
}

int hb_ssh2_FtpReadDir( HB_SSH2_SESSION * pSess, char *cName, int iLen,
      unsigned long *pSize, unsigned long *pTime, unsigned long *pAttrs )
{
   LIBSSH2_SFTP_ATTRIBUTES attrs;
   int rc = libssh2_sftp_readdir( pSess->sftp_handle, cName, iLen, &attrs );
   if( rc )
   {
      *pSize = ( attrs.flags & LIBSSH2_SFTP_ATTR_SIZE ) ? attrs.filesize : 0;
      //*pSize = attrs.filesize;
      *pTime = ( attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME ) ? attrs.mtime : 0;
      *pAttrs = ( attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS ) ? attrs.permissions : 0;
   }
   return rc;
}

#ifdef _USE_HB

#include "hbapi.h"
#include "hbapiitm.h"
#include "hbapicdp.h"

HB_FUNC( SSH2_INIT )
{
   int iPort = ( hb_pcount(  ) > 1 && HB_ISNUM( 2 ) ) ? hb_parni( 2 ) : 22;
   int iNonBlocking = ( hb_pcount(  ) > 2 && HB_ISLOG( 3 ) ) ? hb_parl( 3 ) : 0;

   if( HB_ISCHAR( 1 ) )
      hb_retptr( ( void * ) hb_ssh2_init( hb_parc( 1 ), iPort, iNonBlocking ) );
}

HB_FUNC( SSH2_LASTERR )
{
   hb_retni( ( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) )->iRes );
}

HB_FUNC( SSH2_CLOSE )
{
   hb_ssh2_close( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) );
}

HB_FUNC( SSH2_LOGIN )
{
   hb_retl( !hb_ssh2_LoginPass( ( HB_SSH2_SESSION * ) hb_parptr( 1 ),
               hb_parc( 2 ), hb_parc( 3 ) ) );
}

HB_FUNC( SSH2_OPENCHANNEL )
{
   hb_ssh2_OpenChannel( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) );
}

HB_FUNC( SSH2_CLOSECHANNEL )
{
   hb_ssh2_CloseChannel( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) );
}

HB_FUNC( SSH2_EXEC )
{
   hb_ssh2_Exec( ( HB_SSH2_SESSION * ) hb_parptr( 1 ), hb_parc( 2 ) );
}

HB_FUNC( SSH2_FTP_INIT )
{
   hb_ssh2_FtpInit( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) );
}

HB_FUNC( SSH2_FTP_OPENDIR )
{
   hb_ssh2_FtpOpenDir( ( HB_SSH2_SESSION * ) hb_parptr( 1 ), hb_parc( 2 ) );
}

HB_FUNC( SSH2_FTP_CLOSE )
{
   hb_ssh2_FtpClose( ( HB_SSH2_SESSION * ) hb_parptr( 1 ) );
}

HB_FUNC( SSH2_FTP_READDIR )
{
   char mem[512];
   unsigned long ulSize;
   unsigned long ulTime;
   unsigned long ulAttrs;
   int rc = hb_ssh2_FtpReadDir( ( HB_SSH2_SESSION * ) hb_parptr( 1 ), mem,
         sizeof( mem ),
         &ulSize, &ulTime, &ulAttrs );

   if( rc > 0 )
   {
      hb_stornl( ulSize, 2 );
      hb_stortdt( ulTime / 86400 + 2440588, ( ulTime % 86400 ) * 1000, 3 );
      hb_stornl( ulAttrs, 4 );
      hb_retc( mem );
   }
   else
      hb_ret(  );
}

HB_FUNC( SSH2_FTP_OPENFILE )
{
   hb_ssh2_FtpOpenFile( ( HB_SSH2_SESSION * ) hb_parptr( 1 ), hb_parc( 2 ),
         ( unsigned long ) hb_parnl( 3 ), hb_parnl( 4 ) );
}

HB_FUNC( SSH2_FTP_EXEC )
{
   hb_ssh2_Exec( ( HB_SSH2_SESSION * ) hb_parptr( 1 ), hb_parc( 2 ) );
}

HB_FUNC( SSH2_CHANNELREAD )
{
   HB_SSH2_SESSION *pSess = ( HB_SSH2_SESSION * ) hb_parptr( 1 );
   int nBuffSize = 4000;
   char buffer[nBuffSize], *pOut = NULL;
   int iBytesRead = 0, iBytesReadAll = 0;
   int iOutFirst = 1;
   int rc;

   for( ;; )
   {
      do
      {
         rc = libssh2_channel_read( pSess->channel, buffer+iBytesRead, nBuffSize-iBytesRead );

         if( rc > 0 )
         {
            iBytesRead += rc;
         }
      }
      while( rc > 0 && iBytesRead < nBuffSize );

      if( iOutFirst )
      {
         pOut = (char*) hb_xgrab( iBytesRead + 1 );
         memcpy( pOut, buffer, iBytesRead );
         iOutFirst = 0;
      }
      else
      {
         pOut = ( char * ) hb_xrealloc( pOut, iBytesReadAll + iBytesRead + 1 );
         memcpy( pOut+iBytesReadAll, buffer, iBytesRead );
      }
      iBytesReadAll += iBytesRead;
      pOut[iBytesReadAll] = '\0';
      iBytesRead = 0;

      /* this is due to blocking that would occur otherwise so we loop on
         this condition */
      if( rc == LIBSSH2_ERROR_EAGAIN )
      {
         hb_ssh2_WaitSocket( pSess->sock, pSess->session );
      }
      else
         break;
   }

   if( pOut )
      hb_retclen_buffer( pOut, iBytesReadAll );
   else
      hb_ret();

}

#endif
