#include "includes.h"
#include "Tokenizer.h"
#include "URL.h"
#include "Time.h"
#include "Encodings.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>

#ifdef HAVE_LIBSSL
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/asn1.h>

DH *CachedDH=NULL;



void OpenSSLReseedRandom()
{
int len=32;
char *Tempstr=NULL;


len=GenerateRandomBytes(&Tempstr, len, ENCODE_NONE);
RAND_seed(Tempstr,len);
memset(Tempstr,0,len); //extra paranoid step, don't keep those bytes in memory!

DestroyString(Tempstr);
}



void OpenSSLGenerateDHParams()
{
	CachedDH = DH_new();
	if(CachedDH)
	{
		OpenSSLReseedRandom();
		DH_generate_parameters_ex(CachedDH, 512, DH_GENERATOR_5, 0); 
		//DH_check(CachedDH, &codes); 
	}
}








void STREAM_INTERNAL_SSL_ADD_SECURE_KEYS(STREAM *S, SSL_CTX *ctx)
{
ListNode *Curr;
char *VerifyFile=NULL, *VerifyPath=NULL;

SSL_CTX_set_default_verify_paths(ctx);

//Default Verify path
//VerifyFile=CopyStr(VerifyFile,"/etc/ssl/certs/cacert.pem");

Curr=ListGetNext(LibUsefulValuesGetHead());
while (Curr)
{
  if ((StrLen(Curr->Tag)) && (strcasecmp(Curr->Tag,"SSL_CERT_FILE")==0))
  {
	  SSL_CTX_use_certificate_chain_file(ctx,(char *) Curr->Item);
  }

  if ((StrLen(Curr->Tag)) && (strcasecmp(Curr->Tag,"SSL_KEY_FILE")==0))
  {
	  SSL_CTX_use_PrivateKey_file(ctx,(char *) Curr->Item,SSL_FILETYPE_PEM);
  }

  if ((StrLen(Curr->Tag)) && (strncasecmp(Curr->Tag,"SSL_VERIFY_CERTDIR",18)==0))
  {
	  VerifyPath=CopyStr(VerifyPath,(char *) Curr->Item);
  }

	if ((StrLen(Curr->Tag)) && (strncasecmp(Curr->Tag,"SSL_VERIFY_CERTFILE",19)==0))
	{
	  VerifyFile=CopyStr(VerifyFile,(char *) Curr->Item);
	}

  Curr=ListGetNext(Curr);
}


Curr=ListGetNext(S->Values);
while (Curr)
{
  if ((StrLen(Curr->Tag)) && (strcasecmp(Curr->Tag,"SSL_CERT_FILE")==0))
  {
	  SSL_CTX_use_certificate_chain_file(ctx,(char *) Curr->Item);
  }

  if ((StrLen(Curr->Tag)) && (strcasecmp(Curr->Tag,"SSL_KEY_FILE")==0))
  {
	  SSL_CTX_use_PrivateKey_file(ctx,(char *) Curr->Item,SSL_FILETYPE_PEM);
  }

  if ((StrLen(Curr->Tag)) && (strncasecmp(Curr->Tag,"SSL_VERIFY_CERTDIR",18)==0))
  {
	  VerifyPath=CopyStr(VerifyPath,(char *) Curr->Item);
  }

	if ((StrLen(Curr->Tag)) && (strncasecmp(Curr->Tag,"SSL_VERIFY_CERTFILE",19)==0))
	{
	  VerifyFile=CopyStr(VerifyFile,(char *) Curr->Item);
	}


  Curr=ListGetNext(Curr);
}


SSL_CTX_load_verify_locations(ctx,VerifyFile,VerifyPath);

DestroyString(VerifyFile);
DestroyString(VerifyPath);

}
#endif


void HandleSSLError()
{
#ifdef HAVE_LIBSSL
int val;

	  val=ERR_get_error();
	  fprintf(stderr,"Failed to create SSL_CTX: %s\n",ERR_error_string(val,NULL));
	  fflush(NULL);
#endif
}


