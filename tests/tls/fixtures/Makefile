# tests/tls/fixtures/Makefile — 011-tls-policy PEM/DER fixtures.
#
# Regenerates every fixture enumerated in plan.md Scale/Scope. Run `make all`
# to (re)build the full set; `make clean` to wipe everything except the
# Makefile + configs. The generated `.pem` / `.der` / `.key` files are checked
# into the repo so tests do not depend on OpenSSL being installed in CI.
#
# Dates are pinned ABSOLUTELY against mock_clock baseline 2026-01-01 (per
# tasks.md T005). `openssl x509 -req` in 3.0.x does NOT expose absolute
# notBefore/notAfter, so every fixture is signed via `openssl ca` which does.
#
# Valid envelope:  notBefore = 2025-01-01,  notAfter = 2027-12-31  (covers
#   the mock_clock baseline so positive fixtures verify at clock->now() ==
#   2026-01-01T00:00:00Z).
# Expired envelope: notBefore = 2024-01-01, notAfter = 2025-12-31 (already
#   expired at the mock_clock baseline → tls_handshake_failed sub-reason
#   "expired").

OPENSSL ?= openssl

VALID_START := 250101000000Z
VALID_END   := 271231235959Z
EXPIRED_START := 240101000000Z
EXPIRED_END   := 251231235959Z

# Order of leaf fixtures (each <name> generates <name>.pem + (where applicable)
# <name>.key). Negative fixtures are leaf-only (no key needed for the test;
# the validation predicate rejects on cert content).
POSITIVE := leaf_rsa2048 leaf_ecdsa_p256 leaf_san_64 leaf_encrypted_pem \
            leaf_multi_san leaf_ed25519
NEGATIVE := leaf_rsa1024_negative leaf_rsa16384_negative leaf_ecdsa_p521_negative \
            leaf_x509v1_negative leaf_san_65_negative leaf_der_16KiB_negative \
            leaf_expired

LEAVES := $(POSITIVE) $(NEGATIVE)

CHAINS := chain_depth_8.pem chain_depth_9_negative.pem

LEAF_PEMS := $(addsuffix .pem,$(LEAVES))

CA_FILES := ca.pem ca.key ca-db/index.txt ca-db/serial

.PHONY: all clean ca leaves chains

all: ca leaves chains

ca: $(CA_FILES)

leaves: $(LEAF_PEMS)

chains: $(CHAINS)

# ── CA setup ──────────────────────────────────────────────────────────────────

ca-db:
	mkdir -p ca-db/newcerts

ca-db/index.txt: | ca-db
	touch ca-db/index.txt

ca-db/serial: | ca-db
	echo 1000 > ca-db/serial

ca.key:
	$(OPENSSL) genrsa -out ca.key 4096

ca.pem: ca.key ca.conf
	$(OPENSSL) req -x509 -new -key ca.key -days 3650 -out ca.pem \
	  -config ca.conf -extensions v3_ca -subj "/CN=fixpp test root CA"

# ── Helpers ───────────────────────────────────────────────────────────────────
# sign-with-ca: $1 = csr file, $2 = output pem, $3 = extensions section,
#               $4 = startdate, $5 = enddate
define sign-with-ca
	$(OPENSSL) ca -batch -config ca.conf -in $(1) -out $(2) \
	  -extensions $(3) -startdate $(4) -enddate $(5) -notext -md sha256
endef

# ── leaf_rsa2048 (positive) ──────────────────────────────────────────────────
leaf_rsa2048.key:
	$(OPENSSL) genrsa -out leaf_rsa2048.key 2048

leaf_rsa2048.csr: leaf_rsa2048.key
	$(OPENSSL) req -new -key leaf_rsa2048.key -out leaf_rsa2048.csr \
	  -subj "/CN=fixpp-leaf-rsa2048"

leaf_rsa2048.pem: leaf_rsa2048.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_rsa2048.csr,leaf_rsa2048.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_rsa1024_negative ────────────────────────────────────────────────────
leaf_rsa1024_negative.key:
	$(OPENSSL) genrsa -out leaf_rsa1024_negative.key 1024

leaf_rsa1024_negative.csr: leaf_rsa1024_negative.key
	$(OPENSSL) req -new -key leaf_rsa1024_negative.key \
	  -out leaf_rsa1024_negative.csr -subj "/CN=fixpp-leaf-rsa1024-negative"

leaf_rsa1024_negative.pem: leaf_rsa1024_negative.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_rsa1024_negative.csr,leaf_rsa1024_negative.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_rsa16384_negative ───────────────────────────────────────────────────
leaf_rsa16384_negative.key:
	$(OPENSSL) genrsa -out leaf_rsa16384_negative.key 16384

leaf_rsa16384_negative.csr: leaf_rsa16384_negative.key
	$(OPENSSL) req -new -key leaf_rsa16384_negative.key \
	  -out leaf_rsa16384_negative.csr -subj "/CN=fixpp-leaf-rsa16384-negative"

