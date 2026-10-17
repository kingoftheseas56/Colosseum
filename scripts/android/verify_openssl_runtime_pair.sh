#!/usr/bin/env bash
set -euo pipefail

dir="${1:?usage: verify_openssl_runtime_pair.sh <lib-dir>}"
crypto="$dir/libcrypto_3.so"
ssl="$dir/libssl_3.so"
readelf_bin="${READELF:-readelf}"


for tool in "$readelf_bin"; do
    command -v "$tool" >/dev/null || { echo "[openssl-pair] missing tool: $tool" >&2; exit 2; }
done
for lib in "$crypto" "$ssl"; do
    [[ -f "$lib" ]] || { echo "[openssl-pair] missing runtime library: $lib" >&2; exit 3; }
done

crypto_soname="$("$readelf_bin" -d "$crypto" | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')"
ssl_soname="$("$readelf_bin" -d "$ssl" | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')"
[[ "$crypto_soname" == "libcrypto_3.so" ]] || {
    echo "[openssl-pair] bad crypto SONAME: $crypto_soname" >&2; exit 4;
}
[[ "$ssl_soname" == "libssl_3.so" ]] || {
    echo "[openssl-pair] bad ssl SONAME: $ssl_soname" >&2; exit 5;
}

mapfile -t needed < <("$readelf_bin" -d "$ssl" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p')
printf '%s\n' "${needed[@]}" | grep -Fxq "libcrypto_3.so" || {
    echo "[openssl-pair] libssl_3.so does not DT_NEEDED libcrypto_3.so" >&2
    printf '[openssl-pair] needed: %s\n' "${needed[*]}" >&2
    exit 6
}
if printf '%s\n' "${needed[@]}" | grep -Eq '^libcrypto\.so(\.|$)'; then
    echo "[openssl-pair] libssl_3.so still depends on an unpatched libcrypto.so name" >&2
    exit 7
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/colosseum-openssl-pair.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
"$readelf_bin" -Ws "$crypto" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") {name=$8; sub(/@.*/, "", name); if (name ~ /^ossl_/) print name}' | sort -u > "$tmp/crypto-defined"
"$readelf_bin" -Ws "$ssl" | awk '$7 == "UND" {name=$8; sub(/@.*/, "", name); if (name ~ /^ossl_/) print name}' | sort -u > "$tmp/ssl-undefined"

missing=0
while IFS= read -r symbol; do
    [[ -n "$symbol" ]] || continue
    if ! grep -Fxq "$symbol" "$tmp/crypto-defined"; then
        echo "[openssl-pair] unresolved internal symbol absent from libcrypto_3.so: $symbol" >&2
        missing=1
    fi
done < "$tmp/ssl-undefined"
[[ "$missing" -eq 0 ]] || exit 8

echo "OPENSSL_RUNTIME_PAIR_OK dir=$dir ssl_needed_crypto=1 internal_symbols_resolved=1"