int INTERNAL_SSL_INIT()
{
#ifdef HAVE_LIBSSL
char *Tempstr=NULL;
static int InitDone=FALSE;

//Always reseed RAND on a new connection
//OpenSSLReseedRandom();

if (InitDone) return(TRUE);

  SSL_library_init();
#ifdef USE_OPENSSL_ADD_ALL_ALGORITHMS
  OpenSSL_add_all_algorithms();
#endif
  SSL_load_error_strings();
	Tempstr=MCopyStr(Tempstr,SSLeay_version(SSLEAY_VERSION)," : ", SSLeay_version(SSLEAY_BUILT_ON), " : ",SSLeay_version(SSLEAY_CFLAGS),NULL);
	LibUsefulSetValue("SSL-Library", Tempstr);
	LibUsefulSetValue("SSL-Level", "tls");
	DestroyString(Tempstr);
  InitDone=TRUE;
  return(TRUE);
#endif

return(FALSE);
}


int SSLAvailable()
{
	return(INTERNAL_SSL_INIT());
}

const char *OpenSSLQueryCipher(STREAM *S)
{
void *ptr;

if (! S) return(NULL);
ptr=STREAMGetItem(S,"LIBUSEFUL-SSL-CTX");
if (! ptr) return(NULL);

#ifdef HAVE_LIBSSL
const SSL_CIPHER *Cipher;
char *Tempstr=NULL;

Cipher=SSL_get_current_cipher((const SSL *) ptr);
if (Cipher)
{
Tempstr=FormatStr(Tempstr,"%d",SSL_CIPHER_get_bits(Cipher,NULL));
STREAMSetValue(S,"SSL-Bits",Tempstr);
Tempstr=CopyStr(Tempstr,SSL_CIPHER_get_name(Cipher));
STREAMSetValue(S,"SSL-Cipher",Tempstr);

Tempstr=SetStrLen(Tempstr,1024);
Tempstr=SSL_CIPHER_description(Cipher, Tempstr, 1024);
Tempstr=MCatStr(Tempstr, " ", SSL_CIPHER_get_version(Cipher), NULL);
STREAMSetValue(S,"SSL-Cipher-Details",Tempstr);
}

DestroyString(Tempstr);
return(STREAMGetValue(S,"SSL-Cipher"));

#else
return(NULL);
#endif
}


#ifdef HAVE_LIBSSL
int OpenSSLVerifyCallback(int PreverifyStatus, X509_STORE_CTX *X509)
{
//This does nothing. verification is done in 'OpenSSLVerifyCertificate' instead
return(1);
}



char *OpenSSLConvertTime(char *RetStr, const ASN1_TIME *Time)
{
int result;
BIO *b;

	RetStr=SetStrLen(RetStr,80);
	b = BIO_new(BIO_s_mem());
	result=ASN1_TIME_print(b, Time);
	if (result > 0) result = BIO_gets(b, RetStr, 80);
	if (result > 0) RetStr[result]='\0';
	BIO_free(b);

	return(RetStr);	
}
#endif


void OpenSSLCertError(STREAM *S, const char *Error)
{
	STREAMSetValue(S,"SSL-Certificate-Verify",Error);
	RaiseError(0, "SSL-Certificate-Verify", Error);
}