leaf_rsa16384_negative.pem: leaf_rsa16384_negative.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_rsa16384_negative.csr,leaf_rsa16384_negative.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_ecdsa_p256 (positive) ───────────────────────────────────────────────
leaf_ecdsa_p256.key:
	$(OPENSSL) ecparam -name prime256v1 -genkey -noout -out leaf_ecdsa_p256.key

leaf_ecdsa_p256.csr: leaf_ecdsa_p256.key
	$(OPENSSL) req -new -key leaf_ecdsa_p256.key -out leaf_ecdsa_p256.csr \
	  -subj "/CN=fixpp-leaf-ecdsa-p256"

leaf_ecdsa_p256.pem: leaf_ecdsa_p256.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_ecdsa_p256.csr,leaf_ecdsa_p256.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_ecdsa_p521_negative ─────────────────────────────────────────────────
leaf_ecdsa_p521_negative.key:
	$(OPENSSL) ecparam -name secp521r1 -genkey -noout -out leaf_ecdsa_p521_negative.key

leaf_ecdsa_p521_negative.csr: leaf_ecdsa_p521_negative.key
	$(OPENSSL) req -new -key leaf_ecdsa_p521_negative.key \
	  -out leaf_ecdsa_p521_negative.csr -subj "/CN=fixpp-leaf-ecdsa-p521-negative"

leaf_ecdsa_p521_negative.pem: leaf_ecdsa_p521_negative.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_ecdsa_p521_negative.csr,leaf_ecdsa_p521_negative.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_x509v1_negative ─────────────────────────────────────────────────────
# v1 has NO extensions. Use openssl req -x509 with no extension section.
leaf_x509v1_negative.key:
	$(OPENSSL) genrsa -out leaf_x509v1_negative.key 2048

leaf_x509v1_negative.pem: leaf_x509v1_negative.key
	$(OPENSSL) req -x509 -new -key leaf_x509v1_negative.key \
	  -out leaf_x509v1_negative.pem -days 730 \
	  -subj "/CN=fixpp-leaf-x509v1-negative" \
	  -config v1.conf

# ── SAN config generators (built on-the-fly to keep the static config small) ─
san_64.conf:
	./gen-san-conf.sh 64 san_64.conf "fixpp-leaf-san-64"

san_65.conf:
	./gen-san-conf.sh 65 san_65.conf "fixpp-leaf-san-65-negative"

san_inflate.conf:
	./gen-san-conf.sh 500 san_inflate.conf "fixpp-leaf-der-16KiB-negative"

# ── leaf_san_64 (positive — at the cap) ──────────────────────────────────────
leaf_san_64.key:
	$(OPENSSL) genrsa -out leaf_san_64.key 2048

leaf_san_64.csr: leaf_san_64.key san_64.conf
	$(OPENSSL) req -new -key leaf_san_64.key -out leaf_san_64.csr \
	  -config san_64.conf

leaf_san_64.pem: leaf_san_64.csr ca.pem ca-db/index.txt ca-db/serial san_64.conf
	$(OPENSSL) ca -batch -config ca.conf -in leaf_san_64.csr \
	  -out leaf_san_64.pem -extfile san_64.conf -extensions v3_san \
	  -startdate $(VALID_START) -enddate $(VALID_END) -notext -md sha256

# ── leaf_san_65_negative (over the cap) ──────────────────────────────────────
leaf_san_65_negative.key:
	$(OPENSSL) genrsa -out leaf_san_65_negative.key 2048

leaf_san_65_negative.csr: leaf_san_65_negative.key san_65.conf
	$(OPENSSL) req -new -key leaf_san_65_negative.key \
	  -out leaf_san_65_negative.csr -config san_65.conf

leaf_san_65_negative.pem: leaf_san_65_negative.csr ca.pem ca-db/index.txt ca-db/serial san_65.conf
	$(OPENSSL) ca -batch -config ca.conf -in leaf_san_65_negative.csr \
	  -out leaf_san_65_negative.pem -extfile san_65.conf -extensions v3_san \
	  -startdate $(VALID_START) -enddate $(VALID_END) -notext -md sha256

# ── leaf_der_16KiB_negative ──────────────────────────────────────────────────
# 16 KiB = 16384 bytes. Inflate with a large subjectAltName list (~500 entries)
# until the encoded DER exceeds the 16 KiB cap. The DER-size check (verify_peer
# step 1) fires BEFORE the SAN-cardinality check (step 6), so the cert needs
# only to be big enough — the SAN count itself does not matter for this seam.
leaf_der_16KiB_negative.key:
	$(OPENSSL) genrsa -out leaf_der_16KiB_negative.key 2048

leaf_der_16KiB_negative.csr: leaf_der_16KiB_negative.key san_inflate.conf
	$(OPENSSL) req -new -key leaf_der_16KiB_negative.key \
	  -out leaf_der_16KiB_negative.csr -config san_inflate.conf

