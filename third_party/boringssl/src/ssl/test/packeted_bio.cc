/* Copyright (c) 2014, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include "packeted_bio.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <openssl/mem.h>


namespace {

extern const BIO_METHOD g_packeted_bio_method;

const uint8_t kOpcodePacket = 'P';
const uint8_t kOpcodeTimeout = 'T';
const uint8_t kOpcodeTimeoutAck = 't';

static int PacketedWrite(BIO *bio, const char *in, int inl) {
  if (bio->next_bio == NULL) {
    return 0;
  }

  BIO_clear_retry_flags(bio);

  // Write the header.
  uint8_t header[5];
  header[0] = kOpcodePacket;
  header[1] = (inl >> 24) & 0xff;
  header[2] = (inl >> 16) & 0xff;
  header[3] = (inl >> 8) & 0xff;
  header[4] = inl & 0xff;
  int ret = BIO_write(bio->next_bio, header, sizeof(header));
  if (ret <= 0) {
    BIO_copy_next_retry(bio);
    return ret;
  }

  // Write the buffer. BIOs for which this operation fails are not supported.
  ret = BIO_write(bio->next_bio, in, inl);
  assert(ret == inl);
  return ret;
}

static int PacketedRead(BIO *bio, char *out, int outl) {
  if (bio->next_bio == NULL) {
    return 0;
  }

  BIO_clear_retry_flags(bio);

  // Read the opcode.
  uint8_t opcode;
  int ret = BIO_read(bio->next_bio, &opcode, sizeof(opcode));
  if (ret <= 0) {
    BIO_copy_next_retry(bio);
    return ret;
  }
  assert(static_cast<size_t>(ret) == sizeof(opcode));

  if (opcode == kOpcodeTimeout) {
    // Process the timeout.
    uint8_t buf[8];
    ret = BIO_read(bio->next_bio, &buf, sizeof(buf));
    if (ret <= 0) {
      BIO_copy_next_retry(bio);
      return ret;
    }
    assert(static_cast<size_t>(ret) == sizeof(buf));
    uint64_t timeout = (static_cast<uint64_t>(buf[0]) << 56) |
        (static_cast<uint64_t>(buf[1]) << 48) |
        (static_cast<uint64_t>(buf[2]) << 40) |
        (static_cast<uint64_t>(buf[3]) << 32) |
        (static_cast<uint64_t>(buf[4]) << 24) |
        (static_cast<uint64_t>(buf[5]) << 16) |
        (static_cast<uint64_t>(buf[6]) << 8) |
        static_cast<uint64_t>(buf[7]);
    timeout /= 1000;  // Convert nanoseconds to microseconds.
    OPENSSL_timeval *out_timeout =
        reinterpret_cast<OPENSSL_timeval *>(bio->ptr);
    assert(out_timeout->tv_usec == 0);
    assert(out_timeout->tv_sec == 0);
    out_timeout->tv_usec = timeout % 1000000;
    out_timeout->tv_sec = timeout / 1000000;

    // Send an ACK to the peer.
    ret = BIO_write(bio->next_bio, &kOpcodeTimeoutAck, 1);
    assert(ret == 1);

    // Signal to the caller to retry the read, after processing the
    // new clock.
    BIO_set_retry_read(bio);
    return -1;
  }

  if (opcode != kOpcodePacket) {
    fprintf(stderr, "Unknown opcode, %u\n", opcode);
    return -1;
  }

  // Read the length prefix.
  uint8_t len_bytes[4];
  ret = BIO_read(bio->next_bio, &len_bytes, sizeof(len_bytes));
  if (ret <= 0) {
    BIO_copy_next_retry(bio);
    return ret;
  }
  // BIOs for which a partial length comes back are not supported.
  assert(static_cast<size_t>(ret) == sizeof(len_bytes));

  uint32_t len = (len_bytes[0] << 24) | (len_bytes[1] << 16) |
      (len_bytes[2] << 8) | len_bytes[3];
  char *buf = (char *)OPENSSL_malloc(len);
  if (buf == NULL) {
    return -1;
  }
  ret = BIO_read(bio->next_bio, buf, len);
  assert(ret == (int)len);

  if (outl > (int)len) {
    outl = len;
  }
  memcpy(out, buf, outl);
  OPENSSL_free(buf);
  return outl;
}

static long PacketedCtrl(BIO *bio, int cmd, long num, void *ptr) {
  if (bio->next_bio == NULL) {
    return 0;
  }
  BIO_clear_retry_flags(bio);
  int ret = BIO_ctrl(bio->next_bio, cmd, num, ptr);
  BIO_copy_next_retry(bio);
  return ret;
}

static int PacketedNew(BIO *bio) {
  bio->init = 1;
  return 1;
}

static int PacketedFree(BIO *bio) {
  if (bio == NULL) {
    return 0;
  }

  bio->init = 0;
  return 1;
}

static long PacketedCallbackCtrl(BIO *bio, int cmd, bio_info_cb fp) {
  if (bio->next_bio == NULL) {
    return 0;
  }
  return BIO_callback_ctrl(bio->next_bio, cmd, fp);
}

const BIO_METHOD g_packeted_bio_method = {
  BIO_TYPE_FILTER,
  "packeted bio",
  PacketedWrite,
  PacketedRead,
  NULL /* puts */,
  NULL /* gets */,
  PacketedCtrl,
  PacketedNew,
  PacketedFree,
  PacketedCallbackCtrl,
};

}  // namespace

ScopedBIO PacketedBioCreate(OPENSSL_timeval *out_timeout) {
  ScopedBIO bio(BIO_new(&g_packeted_bio_method));
  if (!bio) {
    return nullptr;
  }
  bio->ptr = out_timeout;
  return bio;
}