int OpenSSLVerifyCertificate(STREAM *S)
{
int RetVal=FALSE;
#ifdef HAVE_LIBSSL
char *Name=NULL, *Value=NULL, *ptr;
int val;
X509 *cert=NULL;
SSL *ssl;

ptr=STREAMGetItem(S,"LIBUSEFUL-SSL-CTX");
if (! ptr) return(FALSE);

ssl=(SSL *) ptr;

cert=SSL_get_peer_certificate(ssl);
if (cert)
{
	STREAMSetValue(S,"SSL-Certificate-Issuer",X509_NAME_oneline( X509_get_issuer_name(cert),NULL, 0));
	ptr=X509_NAME_oneline( X509_get_subject_name(cert),NULL, 0);
	STREAMSetValue(S,"SSL-Certificate-Subject", ptr);

	Value=OpenSSLConvertTime(Value, X509_get_notBefore(cert));
	STREAMSetValue(S,"SSL-Certificate-NotBefore", Value); 
	Value=OpenSSLConvertTime(Value, X509_get_notAfter(cert));
	STREAMSetValue(S,"SSL-Certificate-NotAfter", Value);

	ptr=GetNameValuePair(ptr,"/","=",&Name,&Value);
	while (ptr)
	{
		if (StrLen(Name) && (strcmp(Name,"CN")==0)) STREAMSetValue(S,"SSL-Certificate-CommonName",Value);
		ptr=GetNameValuePair(ptr,"/","=",&Name,&Value);
	}

	if (StrValid(S->Path))
	{
	ParseURL(S->Path,NULL,&Name,NULL,NULL,NULL,NULL,NULL);
	val=X509_check_host(cert, Name, StrLen(Name), 0, NULL);
	}
	else val=0;


	if (val!=1)	OpenSSLCertError(S, "Certificate hostname missmatch");
	else
	{
	val=SSL_get_verify_result(ssl);

	switch(val)
	{
		case X509_V_OK: STREAMSetValue(S,"SSL-Certificate-Verify","OK"); RetVal=TRUE; break;
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT: OpenSSLCertError(S,"unable to get issuer"); break;
		case X509_V_ERR_UNABLE_TO_GET_CRL: OpenSSLCertError(S,"unable to get certificate CRL"); break;
		case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE: OpenSSLCertError(S,"unable to decrypt certificate's signature"); break;
		case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE: OpenSSLCertError(S,"unable to decrypt CRL's signature"); break;
		case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY: OpenSSLCertError(S,"unable to decode issuer public key"); break;
		case X509_V_ERR_CERT_SIGNATURE_FAILURE: OpenSSLCertError(S,"certificate signature invalid"); break;
		case X509_V_ERR_CRL_SIGNATURE_FAILURE: OpenSSLCertError(S,"CRL signature invalid"); break;
		case X509_V_ERR_CERT_NOT_YET_VALID: OpenSSLCertError(S,"certificate is not yet valid"); break;
		case X509_V_ERR_CERT_HAS_EXPIRED: OpenSSLCertError(S,"certificate has expired"); break;
		case X509_V_ERR_CRL_NOT_YET_VALID: OpenSSLCertError(S,"CRL is not yet valid"); break;
		case X509_V_ERR_CRL_HAS_EXPIRED: OpenSSLCertError(S,"CRL has expired"); break;
		case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD: OpenSSLCertError(S,"invalid notBefore value"); break;
		case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD: OpenSSLCertError(S,"invalid notAfter value"); break;
		case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD: OpenSSLCertError(S,"invalid CRL lastUpdate value"); break;
		case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD: OpenSSLCertError(S,"invalid CRL nextUpdate value"); break;
		case X509_V_ERR_OUT_OF_MEM: OpenSSLCertError(S,"out of memory"); break;
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT: OpenSSLCertError(S,"self signed certificate"); break;
		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN: OpenSSLCertError(S,"self signed certificate in certificate chain"); break;
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY: OpenSSLCertError(S,"cant find root certificate in local database"); break;
		case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE: OpenSSLCertError(S,"ERROR: unable to verify the first certificate"); break;
		case X509_V_ERR_CERT_CHAIN_TOO_LONG: OpenSSLCertError(S,"certificate chain too long"); break;
		case X509_V_ERR_CERT_REVOKED: OpenSSLCertError(S,"certificate revoked"); break;
		case X509_V_ERR_INVALID_CA: OpenSSLCertError(S,"invalid CA certificate"); break;
		case X509_V_ERR_PATH_LENGTH_EXCEEDED: OpenSSLCertError(S,"path length constraint exceeded"); break;
		case X509_V_ERR_INVALID_PURPOSE: OpenSSLCertError(S,"unsupported certificate purpose"); break;
		case X509_V_ERR_CERT_UNTRUSTED: OpenSSLCertError(S,"certificate not trusted"); break;
		case X509_V_ERR_CERT_REJECTED: OpenSSLCertError(S,"certificate rejected"); break;
		case X509_V_ERR_SUBJECT_ISSUER_MISMATCH: OpenSSLCertError(S,"subject issuer mismatch"); break;
		case X509_V_ERR_AKID_SKID_MISMATCH: OpenSSLCertError(S,"authority and subject key identifier mismatch"); break;
		case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH: OpenSSLCertError(S,"authority and issuer serial number mismatch"); break;
		case X509_V_ERR_KEYUSAGE_NO_CERTSIGN: OpenSSLCertError(S,"key usage does not include certificate signing"); break;
		case X509_V_ERR_APPLICATION_VERIFICATION: OpenSSLCertError(S,"application verification failure"); break;
	}
	}
}
else OpenSSLCertError(S,"peer provided no certificate");


DestroyString(Name);
DestroyString(Value);

#endif

return(RetVal);
}


