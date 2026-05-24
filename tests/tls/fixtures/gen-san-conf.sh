#!/usr/bin/env bash
# gen-san-conf.sh <N> <output.conf> <CN>
#
# Emits an OpenSSL req config with N subjectAltName DNS entries. Used by the
# Makefile to build leaf_san_64 / leaf_san_65_negative / leaf_der_16KiB_negative.

set -euo pipefail

N="${1:?N required}"
OUT="${2:?output path required}"
CN="${3:?CN required}"

{
  echo "[ req ]"
  echo "distinguished_name = req_dn"
  echo "req_extensions     = v3_san"
  echo "prompt             = no"
  echo
  echo "[ req_dn ]"
  echo "CN = $CN"
  echo
  echo "[ v3_san ]"
  echo "basicConstraints       = critical, CA:FALSE"
  echo "keyUsage               = critical, digitalSignature, keyEncipherment"
  echo "extendedKeyUsage       = clientAuth, serverAuth"
  echo "subjectKeyIdentifier   = hash"
  echo "subjectAltName         = @alt_names"
  echo
  echo "[ alt_names ]"
  for i in $(seq 1 "$N"); do
    # Long DNS labels to inflate DER for the 16 KiB case; short enough that
    # 64 entries stays comfortably under 1 KiB of SAN data otherwise.
    echo "DNS.$i = host-${i}-fixpp-test-fixtures-very-long-subdomain.fixpp.example.invalid"
  done
} > "$OUT"