leaf_der_16KiB_negative.pem: leaf_der_16KiB_negative.csr ca.pem ca-db/index.txt ca-db/serial san_inflate.conf
	$(OPENSSL) ca -batch -config ca.conf -in leaf_der_16KiB_negative.csr \
	  -out leaf_der_16KiB_negative.pem -extfile san_inflate.conf -extensions v3_san \
	  -startdate $(VALID_START) -enddate $(VALID_END) -notext -md sha256

# ── leaf_expired ─────────────────────────────────────────────────────────────
leaf_expired.key:
	$(OPENSSL) genrsa -out leaf_expired.key 2048

leaf_expired.csr: leaf_expired.key
	$(OPENSSL) req -new -key leaf_expired.key -out leaf_expired.csr \
	  -subj "/CN=fixpp-leaf-expired"

leaf_expired.pem: leaf_expired.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_expired.csr,leaf_expired.pem,v3_leaf,$(EXPIRED_START),$(EXPIRED_END))

# ── leaf_encrypted_pem (encrypted PEM, passphrase "test") ────────────────────
leaf_encrypted_pem.key:
	$(OPENSSL) genrsa -aes256 -passout pass:test -out leaf_encrypted_pem.key 2048

leaf_encrypted_pem.csr: leaf_encrypted_pem.key
	$(OPENSSL) req -new -key leaf_encrypted_pem.key -passin pass:test \
	  -out leaf_encrypted_pem.csr -subj "/CN=fixpp-leaf-encrypted-pem"

leaf_encrypted_pem.pem: leaf_encrypted_pem.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_encrypted_pem.csr,leaf_encrypted_pem.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── leaf_multi_san (positive — 3 SAN-DNS entries, T023 / FR-014 PMR witness) ─
# Used by T024 (test_verify_peer_pmr_oom.cpp) to exercise mid/tail PMR-OOM sites.
multi_san.conf:
	./gen-san-conf.sh 3 multi_san.conf "fixpp-leaf-multi-san"

leaf_multi_san.key:
	$(OPENSSL) genrsa -out leaf_multi_san.key 2048

leaf_multi_san.csr: leaf_multi_san.key multi_san.conf
	$(OPENSSL) req -new -key leaf_multi_san.key -out leaf_multi_san.csr \
	  -config multi_san.conf

leaf_multi_san.pem: leaf_multi_san.csr ca.pem ca-db/index.txt ca-db/serial multi_san.conf
	$(OPENSSL) ca -batch -config ca.conf -in leaf_multi_san.csr \
	  -out leaf_multi_san.pem -extfile multi_san.conf -extensions v3_san \
	  -startdate $(VALID_START) -enddate $(VALID_END) -notext -md sha256

# ── leaf_ed25519 (positive — Ed25519 key, T019 / FR-012 sigalg_disallowed) ───
# Fixture for [const §XII.3] allow-list rejection test. Ed25519 is outside the
# allow-list (rsa_pss + ecdsa only). Note: the sigalg_disallowed rejection is
# exercised at unit level (test_verify_peer_t039.cpp) — see T020 comment in
# test_tls_validation_failed_taxonomy.cpp for why the full TLS path is infeasible.
leaf_ed25519.key:
	$(OPENSSL) genpkey -algorithm Ed25519 -out leaf_ed25519.key

leaf_ed25519.csr: leaf_ed25519.key
	$(OPENSSL) req -new -key leaf_ed25519.key -out leaf_ed25519.csr \
	  -subj "/CN=fixpp-leaf-ed25519"

leaf_ed25519.pem: leaf_ed25519.csr ca.pem ca-db/index.txt ca-db/serial
	$(call sign-with-ca,leaf_ed25519.csr,leaf_ed25519.pem,v3_leaf,$(VALID_START),$(VALID_END))

# ── chain_depth_8.pem (at the cap) ───────────────────────────────────────────
# 1 root CA (ca.pem) + 7 intermediates + 1 leaf = 9 certs. Per [2g §4.5.1]'s
# chain depth definition (RFC 5280 path-length-style): max_chain_depth = 8
# means up to 8 intermediates BETWEEN leaf and root, exclusive. Here we treat
# the cap as ≤ 8 certs INCLUDING leaf in the chain (intermediate-CA count
# = 7). The negative case extends to 8 intermediates (9 certs).
chain_depth_8.pem: ca.pem
	./gen-chain.sh 7 chain_depth_8 $(VALID_START) $(VALID_END)

chain_depth_9_negative.pem: ca.pem
	./gen-chain.sh 8 chain_depth_9_negative $(VALID_START) $(VALID_END)

# ── clean ────────────────────────────────────────────────────────────────────
clean:
	rm -f *.pem *.key *.csr *.srl *.der multi_san.conf
	rm -rf ca-db
