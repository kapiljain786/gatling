#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/engine.h>

static int library_inited;

/* don't want to fail handshake if cert isn't verifiable */
static int verify_cb(int preverify_ok, X509_STORE_CTX *ctx) { return 1; }

const char* ssl_server_cert="server.pem";
const char* ssl_client_crl="clientcrl.pem";
const char* ssl_client_ca="clientca.pem";
const char* ssl_dhparams="dhparams.pem";
const char* ssl_ciphers="TLSv1+HIGH:!SSLv2:+TLSv1:+SSLv3:RC4+MEDIUM:!aNULL:!eNULL:@STRENGTH";
const char* ssl_client_cert="clientcert.pem";

const char ssl_default_dhparams[]="—---BEGIN DH PARAMETERS-----\n"
"MIIBCAKCAQEAhS4NySChob9OZmB7WOUbOIxurRRbItWnKmC2fq1pJHRft/r72/qq\n"
"g8qquhYAmikXgX4+uZEgfLBWPlx1d8wHggnKtEJ+0KzlGpxek7QORwN2j9872jXC\n"
"25iZar+Om4hUXREuVyGU02GmGHgfemVT1mOvZMbBxzTfmaUdP9Q304oKz4RUYV1w\n"
"+Jv3iO6MYySz6bhsc7lSyayUIJxXJoaqgz6EJVImU6LwXo8gUbD5GUVXhEzDHuRG\n"
"fbKleVvLf1MC7TT6H5PAFFOkfFET//C9QJkSmUsg3u5GtwvKNZhwrggqNzchXSkS\n"
"FDQXPlpTK7h3BlR8vDadEpT68OcdLr2+owIBAg==\n"
"—---END DH PARAMETERS-----\n";

int init_serverside_tls(SSL** ssl,int sock) {
/* taken from the qmail tls patch */
  SSL* myssl;
  SSL_CTX* ctx;
  X509_STORE *store;
  X509_LOOKUP *lookup;
  DH* dh;

  if (!library_inited) {
    library_inited=1;
    SSL_library_init();
    ENGINE_load_builtin_engines();
    SSL_load_error_strings();
  }
  /* a new SSL context with the bare minimum of options */
  if (!(ctx=SSL_CTX_new(SSLv23_server_method()))) {
#if 0
    printf("SSL_CTX_new failed\n");
#endif
    return -1;
  }
  if (!SSL_CTX_use_certificate_chain_file(ctx, ssl_server_cert)) {
    SSL_CTX_free(ctx);
#if 0
    printf("SSL_CTX_use_certificate_chain_file failed\n");
#endif
    return -1;
  }
  SSL_CTX_load_verify_locations(ctx, ssl_client_ca, NULL);
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
  /* crl checking */
  store = SSL_CTX_get_cert_store(ctx);
  if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) &&
      (X509_load_crl_file(lookup, ssl_client_crl, X509_FILETYPE_PEM) == 1))
    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
                                X509_V_FLAG_CRL_CHECK_ALL);
#endif

  /* set the callback here; SSL_set_verify didn't work before 0.9.6c */
  SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, verify_cb);

  {
    BIO* bio=BIO_new_file(ssl_dhparams,"r");
    if (bio==NULL) bio=BIO_new_file("server.pem","r");
    if (bio==NULL)
      bio=BIO_new_mem_buf(ssl_default_dhparams,sizeof(ssl_default_dhparams)-1);
    if (bio) {
      dh=PEM_read_bio_DHparams(bio,NULL,NULL,NULL);
      BIO_free(bio);
      if (dh) SSL_CTX_set_tmp_dh(ctx, dh);
    }
  }

  /* a new SSL object, with the rest added to it directly to avoid copying */
  myssl = SSL_new(ctx);
  SSL_CTX_free(ctx);
  if (!myssl) {
#if 0
    printf("SSL_new failed\n");
#endif
    return -1;
  }

  /* this will also check whether public and private keys match */
  if (!SSL_use_RSAPrivateKey_file(myssl, ssl_server_cert, SSL_FILETYPE_PEM)) {
#if 0
    printf("SSL_use_RSAPrivateKey_file failed\n");
#endif
    SSL_free(myssl);
    return -1;
  }

  {
    char* tmp=getenv("TLSCIPHERS");
    if (tmp) ssl_ciphers=tmp;
  }
  SSL_set_cipher_list(myssl, ssl_ciphers);

#if 0
  SSL_set_tmp_rsa_callback(myssl, tmp_rsa_cb);
  SSL_set_tmp_dh_callback(myssl, tmp_dh_cb);
#endif
#if 0
  SSL_set_rfd(myssl, sock);
  SSL_set_wfd(myssl, sock);
#endif
  SSL_set_fd(myssl, sock);

  *ssl = myssl; /* call SSL_accept(*ssl) next */
  return 0;
}


int init_clientside_tls(SSL** ssl,int sock) {
/* taken from the qmail tls patch */
  SSL* myssl;
  SSL_CTX* ctx;

  if (!library_inited) {
    library_inited=1;
    SSL_library_init();
    ENGINE_load_builtin_engines();
    SSL_load_error_strings();
  }
  if (!(ctx=SSL_CTX_new(SSLv23_client_method()))) return -1;

  if (SSL_CTX_use_certificate_chain_file(ctx, ssl_client_cert))
    SSL_CTX_use_RSAPrivateKey_file(ctx, ssl_client_cert, SSL_FILETYPE_PEM);

  myssl=SSL_new(ctx);
  SSL_CTX_free(ctx);

  if (!myssl) return -1;

  SSL_set_cipher_list(myssl, ssl_ciphers);
  SSL_set_fd(myssl, sock);

  *ssl=myssl; /* call SSL_connect(*ssl) next */
  return 0;
}

