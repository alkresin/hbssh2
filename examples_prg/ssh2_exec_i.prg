/*
 * Sample showing interactive command execution on remote server via SFTP
 *  using non-blocking io
 */

#include "inkey.ch"

#define SHIFT_PRESSED 0x010000
#define CTRL_PRESSED  0x020000
#define ALT_PRESSED   0x040000

REQUEST HB_CODEPAGE_RU866

FUNCTION Main( cAddr, cProgram )

   LOCAL pSess, nPort, cLogin, cPass, nPos
   LOCAL xRes, nColInit, cmd := "", nKeyExt, nKey

   IF cAddr == Nil
      Outstd( Chr(10) + "Address: " )
      cAddr := Getline()
   ENDIF
   IF Empty( cAddr )
      RETURN Nil
   ENDIF

   IF cProgram == Nil
      Outstd( Chr(10) + "Command (default: uptime):" )
      cProgram := Getline()
   ENDIF
   IF Empty(cProgram)
      cProgram := "uptime"
   ENDIF

   Outstd( Chr(10) + "Login:" )
   cLogin := Getline()
   Outstd( Chr(10) + "Password:" )
   cPass := Getline()

   IF ( nPos := At( ':', cAddr ) ) > 0
      nPort := Val( Substr( cAddr,nPos+1 ) )
      cAddr := Left( cAddr,nPos-1 )
   ENDIF

   pSess := ssh2_Connect( cAddr, nPort, .T. )

   IF ssh2_LastRes( pSess ) != 0
      Outstd( Chr(10) + "Connection error" )
      ssh2_Close( pSess )
      RETURN Nil
   ELSE
      Outstd( Chr(10) +  "Connected" )
   ENDIF

   ssh2_Timeout( pSess, 0, 10000 )

   IF ssh2_Login( pSess, cLogin, cPass )
      Outstd( Chr(10) +  "Login - Ok" )
   ELSE
      Outstd( Chr(10) +  "Can't login..." )
      ssh2_Close( pSess )
      RETURN Nil
   ENDIF

   ssh2_Channel_Open( pSess )

   IF ssh2_LastRes( pSess ) != 0
      Outstd( Chr(10) +  "OpenChannel failed" )
      RETURN Nil
   ENDIF

   ssh2_Channel_Pty( pSess, "xterm" )

   //ssh2_Exec( pSess, cProgram )
   ssh2_Channel_Shell( pSess )
   IF ssh2_LastRes( pSess ) != 0
      Outstd( Chr(10) +  "Exec failed" )
      RETURN Nil
   ENDIF

   DevPos( Maxrow(), nColInit := 0 )
   Outstd( Chr(10) +  "> " + cProgram )
   writelog( "start " + cProgram )

   DO WHILE ( xRes := ssh2_Channel_ReadRaw( pSess ) ) != Nil
      writelog( ltrim(str(seconds())) + ": " + valtype(xRes) )
      IF !Empty( xRes )
         Outstd( xRes )
         nColInit := Col()
         cmd := ""
         writelog( xres )
         writelog( "-----" )
      ENDIF
      nKeyExt := Inkey( 0.05, INKEY_KEYBOARD + HB_INKEY_EXT )
      nKey := hb_keyStd( nKeyExt )
      IF nKeyExt == 0
         LOOP
      ELSEIF nKey == K_ESC
         EXIT
      ELSEIF nKey == K_ENTER
         ssh2_Channel_Write( pSess, hb_eol() )
         //ssh2_Channel_Write( pSess, cmd + hb_eol() )
         //cmd := ""
         //Outstd( Chr(10) )
         nColInit := Col()
      ELSEIF nKey >= 32 .AND. nKey <= 250
         //cmd += Chr( nKey )
         //Outstd( nKey )
         ssh2_Channel_Write( pSess, Chr(nKey) )
      //ELSE
      //   cmd := ProcessKey( nColInit, cmd, nKeyExt )
      ENDIF
   ENDDO

   ssh2_Close( pSess )
   ssh2_Exit()

RETURN Nil

STATIC FUNCTION ProcessKey( nColInit, cRes, nKeyExt, bKeys )

   LOCAL nRow := Row(), lChg, cTemp, nResLen := Len( cRes ), nPos := Col() - nColInit + 1
   LOCAL nKey := hb_keyStd( nKeyExt )
   STATIC lIns := .T.

   lChg := .F.
   IF nKey >= 32 .AND. nKey <= 250
      cRes := Left( cRes, nPos-1 ) + Chr(nKey) + Substr( cRes, Iif(lIns,nPos,nPos+1) )
      nPos ++
      lChg := .T.
   ELSEIF nKey == K_DEL
      IF nPos <= Len( cRes )
         cRes := Left( cRes, nPos-1 ) + Substr( cRes, nPos+1 )
         lChg := .T.
      ENDIF
   ELSEIF nKey == K_BS
      IF nPos > 1
         cRes := Left( cRes, nPos-2 ) + Substr( cRes, nPos )
         nPos --
         lChg := .T.
      ENDIF
   ELSEIF nKey == K_RIGHT
      IF nPos <= Len( cRes )
         nPos ++
      ENDIF
   ELSEIF nKey == K_LEFT
      IF nPos > 1
         nPos --
      ENDIF
   ELSEIF nKey == K_HOME
      nPos := 1
   ELSEIF nKey == K_END
      nPos := Len( cRes ) + 1
   ELSEIF nKey == K_ENTER
      RETURN cRes
   ELSEIF (hb_BitAnd( nKeyExt, CTRL_PRESSED ) != 0 .AND. nKey == 22) .OR. ;
      ( hb_BitAnd( nKeyExt, SHIFT_PRESSED ) != 0 .AND. nKey == K_INS )
      cRes := Left( cRes, nPos-1 ) + cTemp + Substr( cRes, Iif(lIns,nPos,nPos+1) )
      nPos := nPos + Len( cTemp )
      lChg := .T.
   ELSEIF nKey == K_INS
      lIns := !lIns
   ELSEIF nKey == K_ESC
      cRes := ""
      RETURN cRes
   ELSEIF !Empty( bKeys ) .AND. Valtype( cTemp := Eval( bKeys,nKey ) ) == "C"
      cRes := cTemp
      nPos := Len( cRes ) + 1
      lChg := .T.
   ENDIF
   IF lChg
      DevPos( nRow, nColInit )
      DevOut( cRes )
      IF nResLen > Len( cRes )
         DevOut( Space(nResLen - Len( cRes )) )
      ENDIF
   ENDIF
   DevPos( nRow, nColInit + nPos - 1 )

   RETURN cRes

STATIC FUNCTION GetLine()

   LOCAL cRes := "", nKey

   DO WHILE .T.
      nKeyExt := Inkey( 0, INKEY_KEYBOARD + HB_INKEY_EXT )
      nKey := hb_keyStd( nKeyExt )
      IF nKeyExt == 0
         LOOP
      ELSEIF nKey == K_ESC
         RETURN ""
      ELSEIF nKey == K_ENTER
         RETURN cRes
      ELSEIF nKey >= 32 .AND. nKey <= 250
         cRes += Chr( nKey )
         Outstd( Chr(nKey) )
      ENDIF
   ENDDO

   RETURN cRes

FUNCTION writelog( cText )

   LOCAL nHand, fname

   fname := "a.log"
   IF ! File( fname )
      nHand := FCreate( fname )
   ELSE
      nHand := FOpen( fname, 1 )
   ENDIF
   FSeek( nHand, 0, 2 )
   FWrite( nHand, cText + Chr( 10 ) )
   FClose( nHand )

   RETURN Nil
