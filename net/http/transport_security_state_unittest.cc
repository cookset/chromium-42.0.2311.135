// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/transport_security_state.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/rand_util.h"
#include "base/sha1.h"
#include "base/strings/string_piece.h"
#include "crypto/sha2.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/test_completion_callback.h"
#include "net/base/test_data_directory.h"
#include "net/cert/asn1_util.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_cert_types.h"
#include "net/cert/x509_certificate.h"
#include "net/http/http_util.h"
#include "net/ssl/ssl_info.h"
#include "net/test/cert_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_OPENSSL)
#include "crypto/openssl_util.h"
#else
#include "crypto/nss_util.h"
#endif

namespace net {

class TransportSecurityStateTest : public testing::Test {
 public:
  void SetUp() override {
#if defined(USE_OPENSSL)
    crypto::EnsureOpenSSLInit();
#else
    crypto::EnsureNSSInit();
#endif
  }

  static void DisableStaticPins(TransportSecurityState* state) {
    state->enable_static_pins_ = false;
  }

  static void EnableStaticPins(TransportSecurityState* state) {
    state->enable_static_pins_ = true;
  }

  static HashValueVector GetSampleSPKIHashes() {
    HashValueVector spki_hashes;
    HashValue hash(HASH_VALUE_SHA1);
    memset(hash.data(), 0, hash.size());
    spki_hashes.push_back(hash);
    return spki_hashes;
  }

 protected:
  bool GetStaticDomainState(TransportSecurityState* state,
                            const std::string& host,
                            TransportSecurityState::DomainState* result) {
    return state->GetStaticDomainState(host, result);
  }
};

TEST_F(TransportSecurityStateTest, SimpleMatches) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
  bool include_subdomains = false;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("yahoo.com"));
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal("yahoo.com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("foo.yahoo.com"));
  EXPECT_FALSE(state.ShouldSSLErrorsBeFatal("foo.yahoo.com"));
}

TEST_F(TransportSecurityStateTest, MatchesCase1) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
  bool include_subdomains = false;
  state.AddHSTS("YAhoo.coM", expiry, include_subdomains);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("yahoo.com"));
}

TEST_F(TransportSecurityStateTest, Fuzz) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  EnableStaticPins(&state);

  for (size_t i = 0; i < 128; i++) {
    std::string hostname;

    for (;;) {
      if (base::RandInt(0, 16) == 7) {
        break;
      }
      if (i > 0 && base::RandInt(0, 7) == 7) {
        hostname.append(1, '.');
      }
      hostname.append(1, 'a' + base::RandInt(0, 25));
    }
    state.GetStaticDomainState(hostname, &domain_state);
  }
}

TEST_F(TransportSecurityStateTest, MatchesCase2) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("YAhoo.coM"));
  bool include_subdomains = false;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("YAhoo.coM"));
}

TEST_F(TransportSecurityStateTest, SubdomainMatches) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
  bool include_subdomains = true;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("yahoo.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.yahoo.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.bar.yahoo.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.bar.baz.yahoo.com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("notyahoo.com"));
}

// Tests that a more-specific HSTS or HPKP rule overrides a less-specific rule
// with it, regardless of the includeSubDomains bit. This is a regression test
// for https://crbug.com/469957.
TEST_F(TransportSecurityStateTest, SubdomainCarveout) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  const base::Time older = current_time - base::TimeDelta::FromSeconds(1000);

  state.AddHSTS("example1.com", expiry, true);
  state.AddHSTS("foo.example1.com", expiry, false);

  state.AddHPKP("example2.com", expiry, true, GetSampleSPKIHashes());
  state.AddHPKP("foo.example2.com", expiry, false, GetSampleSPKIHashes());

  EXPECT_TRUE(state.ShouldUpgradeToSSL("example1.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.example1.com"));

  // The foo.example1.com rule overrides the example1.com rule, so
  // bar.foo.example1.com has no HSTS state.
  EXPECT_FALSE(state.ShouldUpgradeToSSL("bar.foo.example1.com"));
  EXPECT_FALSE(state.ShouldSSLErrorsBeFatal("bar.foo.example1.com"));

  EXPECT_TRUE(state.HasPublicKeyPins("example2.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("foo.example2.com"));

  // The foo.example2.com rule overrides the example1.com rule, so
  // bar.foo.example2.com has no HPKP state.
  EXPECT_FALSE(state.HasPublicKeyPins("bar.foo.example2.com"));
  EXPECT_FALSE(state.ShouldSSLErrorsBeFatal("bar.foo.example2.com"));

  // Expire the foo.example*.com rules.
  state.AddHSTS("foo.example1.com", older, false);
  state.AddHPKP("foo.example2.com", older, false, GetSampleSPKIHashes());

  // Now the base example*.com rules apply to bar.foo.example*.com.
  EXPECT_TRUE(state.ShouldUpgradeToSSL("bar.foo.example1.com"));
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal("bar.foo.example1.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("bar.foo.example2.com"));
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal("bar.foo.example2.com"));
}

TEST_F(TransportSecurityStateTest, FatalSSLErrors) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  state.AddHSTS("example1.com", expiry, false);
  state.AddHPKP("example2.com", expiry, false, GetSampleSPKIHashes());

  // The presense of either HSTS or HPKP is enough to make SSL errors fatal.
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal("example1.com"));
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal("example2.com"));
}

