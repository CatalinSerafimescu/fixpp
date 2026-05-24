#!/usr/bin/env bash
# gen-chain.sh <num_intermediates> <basename> <startdate> <enddate>
#
# Builds an N-intermediate cert chain rooted at ca.pem/ca.key. The final leaf
# + every intermediate are concatenated into <basename>.pem (root first
# upstream — standard `cat int1 int2 ... intN leaf` order — for SSL_CTX
# `add_extra_chain_cert` consumption order; the leaf itself sits last per
# RFC 5246 §7.4.2 server certificate_list ordering).
#
# Intermediates use the v3_int extension section in ca.conf (basicConstraints
# CA:TRUE, keyUsage keyCertSign+cRLSign). The leaf uses v3_leaf.

set -euo pipefail

NUM_INT="${1:?num_intermediates required}"
BASENAME="${2:?basename required}"
STARTDATE="${3:?startdate required}"
ENDDATE="${4:?enddate required}"

OPENSSL="${OPENSSL:-openssl}"

WORKDIR=$(mktemp -d -t fixpp-chain-XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

# Each link in the chain has its own (ephemeral) CA DB so `openssl ca` can
# sign without conflicting with the root's ca-db/index.txt serial.
make_ca_db() {
  local dir="$1"
  mkdir -p "$dir/newcerts"
  : > "$dir/index.txt"
  echo 1000 > "$dir/serial"
}

# Write a minimal ca.conf per signer that points at $signer.key / $signer.pem.
write_signer_conf() {
  local conf="$1" signer_key="$2" signer_cert="$3" db_dir="$4"
  cat > "$conf" <<EOF
[ ca ]
default_ca = CA_default

[ CA_default ]
dir              = $db_dir
new_certs_dir    = $db_dir/newcerts
database         = $db_dir/index.txt
serial           = $db_dir/serial
certificate      = $signer_cert
private_key      = $signer_key
default_days     = 730
default_md       = sha256
preserve         = no
policy           = policy_any
copy_extensions  = none
unique_subject   = no
email_in_dn      = no
name_opt         = ca_default
cert_opt         = ca_default

[ policy_any ]
commonName             = supplied

[ req ]
distinguished_name = req_dn
prompt             = no

[ req_dn ]
CN = placeholder

[ v3_int ]
basicConstraints       = critical, CA:TRUE
keyUsage               = critical, keyCertSign, cRLSign
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid:always, issuer

[ v3_leaf ]
basicConstraints       = critical, CA:FALSE
keyUsage               = critical, digitalSignature, keyEncipherment
extendedKeyUsage       = clientAuth, serverAuth
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid, issuer
EOF
}

# Generate N intermediates int1..intN rooted at ca.pem.
PREV_CERT="ca.pem"
PREV_KEY="ca.key"

for i in $(seq 1 "$NUM_INT"); do
  INT_KEY="$WORKDIR/int${i}.key"
  INT_CSR="$WORKDIR/int${i}.csr"
  INT_PEM="$WORKDIR/int${i}.pem"
  SIGNER_CONF="$WORKDIR/signer${i}.conf"
  SIGNER_DB="$WORKDIR/db${i}"

  "$OPENSSL" genrsa -out "$INT_KEY" 2048 2>/dev/null
  "$OPENSSL" req -new -key "$INT_KEY" -out "$INT_CSR" \
    -subj "/CN=fixpp-chain-int-${i}"

  make_ca_db "$SIGNER_DB"
  write_signer_conf "$SIGNER_CONF" "$PREV_KEY" "$PREV_CERT" "$SIGNER_DB"

  "$OPENSSL" ca -batch -config "$SIGNER_CONF" -in "$INT_CSR" \
    -out "$INT_PEM" -extensions v3_int \
    -startdate "$STARTDATE" -enddate "$ENDDATE" -notext -md sha256 2>/dev/null

  PREV_CERT="$INT_PEM"
  PREV_KEY="$INT_KEY"
done

# Final leaf signed by the deepest intermediate.
LEAF_KEY="${BASENAME}.key"
LEAF_CSR="$WORKDIR/leaf.csr"
LEAF_PEM="$WORKDIR/leaf.pem"
SIGNER_CONF="$WORKDIR/signer-leaf.conf"
SIGNER_DB="$WORKDIR/db-leaf"

"$OPENSSL" genrsa -out "$LEAF_KEY" 2048 2>/dev/null
"$OPENSSL" req -new -key "$LEAF_KEY" -out "$LEAF_CSR" \
  -subj "/CN=fixpp-chain-leaf-${BASENAME}"

make_ca_db "$SIGNER_DB"
write_signer_conf "$SIGNER_CONF" "$PREV_KEY" "$PREV_CERT" "$SIGNER_DB"

"$OPENSSL" ca -batch -config "$SIGNER_CONF" -in "$LEAF_CSR" \
  -out "$LEAF_PEM" -extensions v3_leaf \
  -startdate "$STARTDATE" -enddate "$ENDDATE" -notext -md sha256 2>/dev/null

# Concatenate: int1, int2, ..., intN, leaf  (root NOT included — it lives in
# ca.pem and is the trust anchor; chain file = intermediates + leaf).
> "${BASENAME}.pem"
for i in $(seq 1 "$NUM_INT"); do
  cat "$WORKDIR/int${i}.pem" >> "${BASENAME}.pem"
done
cat "$LEAF_PEM" >> "${BASENAME}.pem"

echo "wrote ${BASENAME}.pem (chain depth: ${NUM_INT} intermediates + 1 leaf)"
