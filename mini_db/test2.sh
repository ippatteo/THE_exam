#!/usr/bin/env bash
# Client-side tester for mini_db servers.
# Usage:
#   ./client_test_mini_db.sh HOST PORT [--profile post|set] [--maxline 1000]
# Examples:
#   ./client_test_mini_db.sh 127.0.0.1 4242
#   ./client_test_mini_db.sh 127.0.0.1 4242 --profile set
#ATTENZIONE QUESTO TESTERE È GENERATO E TUTTO SMINCHIATO, ALCUNI TEST FALLISCONO A PTIORI

set -uo pipefail

HOST="${1:-}"
PORT="${2:-}"
PROFILE="post"      # 'post' (POST/GET/DELETE + 0/1/2) | 'set' (SET/GET/DEL + OK/NOT FOUND/ERR)
MAXLINE=1000        # limite riga (se serve per test lunghi)
shift 2 || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="${2:-post}"; shift 2 ;;
    --maxline) MAXLINE="${2:-1000}"; shift 2 ;;
    *) echo "Argomento sconosciuto: $1"; exit 2 ;;
  esac
done

if [[ -z "$HOST" || -z "$PORT" ]]; then
  echo "Uso: $0 HOST PORT [--profile post|set] [--maxline N]"
  exit 2
fi

PASS=0; FAIL=0
ok() { echo "✅ PASS: $*"; ((PASS++)); }
ko() { echo "❌ FAIL: $*"; ((FAIL++)); }

# --- Netcat wrapper (gestisce varianti OpenBSD/GNU/Busybox) ---
nc_cmd() {
  local h="$1" p="$2"; shift 2
  if nc -h 2>&1 | grep -q -- '-q '; then
    nc -w 3 -q 1 "$h" "$p" "$@" || true
  else
    nc -w 3 "$h" "$p" "$@" || true
  fi
}

send_line() { # invia una SINGOLA riga e cattura UNA riga di risposta
  local line="$1"
  local out
  out="$(printf "%s\n" "$line" | nc_cmd "$HOST" "$PORT" | tr -d '\r')"
  printf "%s" "$out"
}

send_payload() { # invia più righe e ritorna TUTTE le righe di risposta
  local payload="$1"
  printf "%b" "$payload" | nc_cmd "$HOST" "$PORT" | tr -d '\r'
}

# frammentazione: usa sempre python per maggiore affidabilità
send_fragmented() {
  local frag1="$1" frag2="$2"
  python3 - "$HOST" "$PORT" "$frag1" "$frag2" 2>/dev/null <<'PY' || echo ""
import sys, socket, time
try:
    host, port, f1, f2 = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    s = socket.socket()
    s.settimeout(3.0)
    s.connect((host, port))
    s.sendall(f1.encode('utf-8'))
    time.sleep(0.2)
    s.sendall((f2 + "\n").encode('utf-8'))
    data = s.recv(4096)
    s.close()
    sys.stdout.write(data.decode('utf-8', errors='ignore').replace('\r',''))
except Exception:
    pass
PY
}

repeat_char() { # repeat_char A 10 -> AAAAAAAAAA
  local ch="$1" n="$2"
  python3 - <<PY
ch="$ch"; n=$n
print(ch * n, end="")
PY
}

# --- DSL attese per i due profili ---
exp_ok()      { [[ "$PROFILE" == "post" ]] && echo "0" || echo "OK"; }
exp_notfound(){ [[ "$PROFILE" == "post" ]] && echo "1" || echo "NOT FOUND"; }
exp_err()     { [[ "$PROFILE" == "post" ]] && echo "2" || echo "ERR"; }
cmd_post()    { [[ "$PROFILE" == "post" ]] && echo "POST $1 $2" || echo "SET $1 $2"; }
cmd_get()     { echo "GET $1"; }
cmd_del()     { [[ "$PROFILE" == "post" ]] && echo "DELETE $1" || echo "DEL $1"; }

