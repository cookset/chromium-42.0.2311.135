/*
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005. 
 */
/* ====================================================================
 * Copyright (c) 1999-2007 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <assert.h>
#include <stdio.h>

#include <openssl/bn.h>
#include <openssl/buf.h>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/mem.h>
#include <openssl/obj.h>
#include <openssl/rand.h>

#include "ssl_locl.h"

static int dtls1_get_hello_verify(SSL *s);

int dtls1_connect(SSL *s) {
  BUF_MEM *buf = NULL;
  void (*cb)(const SSL *ssl, int type, int val) = NULL;
  int ret = -1;
  int new_state, state, skip = 0;

  assert(s->handshake_func == dtls1_connect);
  assert(!s->server);
  assert(SSL_IS_DTLS(s));

  ERR_clear_error();
  ERR_clear_system_error();

  if (s->info_callback != NULL) {
    cb = s->info_callback;
  } else if (s->ctx->info_callback != NULL) {
    cb = s->ctx->info_callback;
  }

  s->in_handshake++;

  for (;;) {
    state = s->state;

    switch (s->state) {
      case SSL_ST_RENEGOTIATE:
        s->renegotiate = 1;
        s->state = SSL_ST_CONNECT;
        s->ctx->stats.sess_connect_renegotiate++;
      /* break */
      case SSL_ST_CONNECT:
      case SSL_ST_BEFORE | SSL_ST_CONNECT:
        if (cb != NULL) {
          cb(s, SSL_CB_HANDSHAKE_START, 1);
        }

        if (s->init_buf == NULL) {
          buf = BUF_MEM_new();
          if (buf == NULL ||
              !BUF_MEM_grow(buf, SSL3_RT_MAX_PLAIN_LENGTH)) {
            ret = -1;
            goto end;
          }
          s->init_buf = buf;
          buf = NULL;
        }

        if (!ssl3_setup_buffers(s) ||
            !ssl_init_wbio_buffer(s, 0)) {
          ret = -1;
          goto end;
        }

        /* don't push the buffering BIO quite yet */

        s->state = SSL3_ST_CW_CLNT_HELLO_A;
        s->ctx->stats.sess_connect++;
        s->init_num = 0;
        s->d1->send_cookie = 0;
        s->hit = 0;
        break;

      case SSL3_ST_CW_CLNT_HELLO_A:
      case SSL3_ST_CW_CLNT_HELLO_B:
        s->shutdown = 0;

        /* every DTLS ClientHello resets Finished MAC */
        if (!ssl3_init_finished_mac(s)) {
          OPENSSL_PUT_ERROR(SSL, dtls1_connect, ERR_R_INTERNAL_ERROR);
          ret = -1;
          goto end;
        }

        dtls1_start_timer(s);
        ret = ssl3_send_client_hello(s);
        if (ret <= 0) {
          goto end;
        }

        if (s->d1->send_cookie) {
          s->state = SSL3_ST_CW_FLUSH;
          s->s3->tmp.next_state = SSL3_ST_CR_SRVR_HELLO_A;
        } else {
          s->state = DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A;
        }

        s->init_num = 0;
        /* turn on buffering for the next lot of output */
        if (s->bbio != s->wbio) {
          s->wbio = BIO_push(s->bbio, s->wbio);
        }

        break;

      case DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A:
      case DTLS1_ST_CR_HELLO_VERIFY_REQUEST_B:
        ret = dtls1_get_hello_verify(s);
        if (ret <= 0) {
          goto end;
        }
        if (s->d1->send_cookie) {
          /* start again, with a cookie */
          dtls1_stop_timer(s);
          s->state = SSL3_ST_CW_CLNT_HELLO_A;
        } else {
          s->state = SSL3_ST_CR_SRVR_HELLO_A;
        }
        s->init_num = 0;
        break;

      case SSL3_ST_CR_SRVR_HELLO_A:
      case SSL3_ST_CR_SRVR_HELLO_B:
        ret = ssl3_get_server_hello(s);
        if (ret <= 0) {
          goto end;
        }

        if (s->hit) {
          s->state = SSL3_ST_CR_FINISHED_A;
          if (s->tlsext_ticket_expected) {
            /* receive renewed session ticket */
            s->state = SSL3_ST_CR_SESSION_TICKET_A;
          }
        } else {
          s->state = SSL3_ST_CR_CERT_A;
        }
        s->init_num = 0;
        break;

      case SSL3_ST_CR_CERT_A:
      case SSL3_ST_CR_CERT_B:
        if (ssl_cipher_has_server_public_key(s->s3->tmp.new_cipher)) {
          ret = ssl3_get_server_certificate(s);
          if (ret <= 0) {
            goto end;
          }
          if (s->s3->tmp.certificate_status_expected) {
            s->state = SSL3_ST_CR_CERT_STATUS_A;
          } else {
            s->state = SSL3_ST_CR_KEY_EXCH_A;
          }
        } else {
          skip = 1;
          s->state = SSL3_ST_CR_KEY_EXCH_A;
        }
        s->init_num = 0;
        break;

      case SSL3_ST_CR_KEY_EXCH_A:
      case SSL3_ST_CR_KEY_EXCH_B:
        ret = ssl3_get_server_key_exchange(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CR_CERT_REQ_A;
        s->init_num = 0;

        /* at this point we check that we have the
         * required stuff from the server */
        if (!ssl3_check_cert_and_algorithm(s)) {
          ret = -1;
          goto end;
        }
        break;

      case SSL3_ST_CR_CERT_REQ_A:
      case SSL3_ST_CR_CERT_REQ_B:
        ret = ssl3_get_certificate_request(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CR_SRVR_DONE_A;
        s->init_num = 0;
        break;

      case SSL3_ST_CR_SRVR_DONE_A:
      case SSL3_ST_CR_SRVR_DONE_B:
        ret = ssl3_get_server_done(s);
        if (ret <= 0) {
          goto end;
        }
        dtls1_stop_timer(s);
        if (s->s3->tmp.cert_req) {
          s->s3->tmp.next_state = SSL3_ST_CW_CERT_A;
        } else {
          s->s3->tmp.next_state = SSL3_ST_CW_KEY_EXCH_A;
        }
        s->init_num = 0;
        s->state = s->s3->tmp.next_state;
        break;

      case SSL3_ST_CW_CERT_A:
      case SSL3_ST_CW_CERT_B:
      case SSL3_ST_CW_CERT_C:
      case SSL3_ST_CW_CERT_D:
        dtls1_start_timer(s);
        ret = ssl3_send_client_certificate(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CW_KEY_EXCH_A;
        s->init_num = 0;
        break;

      case SSL3_ST_CW_KEY_EXCH_A:
      case SSL3_ST_CW_KEY_EXCH_B:
        dtls1_start_timer(s);
        ret = ssl3_send_client_key_exchange(s);
        if (ret <= 0) {
          goto end;
        }
        /* For TLS, cert_req is set to 2, so a cert chain
         * of nothing is sent, but no verify packet is sent */
        if (s->s3->tmp.cert_req == 1) {
          s->state = SSL3_ST_CW_CERT_VRFY_A;
        } else {
          s->state = SSL3_ST_CW_CHANGE_A;
          s->s3->change_cipher_spec = 0;
        }

        s->init_num = 0;
        break;

      case SSL3_ST_CW_CERT_VRFY_A:
      case SSL3_ST_CW_CERT_VRFY_B:
        dtls1_start_timer(s);
        ret = ssl3_send_cert_verify(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CW_CHANGE_A;
        s->init_num = 0;
        s->s3->change_cipher_spec = 0;
        break;

      case SSL3_ST_CW_CHANGE_A:
      case SSL3_ST_CW_CHANGE_B:
        if (!s->hit) {
          dtls1_start_timer(s);
        }
        ret = dtls1_send_change_cipher_spec(s, SSL3_ST_CW_CHANGE_A,
                                            SSL3_ST_CW_CHANGE_B);
        if (ret <= 0) {
          goto end;
        }

        s->state = SSL3_ST_CW_FINISHED_A;
        s->init_num = 0;

        s->session->cipher = s->s3->tmp.new_cipher;
        if (!s->enc_method->setup_key_block(s) ||
            !s->enc_method->change_cipher_state(
                s, SSL3_CHANGE_CIPHER_CLIENT_WRITE)) {
          ret = -1;
          goto end;
        }

        dtls1_reset_seq_numbers(s, SSL3_CC_WRITE);
        break;

      case SSL3_ST_CW_FINISHED_A:
      case SSL3_ST_CW_FINISHED_B:
        if (!s->hit) {
          dtls1_start_timer(s);
        }

        ret =
            ssl3_send_finished(s, SSL3_ST_CW_FINISHED_A, SSL3_ST_CW_FINISHED_B,
                               s->enc_method->client_finished_label,
                               s->enc_method->client_finished_label_len);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CW_FLUSH;

        if (s->hit) {
          s->s3->tmp.next_state = SSL_ST_OK;
        } else {
          /* Allow NewSessionTicket if ticket expected */
          if (s->tlsext_ticket_expected) {
            s->s3->tmp.next_state = SSL3_ST_CR_SESSION_TICKET_A;
          } else {
            s->s3->tmp.next_state = SSL3_ST_CR_FINISHED_A;
          }
        }
        s->init_num = 0;
        break;

      case SSL3_ST_CR_SESSION_TICKET_A:
      case SSL3_ST_CR_SESSION_TICKET_B:
        ret = ssl3_get_new_session_ticket(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CR_FINISHED_A;
        s->init_num = 0;
        break;

      case SSL3_ST_CR_CERT_STATUS_A:
      case SSL3_ST_CR_CERT_STATUS_B:
        ret = ssl3_get_cert_status(s);
        if (ret <= 0) {
          goto end;
        }
        s->state = SSL3_ST_CR_KEY_EXCH_A;
        s->init_num = 0;
        break;

      case SSL3_ST_CR_FINISHED_A:
      case SSL3_ST_CR_FINISHED_B:
        s->d1->change_cipher_spec_ok = 1;
        ret =
            ssl3_get_finished(s, SSL3_ST_CR_FINISHED_A, SSL3_ST_CR_FINISHED_B);
        if (ret <= 0) {
          goto end;
        }
        dtls1_stop_timer(s);

        if (s->hit) {
          s->state = SSL3_ST_CW_CHANGE_A;
        } else {
          s->state = SSL_ST_OK;
        }

        s->init_num = 0;
        break;

      case SSL3_ST_CW_FLUSH:
        s->rwstate = SSL_WRITING;
        if (BIO_flush(s->wbio) <= 0) {
          /* If the write error was fatal, stop trying */
          if (!BIO_should_retry(s->wbio)) {
            s->rwstate = SSL_NOTHING;
            s->state = s->s3->tmp.next_state;
          }

          ret = -1;
          goto end;
        }
        s->rwstate = SSL_NOTHING;
        s->state = s->s3->tmp.next_state;
        break;

      case SSL_ST_OK:
        /* clean a few things up */
        ssl3_cleanup_key_block(s);

        /* Remove write buffering now. */
        ssl_free_wbio_buffer(s);

        s->init_num = 0;
        s->renegotiate = 0;
        s->new_session = 0;

        ssl_update_cache(s, SSL_SESS_CACHE_CLIENT);
        if (s->hit) {
          s->ctx->stats.sess_hit++;
        }

        ret = 1;
        s->ctx->stats.sess_connect_good++;

        if (cb != NULL) {
          cb(s, SSL_CB_HANDSHAKE_DONE, 1);
        }

        /* done with handshaking */
        s->d1->handshake_read_seq = 0;
        s->d1->next_handshake_write_seq = 0;
        goto end;

      default:
        OPENSSL_PUT_ERROR(SSL, dtls1_connect, SSL_R_UNKNOWN_STATE);
        ret = -1;
        goto end;
    }

    /* did we do anything? */
    if (!s->s3->tmp.reuse_message && !skip) {
      if ((cb != NULL) && (s->state != state)) {
        new_state = s->state;
        s->state = state;
        cb(s, SSL_CB_CONNECT_LOOP, 1);
        s->state = new_state;
      }
    }
    skip = 0;
  }

end:
  s->in_handshake--;

  if (buf != NULL) {
    BUF_MEM_free(buf);
  }
  if (cb != NULL) {
    cb(s, SSL_CB_CONNECT_EXIT, ret);
  }
  return ret;
}

static int dtls1_get_hello_verify(SSL *s) {
  long n;
  int al, ok = 0;
  CBS hello_verify_request, cookie;
  uint16_t server_version;

  n = s->method->ssl_get_message(
      s, DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A, DTLS1_ST_CR_HELLO_VERIFY_REQUEST_B,
      -1,
      /* Use the same maximum size as ssl3_get_server_hello. */
      20000, SSL_GET_MESSAGE_HASH_MESSAGE, &ok);

  if (!ok) {
    return n;
  }

  if (s->s3->tmp.message_type != DTLS1_MT_HELLO_VERIFY_REQUEST) {
    s->d1->send_cookie = 0;
    s->s3->tmp.reuse_message = 1;
    return 1;
  }

  CBS_init(&hello_verify_request, s->init_msg, n);

  if (!CBS_get_u16(&hello_verify_request, &server_version) ||
      !CBS_get_u8_length_prefixed(&hello_verify_request, &cookie) ||
      CBS_len(&hello_verify_request) != 0) {
    al = SSL_AD_DECODE_ERROR;
    OPENSSL_PUT_ERROR(SSL, ssl3_get_cert_status, SSL_R_DECODE_ERROR);
    goto f_err;
  }

  if (CBS_len(&cookie) > sizeof(s->d1->cookie)) {
    al = SSL_AD_ILLEGAL_PARAMETER;
    goto f_err;
  }

  memcpy(s->d1->cookie, CBS_data(&cookie), CBS_len(&cookie));
  s->d1->cookie_len = CBS_len(&cookie);

  s->d1->send_cookie = 1;
  return 1;

f_err:
  ssl3_send_alert(s, SSL3_AL_FATAL, al);
  return -1;
}