// Tests that HPKP and HSTS state both expire. Also tests that expired entries
// are pruned.
TEST_F(TransportSecurityStateTest, Expiration) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  const base::Time older = current_time - base::TimeDelta::FromSeconds(1000);

  // Note: this test assumes that inserting an entry with an expiration time in
  // the past works and is pruned on query.
  state.AddHSTS("example1.com", older, false);
  EXPECT_TRUE(TransportSecurityState::Iterator(state).HasNext());
  EXPECT_FALSE(state.ShouldUpgradeToSSL("example1.com"));
  // Querying |state| for a domain should flush out expired entries.
  EXPECT_FALSE(TransportSecurityState::Iterator(state).HasNext());

  state.AddHPKP("example1.com", older, false, GetSampleSPKIHashes());
  EXPECT_TRUE(TransportSecurityState::Iterator(state).HasNext());
  EXPECT_FALSE(state.HasPublicKeyPins("example1.com"));
  // Querying |state| for a domain should flush out expired entries.
  EXPECT_FALSE(TransportSecurityState::Iterator(state).HasNext());

  state.AddHSTS("example1.com", older, false);
  state.AddHPKP("example1.com", older, false, GetSampleSPKIHashes());
  EXPECT_TRUE(TransportSecurityState::Iterator(state).HasNext());
  EXPECT_FALSE(state.ShouldSSLErrorsBeFatal("example1.com"));
  // Querying |state| for a domain should flush out expired entries.
  EXPECT_FALSE(TransportSecurityState::Iterator(state).HasNext());

  // Test that HSTS can outlive HPKP.
  state.AddHSTS("example1.com", expiry, false);
  state.AddHPKP("example1.com", older, false, GetSampleSPKIHashes());
  EXPECT_TRUE(state.ShouldUpgradeToSSL("example1.com"));
  EXPECT_FALSE(state.HasPublicKeyPins("example1.com"));

  // Test that HPKP can outlive HSTS.
  state.AddHSTS("example2.com", older, false);
  state.AddHPKP("example2.com", expiry, false, GetSampleSPKIHashes());
  EXPECT_FALSE(state.ShouldUpgradeToSSL("example2.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("example2.com"));
}

TEST_F(TransportSecurityStateTest, InvalidDomains) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
  bool include_subdomains = true;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("www-.foo.yahoo.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("2\x01.foo.yahoo.com"));
}

// Tests that HPKP and HSTS state are queried independently for subdomain
// matches.
TEST_F(TransportSecurityStateTest, IndependentSubdomain) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  state.AddHSTS("example1.com", expiry, true);
  state.AddHPKP("example1.com", expiry, false, GetSampleSPKIHashes());

  state.AddHSTS("example2.com", expiry, false);
  state.AddHPKP("example2.com", expiry, true, GetSampleSPKIHashes());

  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.example1.com"));
  EXPECT_FALSE(state.HasPublicKeyPins("foo.example1.com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("foo.example2.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("foo.example2.com"));
}

// Tests that HPKP and HSTS state are inserted and overridden independently.
TEST_F(TransportSecurityStateTest, IndependentInsertion) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);

  // Place an includeSubdomains HSTS entry below a normal HPKP entry.
  state.AddHSTS("example1.com", expiry, true);
  state.AddHPKP("foo.example1.com", expiry, false, GetSampleSPKIHashes());

  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.example1.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("foo.example1.com"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL("example1.com"));
  EXPECT_FALSE(state.HasPublicKeyPins("example1.com"));

  // Drop the includeSubdomains from the HSTS entry.
  state.AddHSTS("example1.com", expiry, false);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("foo.example1.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("foo.example1.com"));

  // Place an includeSubdomains HPKP entry below a normal HSTS entry.
  state.AddHSTS("foo.example2.com", expiry, false);
  state.AddHPKP("example2.com", expiry, true, GetSampleSPKIHashes());

  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.example2.com"));
  EXPECT_TRUE(state.HasPublicKeyPins("foo.example2.com"));

  // Drop the includeSubdomains from the HSTS entry.
  state.AddHPKP("example2.com", expiry, false, GetSampleSPKIHashes());

  EXPECT_TRUE(state.ShouldUpgradeToSSL("foo.example2.com"));
  EXPECT_FALSE(state.HasPublicKeyPins("foo.example2.com"));
}