# GET: attese diverse tra profili
# post: "0 <value>" oppure "1"
# set : "<value>"   oppure "NOT FOUND"
match_get_ok() {
  local got="$1" val="$2"
  if [[ "$PROFILE" == "post" ]]; then
    [[ "$got" == "0 $val" ]]
  else
    [[ "$got" == "$val" ]]
  fi
}
match_get_nf() {
  local got="$1"
  if [[ "$PROFILE" == "post" ]]; then
    [[ "$got" == "$(exp_notfound)" ]]
  else
    [[ "$got" == "NOT FOUND" ]]
  fi
}

# --- TESTS ---------------------------------------------------------------
run_tests() {

  # 1) Sanity: POST/SET, GET ok, GET not-found, DELETE ok, DELETE not-found, comando sconosciuto
  local resp
  resp="$(send_line "$(cmd_post A B)")"
  [[ "$resp" == "$(exp_ok)" ]] && ok "POST/SET A B" || ko "POST/SET A B atteso [$(exp_ok)] (got: [$resp])"

  resp="$(send_line "$(cmd_get A)")"
  if match_get_ok "$resp" "B"; then ok "GET A → B"; else ko "GET A → atteso B (got: [$resp])"; fi

  resp="$(send_line "$(cmd_get C)")"
  if match_get_nf "$resp"; then ok "GET C inesistente"; else ko "GET C inesistente (got: [$resp])"; fi

  resp="$(send_line "$(cmd_del A)")"
  [[ "$resp" == "$(exp_ok)" ]] && ok "DELETE/DEL A" || ko "DELETE/DEL A (got: [$resp])"

  resp="$(send_line "$(cmd_del C)")"
  [[ "$resp" == "$(exp_notfound)" ]] && ok "DELETE/DEL C inesistente" || ko "DELETE/DEL C inesistente (got: [$resp])"

  resp="$(send_line "UNKNOWN_COMMAND")"
  [[ "$resp" == "$(exp_err)" ]] && ok "Comando sconosciuto" || ko "Comando sconosciuto (got: [$resp])"

  # 2) Più comandi in una sola connessione
  local multi payload exp_multi
  payload="$(printf "%s\n%s\n" "$(cmd_post X Y)" "$(cmd_get X)")"
  multi="$(send_payload "$payload")"
  if [[ "$PROFILE" == "post" ]]; then
    exp_multi=$'0\n0 Y'
  else
    exp_multi=$'OK\nY'
  fi
  [[ "$multi" == "$exp_multi" ]] && ok "Multi-comando stessa connessione" || ko "Multi-comando atteso [$exp_multi] (got: [$multi])"

  # 3) Frammentazione TCP (comando spezzato)
  local frag_resp
  if [[ "$PROFILE" == "post" ]]; then
    frag_resp="$(send_fragmented "PO" "ST FOO BAR")"
    [[ "$frag_resp" == "$(exp_ok)" ]] && ok "Segmentation: POST FOO BAR" || ko "Segmentation POST atteso [$(exp_ok)] (got: [$frag_resp])"
  else
    frag_resp="$(send_fragmented "SE" "T FOO BAR")"
    [[ "$frag_resp" == "OK" ]] && ok "Segmentation: SET FOO BAR" || ko "Segmentation SET atteso [OK] (got: [$frag_resp])"
  fi

  # 4) Limite lunghezza riga: costruisco una riga TOTAL <= MAXLINE
  #    formato: "POST " (o "SET ") + key + " " + value + "\n"
  local header spacer=1 nl=1 keylen valuelen total
  if [[ "$PROFILE" == "post" ]]; then header=5; else header=4; fi

  total=$(( MAXLINE - 5 )) # una prova conservativa (MAXLINE-5)
  keylen=$(( total/2 ))
  valuelen=$(( total - header - spacer - nl - keylen ))
  if (( keylen < 1 || valuelen < 1 )); then
    echo "⚠️  Salto test lunghi: MAXLINE troppo basso ($MAXLINE)"; 
  else
    local LKEY LVAL cmd long_resp
    LKEY="$(repeat_char A "$keylen")"
    LVAL="$(repeat_char B "$valuelen")"
    cmd="$(cmd_post "$LKEY" "$LVAL")"
    long_resp="$(send_line "$cmd")"
    [[ "$long_resp" == "$(exp_ok)" ]] && ok "POST/SET riga lunga (<= $MAXLINE)" || ko "POST/SET riga lunga (got: [$long_resp])"

    local get_long
    get_long="$(send_line "$(cmd_get "$LKEY")")"
    if match_get_ok "$get_long" "$LVAL"; then ok "GET chiave lunga → valore lungo"; else ko "GET chiave lunga (got: [$get_long])"; fi
  fi

  # 5) Lowercase comando (deve dare errore)
  local lc_resp
  if [[ "$PROFILE" == "post" ]]; then
    lc_resp="$(send_line "post a b")"
    [[ "$lc_resp" == "2" ]] && ok "lowercase → errore" || ko "lowercase atteso [2] (got: [$lc_resp])"
  else
    lc_resp="$(send_line "set a b")"
    [[ "$lc_resp" == "ERR" ]] && ok "lowercase → errore" || ko "lowercase atteso [ERR] (got: [$lc_resp])"
  fi

  # 6) Concorrenza minima: 3 client in parallelo
  tmp1="$(mktemp)"; tmp2="$(mktemp)"; tmp3="$(mktemp)"
  (
    printf "%s\n%s\n" "$(cmd_post K1 V1)" "$(cmd_get K1)" | nc_cmd "$HOST" "$PORT" | tr -d '\r' >"$tmp1"
  ) & (
    printf "%s\n%s\n" "$(cmd_post K2 V2)" "$(cmd_get K2)" | nc_cmd "$HOST" "$PORT" | tr -d '\r' >"$tmp2"
  ) & (
    printf "%s\n%s\n" "$(cmd_get K1)" "$(cmd_get K2)" | nc_cmd "$HOST" "$PORT" | tr -d '\r' >"$tmp3"
  ) & wait

  if [[ "$PROFILE" == "post" ]]; then
    [[ "$(cat "$tmp1")" == $'0\n0 V1' ]] && ok "Client1 parallel" || ko "Client1 parallel (got: [$(cat "$tmp1")])"
    [[ "$(cat "$tmp2")" == $'0\n0 V2' ]] && ok "Client2 parallel" || ko "Client2 parallel (got: [$(cat "$tmp2")])"
    c3="$(cat "$tmp3")"
    [[ "$c3" == $'0 V1\n0 V2' || "$c3" == $'0 V2\n0 V1' ]] && ok "Client3 parallel" || ko "Client3 parallel (got: [$c3])"
  else
    [[ "$(cat "$tmp1")" == $'OK\nV1' ]] && ok "Client1 parallel" || ko "Client1 parallel (got: [$(cat "$tmp1")])"
    [[ "$(cat "$tmp2")" == $'OK\nV2' ]] && ok "Client2 parallel" || ko "Client2 parallel (got: [$(cat "$tmp2")])"
    c3="$(cat "$tmp3")"
    [[ "$c3" == $'V1\nV2' || "$c3" == $'V2\nV1' ]] && ok "Client3 parallel" || ko "Client3 parallel (got: [$c3])"
  fi
  rm -f "$tmp1" "$tmp2" "$tmp3"

  # 7) GET/DEL su chiave mancante
  local miss
  miss="$(send_line "$(cmd_get __missing__)")"
  if match_get_nf "$miss"; then ok "GET missing"; else ko "GET missing atteso [$(exp_notfound)] (got: [$miss])"; fi

  miss="$(send_line "$(cmd_del __missing__)")"
  [[ "$miss" == "$(exp_notfound)" ]] && ok "DEL/DELETE missing" || ko "DEL/DELETE missing atteso [$(exp_notfound)] (got: [$miss])"
}

run_tests

echo
echo "=== RISULTATO: PASS=$PASS  FAIL=$FAIL ==="
exit $(( FAIL == 0 ? 0 : 1 ))