//ParseLevel allows chosing between the following levels:
// ssl - allow SSLv3 and up
// tls - allow all TLS types
// tls1.1 - allow TLSv1.1 and up
// tls1.2 - allow TLSv.12 and up
// default. Currently equivalent to tls1.2 but may change in future

#ifdef HAVE_LIBSSL
int OpenSSLSetOptions(STREAM *S, SSL *ssl, int Options)
{
const char *ptr;

	//anything before SSLv3 is madness
   Options |= SSL_OP_NO_SSLv2;

   ptr=STREAMGetValue(S,"SSL-Level");
   if (! StrValid(ptr)) ptr=LibUsefulGetValue("SSL-Level");

	if (StrValid(ptr))
	{
	if (strncasecmp(ptr,"ssl",3) !=0)
	{
		Options=SSL_OP_NO_SSLv3;

		if (strncasecmp(ptr,"tls",3) !=0)
		{
			#ifdef SSL_OP_NO_TLSv1
			Options|= SSL_OP_NO_TLSv1;
			#endif

		  if (strcasecmp(ptr,"tls1.1")==0) 
			{
			#ifdef SSL_OP_NO_TLSv1_1
			Options|= SSL_OP_NO_TLSv1_1;
			#endif
			}
		}
	}
	}

	SSL_set_options(ssl, Options);
  return(Options);
}
#endif


int DoSSLClientNegotiation(STREAM *S, int Flags)
{
int result=FALSE, Options=0;
#ifdef HAVE_LIBSSL
const SSL_METHOD *Method;
SSL_CTX *ctx;
SSL *ssl;
//struct x509 *cert=NULL;
const char *ptr;

if (S)
{
	INTERNAL_SSL_INIT();
	//  SSL_load_ciphers();
  Method=SSLv23_client_method();
  ctx=SSL_CTX_new(Method);
  if (! ctx) HandleSSLError();
  else
  {
  STREAM_INTERNAL_SSL_ADD_SECURE_KEYS(S,ctx);

	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, OpenSSLVerifyCallback);
  ssl=SSL_new(ctx);
  SSL_set_fd(ssl,S->in_fd);
  STREAMSetItem(S,"LIBUSEFUL-SSL-CTX",ssl);

  OpenSSLSetOptions(S, ssl, SSL_OP_SINGLE_DH_USE);

	ptr=LibUsefulGetValue("SSL-Permitted-Ciphers");
	if (ptr) SSL_set_cipher_list(ssl, ptr);
  result=SSL_connect(ssl);
	while (result==-1)
	{
	result=SSL_get_error(ssl, result);
	if ( (result!=SSL_ERROR_WANT_READ) && (result != SSL_ERROR_WANT_WRITE) ) break;
	usleep(300);
  result=SSL_connect(ssl);
	}
  S->State |= SS_SSL;

	OpenSSLQueryCipher(S);
	OpenSSLVerifyCertificate(S);
	}
}

#else
RaiseError(0, "DoSSLClientNegotiation", "ssl support not compiled into libUseful");
#endif


return(result);
}