// Tests that GetDynamicDomainState appropriately stitches together the results
// of HSTS and HPKP.
TEST_F(TransportSecurityStateTest, DynamicDomainState) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry1 = current_time + base::TimeDelta::FromSeconds(1000);
  const base::Time expiry2 = current_time + base::TimeDelta::FromSeconds(2000);

  state.AddHSTS("example.com", expiry1, true);
  state.AddHPKP("foo.example.com", expiry2, false, GetSampleSPKIHashes());

  TransportSecurityState::DomainState domain_state;
  ASSERT_TRUE(state.GetDynamicDomainState("foo.example.com", &domain_state));
  EXPECT_TRUE(domain_state.ShouldUpgradeToSSL());
  EXPECT_TRUE(domain_state.HasPublicKeyPins());
  EXPECT_TRUE(domain_state.sts.include_subdomains);
  EXPECT_FALSE(domain_state.pkp.include_subdomains);
  EXPECT_EQ(expiry1, domain_state.sts.expiry);
  EXPECT_EQ(expiry2, domain_state.pkp.expiry);
  EXPECT_EQ("example.com", domain_state.sts.domain);
  EXPECT_EQ("foo.example.com", domain_state.pkp.domain);
}

// Tests that new pins always override previous pins. This should be true for
// both pins at the same domain or includeSubdomains pins at a parent domain.
TEST_F(TransportSecurityStateTest, NewPinsOverride) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  HashValue hash1(HASH_VALUE_SHA1);
  memset(hash1.data(), 0x01, hash1.size());
  HashValue hash2(HASH_VALUE_SHA1);
  memset(hash2.data(), 0x02, hash1.size());
  HashValue hash3(HASH_VALUE_SHA1);
  memset(hash3.data(), 0x03, hash1.size());

  state.AddHPKP("example.com", expiry, true, HashValueVector(1, hash1));

  ASSERT_TRUE(state.GetDynamicDomainState("foo.example.com", &domain_state));
  ASSERT_EQ(1u, domain_state.pkp.spki_hashes.size());
  EXPECT_TRUE(domain_state.pkp.spki_hashes[0].Equals(hash1));

  state.AddHPKP("foo.example.com", expiry, false, HashValueVector(1, hash2));

  ASSERT_TRUE(state.GetDynamicDomainState("foo.example.com", &domain_state));
  ASSERT_EQ(1u, domain_state.pkp.spki_hashes.size());
  EXPECT_TRUE(domain_state.pkp.spki_hashes[0].Equals(hash2));

  state.AddHPKP("foo.example.com", expiry, false, HashValueVector(1, hash3));

  ASSERT_TRUE(state.GetDynamicDomainState("foo.example.com", &domain_state));
  ASSERT_EQ(1u, domain_state.pkp.spki_hashes.size());
  EXPECT_TRUE(domain_state.pkp.spki_hashes[0].Equals(hash3));
}

TEST_F(TransportSecurityStateTest, DeleteAllDynamicDataSince) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  const base::Time older = current_time - base::TimeDelta::FromSeconds(1000);

  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
  bool include_subdomains = false;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);

  state.DeleteAllDynamicDataSince(expiry);
  EXPECT_TRUE(state.ShouldUpgradeToSSL("yahoo.com"));
  state.DeleteAllDynamicDataSince(older);
  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));

  // |state| should be empty now.
  EXPECT_FALSE(TransportSecurityState::Iterator(state).HasNext());
}

TEST_F(TransportSecurityStateTest, DeleteDynamicDataForHost) {
  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  bool include_subdomains = false;
  state.AddHSTS("yahoo.com", expiry, include_subdomains);

  EXPECT_TRUE(state.ShouldUpgradeToSSL("yahoo.com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("example.com"));
  EXPECT_TRUE(state.DeleteDynamicDataForHost("yahoo.com"));
  EXPECT_FALSE(state.ShouldUpgradeToSSL("yahoo.com"));
}

TEST_F(TransportSecurityStateTest, EnableStaticPins) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  EnableStaticPins(&state);

  EXPECT_TRUE(
      state.GetStaticDomainState("chrome.google.com", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
}

TEST_F(TransportSecurityStateTest, DisableStaticPins) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  DisableStaticPins(&state);
  EXPECT_TRUE(
      state.GetStaticDomainState("chrome.google.com", &domain_state));
  EXPECT_TRUE(domain_state.pkp.spki_hashes.empty());
}

TEST_F(TransportSecurityStateTest, IsPreloaded) {
  const std::string paypal = "paypal.com";
  const std::string www_paypal = "www.paypal.com";
  const std::string foo_paypal = "foo.paypal.com";
  const std::string a_www_paypal = "a.www.paypal.com";
  const std::string abc_paypal = "a.b.c.paypal.com";
  const std::string example = "example.com";
  const std::string aypal = "aypal.com";

  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  EXPECT_TRUE(GetStaticDomainState(&state, paypal, &domain_state));
  EXPECT_TRUE(GetStaticDomainState(&state, www_paypal, &domain_state));
  EXPECT_FALSE(domain_state.sts.include_subdomains);
  EXPECT_FALSE(GetStaticDomainState(&state, a_www_paypal, &domain_state));
  EXPECT_FALSE(GetStaticDomainState(&state, abc_paypal, &domain_state));
  EXPECT_FALSE(GetStaticDomainState(&state, example, &domain_state));
  EXPECT_FALSE(GetStaticDomainState(&state, aypal, &domain_state));
}

TEST_F(TransportSecurityStateTest, PreloadedDomainSet) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  // The domain wasn't being set, leading to a blank string in the
  // chrome://net-internals/#hsts UI. So test that.
  EXPECT_TRUE(
      state.GetStaticDomainState("market.android.com", &domain_state));
  EXPECT_EQ(domain_state.sts.domain, "market.android.com");
  EXPECT_EQ(domain_state.pkp.domain, "market.android.com");
  EXPECT_TRUE(state.GetStaticDomainState(
      "sub.market.android.com", &domain_state));
  EXPECT_EQ(domain_state.sts.domain, "market.android.com");
  EXPECT_EQ(domain_state.pkp.domain, "market.android.com");
}

static bool StaticShouldRedirect(const char* hostname) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;
  return state.GetStaticDomainState(
             hostname, &domain_state) &&
         domain_state.ShouldUpgradeToSSL();
}

static bool HasStaticState(const char* hostname) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;
  return state.GetStaticDomainState(hostname, &domain_state);
}

static bool HasStaticPublicKeyPins(const char* hostname) {
  TransportSecurityState state;
  TransportSecurityStateTest::EnableStaticPins(&state);
  TransportSecurityState::DomainState domain_state;
  if (!state.GetStaticDomainState(hostname, &domain_state))
    return false;

  return domain_state.HasPublicKeyPins();
}

static bool OnlyPinningInStaticState(const char* hostname) {
  TransportSecurityState state;
  TransportSecurityStateTest::EnableStaticPins(&state);
  TransportSecurityState::DomainState domain_state;
  if (!state.GetStaticDomainState(hostname, &domain_state))
    return false;

  return (domain_state.pkp.spki_hashes.size() > 0 ||
          domain_state.pkp.bad_spki_hashes.size() > 0) &&
         !domain_state.ShouldUpgradeToSSL();
}