#ifdef HAVE_LIBSSL
void OpenSSLSetupECDH(SSL_CTX *ctx)
{
EC_KEY* ecdh;

ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
//ecdh = EC_KEY_new_by_curve_name( NID_secp384r1);

{
	SSL_CTX_set_tmp_ecdh(ctx, ecdh);
	EC_KEY_free(ecdh);
}

}



void OpenSSLSetupDH(SSL_CTX *ctx)
{
char *Tempstr=NULL;
const char *ptr;
DH *dh=NULL;
FILE *paramfile;

if (CachedDH) dh=CachedDH;
else
{
	ptr=LibUsefulGetValue("SSL-DHParams-File");
	if (StrLen(ptr)) Tempstr=CopyStr(Tempstr,ptr);

	paramfile = fopen(Tempstr, "r");
	if (paramfile) 
	{
 	  CachedDH = PEM_read_DHparams(paramfile, NULL, NULL, NULL);
		dh=CachedDH;
 	  fclose(paramfile);
	}

	if (! dh) 
	{
		//OpenSSLGenerateDHParams();
		dh=CachedDH;
	}
}

if (dh) SSL_CTX_set_tmp_dh(ctx, dh);

//Don't free these parameters, as they are cached
//DH_KEY_free(dh);

DestroyString(Tempstr);
}
#endif




int DoSSLServerNegotiation(STREAM *S, int Flags)
{
int result=FALSE, Options=0;
#ifdef HAVE_LIBSSL
const SSL_METHOD *Method;
SSL_CTX *ctx;
SSL *ssl;
const char *ptr;


if (S)
{
	INTERNAL_SSL_INIT();
  Method=SSLv23_server_method();
  if (Method)
  {
  ctx=SSL_CTX_new(Method);
	  
  if (ctx)
	{
	  STREAM_INTERNAL_SSL_ADD_SECURE_KEYS(S,ctx);
		if (Flags & LU_SSL_PFS) 
		{
			OpenSSLSetupDH(ctx);
			OpenSSLSetupECDH(ctx);
		}
		if (Flags & LU_SSL_VERIFY_PEER) 
		{
			SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, OpenSSLVerifyCallback);
			SSL_CTX_set_verify_depth(ctx,1);
		}
		SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
	  ssl=SSL_new(ctx);
		OpenSSLSetOptions(S, ssl, SSL_OP_SINGLE_DH_USE|SSL_OP_CIPHER_SERVER_PREFERENCE);

	  SSL_set_fd(ssl,S->in_fd);
	  STREAMSetItem(S,"LIBUSEFUL-SSL-CTX",ssl);
		ptr=LibUsefulGetValue("SSL-Permitted-Ciphers");
		if (ptr) SSL_set_cipher_list(ssl, ptr);
	  SSL_set_accept_state(ssl);

		while (1)
		{
	  result=SSL_accept(ssl);
		if (result != TRUE) result=SSL_get_error(ssl,result);

		switch (result)
		{
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE: 
			usleep(300);
			result=-1;
			break;

			case TRUE:
			S->State |= SS_SSL;
			if (Flags & SSL_VERIFY_PEER) OpenSSLVerifyCertificate(S);
			OpenSSLQueryCipher(S);
			break;
 
			default:
			result=ERR_get_error();
			STREAMSetValue(S, "SSL-Error", ERR_error_string(result,NULL));
			result=FALSE;
			break;
		}
		if (result !=-1) break;
		}
	}
  }
}

#else
RaiseError(0, "DoSSLServerNegotiation", "ssl support not compiled into libUseful");
#endif
return(result);
}



int STREAMIsPeerAuth(STREAM *S)
{
#ifdef HAVE_LIBSSL
void *ptr;

ptr=STREAMGetItem(S,"LIBUSEFUL-SSL-CTX");
if (! ptr) return(FALSE);

if (SSL_get_verify_result((SSL *) ptr)==X509_V_OK)
{
  if (SSL_get_peer_certificate((SSL *) ptr) !=NULL) return(TRUE);
}
#endif
return(FALSE);
}