TEST_F(TransportSecurityStateTest, Preloaded) {
  TransportSecurityState state;
  TransportSecurityState::DomainState domain_state;

  // We do more extensive checks for the first domain.
  EXPECT_TRUE(
      state.GetStaticDomainState("www.paypal.com", &domain_state));
  EXPECT_EQ(domain_state.sts.upgrade_mode,
            TransportSecurityState::DomainState::MODE_FORCE_HTTPS);
  EXPECT_FALSE(domain_state.sts.include_subdomains);
  EXPECT_FALSE(domain_state.pkp.include_subdomains);

  EXPECT_TRUE(HasStaticState("paypal.com"));
  EXPECT_FALSE(HasStaticState("www2.paypal.com"));

  // Google hosts:

  EXPECT_TRUE(StaticShouldRedirect("chrome.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("checkout.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("wallet.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("docs.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("sites.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("drive.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("spreadsheets.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("appengine.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("market.android.com"));
  EXPECT_TRUE(StaticShouldRedirect("encrypted.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("accounts.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("profiles.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("mail.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("chatenabled.mail.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("talkgadget.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("hostedtalkgadget.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("talk.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("plus.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("groups.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("apis.google.com"));
  EXPECT_FALSE(StaticShouldRedirect("chart.apis.google.com"));
  EXPECT_TRUE(StaticShouldRedirect("ssl.google-analytics.com"));
  EXPECT_TRUE(StaticShouldRedirect("gmail.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.gmail.com"));
  EXPECT_TRUE(StaticShouldRedirect("googlemail.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.googlemail.com"));
  EXPECT_TRUE(StaticShouldRedirect("googleplex.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.googleplex.com"));

  // These domains used to be only HSTS when SNI was available.
  EXPECT_TRUE(state.GetStaticDomainState("gmail.com", &domain_state));
  EXPECT_TRUE(state.GetStaticDomainState("www.gmail.com", &domain_state));
  EXPECT_TRUE(state.GetStaticDomainState("googlemail.com", &domain_state));
  EXPECT_TRUE(state.GetStaticDomainState("www.googlemail.com", &domain_state));

  // Other hosts:

  EXPECT_TRUE(StaticShouldRedirect("aladdinschools.appspot.com"));

  EXPECT_TRUE(StaticShouldRedirect("ottospora.nl"));
  EXPECT_TRUE(StaticShouldRedirect("www.ottospora.nl"));

  EXPECT_TRUE(StaticShouldRedirect("www.paycheckrecords.com"));

  EXPECT_TRUE(StaticShouldRedirect("lastpass.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.lastpass.com"));
  EXPECT_FALSE(HasStaticState("blog.lastpass.com"));

  EXPECT_TRUE(StaticShouldRedirect("keyerror.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.keyerror.com"));

  EXPECT_TRUE(StaticShouldRedirect("entropia.de"));
  EXPECT_TRUE(StaticShouldRedirect("www.entropia.de"));
  EXPECT_FALSE(HasStaticState("foo.entropia.de"));

  EXPECT_TRUE(StaticShouldRedirect("www.elanex.biz"));
  EXPECT_FALSE(HasStaticState("elanex.biz"));
  EXPECT_FALSE(HasStaticState("foo.elanex.biz"));

  EXPECT_TRUE(StaticShouldRedirect("sunshinepress.org"));
  EXPECT_TRUE(StaticShouldRedirect("www.sunshinepress.org"));
  EXPECT_TRUE(StaticShouldRedirect("a.b.sunshinepress.org"));

  EXPECT_TRUE(StaticShouldRedirect("www.noisebridge.net"));
  EXPECT_FALSE(HasStaticState("noisebridge.net"));
  EXPECT_FALSE(HasStaticState("foo.noisebridge.net"));

  EXPECT_TRUE(StaticShouldRedirect("neg9.org"));
  EXPECT_FALSE(HasStaticState("www.neg9.org"));

  EXPECT_TRUE(StaticShouldRedirect("riseup.net"));
  EXPECT_TRUE(StaticShouldRedirect("foo.riseup.net"));

  EXPECT_TRUE(StaticShouldRedirect("factor.cc"));
  EXPECT_FALSE(HasStaticState("www.factor.cc"));

  EXPECT_TRUE(StaticShouldRedirect("members.mayfirst.org"));
  EXPECT_TRUE(StaticShouldRedirect("support.mayfirst.org"));
  EXPECT_TRUE(StaticShouldRedirect("id.mayfirst.org"));
  EXPECT_TRUE(StaticShouldRedirect("lists.mayfirst.org"));
  EXPECT_FALSE(HasStaticState("www.mayfirst.org"));

  EXPECT_TRUE(StaticShouldRedirect("romab.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.romab.com"));
  EXPECT_TRUE(StaticShouldRedirect("foo.romab.com"));

  EXPECT_TRUE(StaticShouldRedirect("logentries.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.logentries.com"));
  EXPECT_FALSE(HasStaticState("foo.logentries.com"));

  EXPECT_TRUE(StaticShouldRedirect("stripe.com"));
  EXPECT_TRUE(StaticShouldRedirect("foo.stripe.com"));

  EXPECT_TRUE(StaticShouldRedirect("cloudsecurityalliance.org"));
  EXPECT_TRUE(StaticShouldRedirect("foo.cloudsecurityalliance.org"));

  EXPECT_TRUE(StaticShouldRedirect("login.sapo.pt"));
  EXPECT_TRUE(StaticShouldRedirect("foo.login.sapo.pt"));

  EXPECT_TRUE(StaticShouldRedirect("mattmccutchen.net"));
  EXPECT_TRUE(StaticShouldRedirect("foo.mattmccutchen.net"));

  EXPECT_TRUE(StaticShouldRedirect("betnet.fr"));
  EXPECT_TRUE(StaticShouldRedirect("foo.betnet.fr"));

  EXPECT_TRUE(StaticShouldRedirect("uprotect.it"));
  EXPECT_TRUE(StaticShouldRedirect("foo.uprotect.it"));

  EXPECT_TRUE(StaticShouldRedirect("squareup.com"));
  EXPECT_FALSE(HasStaticState("foo.squareup.com"));

  EXPECT_TRUE(StaticShouldRedirect("cert.se"));
  EXPECT_TRUE(StaticShouldRedirect("foo.cert.se"));

  EXPECT_TRUE(StaticShouldRedirect("crypto.is"));
  EXPECT_TRUE(StaticShouldRedirect("foo.crypto.is"));

  EXPECT_TRUE(StaticShouldRedirect("simon.butcher.name"));
  EXPECT_TRUE(StaticShouldRedirect("foo.simon.butcher.name"));

  EXPECT_TRUE(StaticShouldRedirect("linx.net"));
  EXPECT_TRUE(StaticShouldRedirect("foo.linx.net"));

  EXPECT_TRUE(StaticShouldRedirect("dropcam.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.dropcam.com"));
  EXPECT_FALSE(HasStaticState("foo.dropcam.com"));

  EXPECT_TRUE(StaticShouldRedirect("ebanking.indovinabank.com.vn"));
  EXPECT_TRUE(StaticShouldRedirect("foo.ebanking.indovinabank.com.vn"));

  EXPECT_TRUE(StaticShouldRedirect("epoxate.com"));
  EXPECT_FALSE(HasStaticState("foo.epoxate.com"));

  EXPECT_FALSE(HasStaticState("foo.torproject.org"));

  EXPECT_TRUE(StaticShouldRedirect("www.moneybookers.com"));
  EXPECT_FALSE(HasStaticState("moneybookers.com"));

  EXPECT_TRUE(StaticShouldRedirect("ledgerscope.net"));
  EXPECT_TRUE(StaticShouldRedirect("www.ledgerscope.net"));
  EXPECT_FALSE(HasStaticState("status.ledgerscope.net"));

  EXPECT_TRUE(StaticShouldRedirect("foo.app.recurly.com"));
  EXPECT_TRUE(StaticShouldRedirect("foo.api.recurly.com"));

  EXPECT_TRUE(StaticShouldRedirect("greplin.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.greplin.com"));
  EXPECT_FALSE(HasStaticState("foo.greplin.com"));

  EXPECT_TRUE(StaticShouldRedirect("luneta.nearbuysystems.com"));
  EXPECT_TRUE(StaticShouldRedirect("foo.luneta.nearbuysystems.com"));

  EXPECT_TRUE(StaticShouldRedirect("ubertt.org"));
  EXPECT_TRUE(StaticShouldRedirect("foo.ubertt.org"));

  EXPECT_TRUE(StaticShouldRedirect("pixi.me"));
  EXPECT_TRUE(StaticShouldRedirect("www.pixi.me"));

  EXPECT_TRUE(StaticShouldRedirect("grepular.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.grepular.com"));

  EXPECT_TRUE(StaticShouldRedirect("mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.mydigipass.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.www.mydigipass.com"));
  EXPECT_TRUE(StaticShouldRedirect("developer.mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.developer.mydigipass.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.developer.mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.www.developer.mydigipass.com"));
  EXPECT_TRUE(StaticShouldRedirect("sandbox.mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.sandbox.mydigipass.com"));
  EXPECT_TRUE(StaticShouldRedirect("www.sandbox.mydigipass.com"));
  EXPECT_FALSE(StaticShouldRedirect("foo.www.sandbox.mydigipass.com"));

  EXPECT_TRUE(StaticShouldRedirect("bigshinylock.minazo.net"));
  EXPECT_TRUE(StaticShouldRedirect("foo.bigshinylock.minazo.net"));

  EXPECT_TRUE(StaticShouldRedirect("crate.io"));
  EXPECT_TRUE(StaticShouldRedirect("foo.crate.io"));
}

TEST_F(TransportSecurityStateTest, PreloadedPins) {
  TransportSecurityState state;
  EnableStaticPins(&state);
  TransportSecurityState::DomainState domain_state;

  // We do more extensive checks for the first domain.
  EXPECT_TRUE(
      state.GetStaticDomainState("www.paypal.com", &domain_state));
  EXPECT_EQ(domain_state.sts.upgrade_mode,
            TransportSecurityState::DomainState::MODE_FORCE_HTTPS);
  EXPECT_FALSE(domain_state.sts.include_subdomains);
  EXPECT_FALSE(domain_state.pkp.include_subdomains);

  EXPECT_TRUE(OnlyPinningInStaticState("www.google.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("foo.google.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("google.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("www.youtube.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("youtube.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("i.ytimg.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("ytimg.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("googleusercontent.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("www.googleusercontent.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("www.google-analytics.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("googleapis.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("googleadservices.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("googlecode.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("appspot.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("googlesyndication.com"));
  EXPECT_TRUE(OnlyPinningInStaticState("doubleclick.net"));
  EXPECT_TRUE(OnlyPinningInStaticState("googlegroups.com"));

  EXPECT_TRUE(HasStaticPublicKeyPins("torproject.org"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.torproject.org"));
  EXPECT_TRUE(HasStaticPublicKeyPins("check.torproject.org"));
  EXPECT_TRUE(HasStaticPublicKeyPins("blog.torproject.org"));
  EXPECT_FALSE(HasStaticState("foo.torproject.org"));

  EXPECT_TRUE(state.GetStaticDomainState("torproject.org", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(state.GetStaticDomainState("www.torproject.org", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(
      state.GetStaticDomainState("check.torproject.org", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(state.GetStaticDomainState("blog.torproject.org", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());

  EXPECT_TRUE(HasStaticPublicKeyPins("www.twitter.com"));

  // Check that Facebook subdomains have pinning but not HSTS.
  EXPECT_TRUE(state.GetStaticDomainState("facebook.com", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(StaticShouldRedirect("facebook.com"));

  EXPECT_FALSE(state.GetStaticDomainState("foo.facebook.com", &domain_state));

  EXPECT_TRUE(state.GetStaticDomainState("www.facebook.com", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(StaticShouldRedirect("www.facebook.com"));

  EXPECT_TRUE(
      state.GetStaticDomainState("foo.www.facebook.com", &domain_state));
  EXPECT_FALSE(domain_state.pkp.spki_hashes.empty());
  EXPECT_TRUE(StaticShouldRedirect("foo.www.facebook.com"));
}

TEST_F(TransportSecurityStateTest, LongNames) {
  TransportSecurityState state;
  const char kLongName[] =
      "lookupByWaveIdHashAndWaveIdIdAndWaveIdDomainAndWaveletIdIdAnd"
      "WaveletIdDomainAndBlipBlipid";
  TransportSecurityState::DomainState domain_state;
  // Just checks that we don't hit a NOTREACHED.
  EXPECT_FALSE(state.GetStaticDomainState(kLongName, &domain_state));
  EXPECT_FALSE(state.GetDynamicDomainState(kLongName, &domain_state));
}

TEST_F(TransportSecurityStateTest, BuiltinCertPins) {
  TransportSecurityState state;
  EnableStaticPins(&state);
  TransportSecurityState::DomainState domain_state;

  EXPECT_TRUE(
      state.GetStaticDomainState("chrome.google.com", &domain_state));
  EXPECT_TRUE(HasStaticPublicKeyPins("chrome.google.com"));

  HashValueVector hashes;
  std::string failure_log;
  // Checks that a built-in list does exist.
  EXPECT_FALSE(domain_state.CheckPublicKeyPins(hashes, &failure_log));
  EXPECT_FALSE(HasStaticPublicKeyPins("www.paypal.com"));

  EXPECT_TRUE(HasStaticPublicKeyPins("docs.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("1.docs.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("sites.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("drive.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("spreadsheets.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("wallet.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("checkout.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("appengine.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("market.android.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("encrypted.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("accounts.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("profiles.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("mail.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("chatenabled.mail.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("talkgadget.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("hostedtalkgadget.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("talk.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("plus.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("groups.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("apis.google.com"));

  EXPECT_TRUE(HasStaticPublicKeyPins("ssl.gstatic.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("gstatic.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.gstatic.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("ssl.google-analytics.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.googleplex.com"));

  EXPECT_TRUE(HasStaticPublicKeyPins("twitter.com"));
  EXPECT_FALSE(HasStaticPublicKeyPins("foo.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("api.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("oauth.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("mobile.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("dev.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("business.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("platform.twitter.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("si0.twimg.com"));
}

static bool AddHash(const std::string& type_and_base64,
                    HashValueVector* out) {
  HashValue hash;
  if (!hash.FromString(type_and_base64))
    return false;

  out->push_back(hash);
  return true;
}

TEST_F(TransportSecurityStateTest, PinValidationWithoutRejectedCerts) {
  // kGoodPath is blog.torproject.org.
  static const char* const kGoodPath[] = {
    "sha1/m9lHYJYke9k0GtVZ+bXSQYE8nDI=",
    "sha1/o5OZxATDsgmwgcIfIWIneMJ0jkw=",
    "sha1/wHqYaI2J+6sFZAwRfap9ZbjKzE4=",
    NULL,
  };

  // kBadPath is plus.google.com via Trustcenter, which is utterly wrong for
  // torproject.org.
  static const char* const kBadPath[] = {
    "sha1/4BjDjn8v2lWeUFQnqSs0BgbIcrU=",
    "sha1/gzuEEAB/bkqdQS3EIjk2by7lW+k=",
    "sha1/SOZo+SvSspXXR9gjIBBPM5iQn9Q=",
    NULL,
  };

  HashValueVector good_hashes, bad_hashes;

  for (size_t i = 0; kGoodPath[i]; i++) {
    EXPECT_TRUE(AddHash(kGoodPath[i], &good_hashes));
  }
  for (size_t i = 0; kBadPath[i]; i++) {
    EXPECT_TRUE(AddHash(kBadPath[i], &bad_hashes));
  }

  TransportSecurityState state;
  EnableStaticPins(&state);

  TransportSecurityState::DomainState domain_state;
  EXPECT_TRUE(
      state.GetStaticDomainState("blog.torproject.org", &domain_state));
  EXPECT_TRUE(domain_state.HasPublicKeyPins());

  std::string failure_log;
  EXPECT_TRUE(domain_state.CheckPublicKeyPins(good_hashes, &failure_log));
  EXPECT_FALSE(domain_state.CheckPublicKeyPins(bad_hashes, &failure_log));
}

TEST_F(TransportSecurityStateTest, OptionalHSTSCertPins) {
  TransportSecurityState state;
  EnableStaticPins(&state);
  TransportSecurityState::DomainState domain_state;

  EXPECT_FALSE(StaticShouldRedirect("www.google-analytics.com"));

  EXPECT_TRUE(HasStaticPublicKeyPins("www.google-analytics.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.google.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("mail-attachment.googleusercontent.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("www.youtube.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("i.ytimg.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("googleapis.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("ajax.googleapis.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("googleadservices.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("pagead2.googleadservices.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("googlecode.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("kibbles.googlecode.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("appspot.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("googlesyndication.com"));
  EXPECT_TRUE(HasStaticPublicKeyPins("doubleclick.net"));
  EXPECT_TRUE(HasStaticPublicKeyPins("ad.doubleclick.net"));
  EXPECT_FALSE(HasStaticPublicKeyPins("learn.doubleclick.net"));
  EXPECT_TRUE(HasStaticPublicKeyPins("a.googlegroups.com"));
}

TEST_F(TransportSecurityStateTest, OverrideBuiltins) {
  EXPECT_TRUE(HasStaticPublicKeyPins("google.com"));
  EXPECT_FALSE(StaticShouldRedirect("google.com"));
  EXPECT_FALSE(StaticShouldRedirect("www.google.com"));

  TransportSecurityState state;
  const base::Time current_time(base::Time::Now());
  const base::Time expiry = current_time + base::TimeDelta::FromSeconds(1000);
  state.AddHSTS("www.google.com", expiry, true);

  EXPECT_TRUE(state.ShouldUpgradeToSSL("www.google.com"));
}

TEST_F(TransportSecurityStateTest, GooglePinnedProperties) {
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "www.example.com"));
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "www.paypal.com"));
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "mail.twitter.com"));
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "www.google.com.int"));
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "jottit.com"));
  // learn.doubleclick.net has a more specific match than
  // *.doubleclick.com, and has 0 or NULL for its required certs.
  // This test ensures that the exact-match-preferred behavior
  // works.
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "learn.doubleclick.net"));

  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "encrypted.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "mail.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "accounts.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "doubleclick.net"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "ad.doubleclick.net"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "youtube.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "www.profiles.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "checkout.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "googleadservices.com"));

  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "www.example.com"));
  EXPECT_FALSE(TransportSecurityState::IsGooglePinnedProperty(
      "www.paypal.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "checkout.google.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "googleadservices.com"));

  // Test some SNI hosts:
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "gmail.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "googlegroups.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "www.googlegroups.com"));

  // These hosts used to only be HSTS when SNI was available.
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "gmail.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "googlegroups.com"));
  EXPECT_TRUE(TransportSecurityState::IsGooglePinnedProperty(
      "www.googlegroups.com"));
}

}  // namespace net
