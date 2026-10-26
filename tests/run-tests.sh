#!/bin/bash
# Configuration tests for mod_define.
#
# Each case writes a small configuration, loads it with "httpd -t -D
# DUMP_CONFIG" (mod_info prints the configuration tree after mod_define
# has substituted the variables) and checks the exit status and the
# output. Nothing is installed and no server is started.
#
# Usage: tests/run-tests.sh path/to/mod_define.so
# Set WRAPPER to run httpd under a tool, for example
# WRAPPER="valgrind -q --error-exitcode=99 --errors-for-leak-kinds=none"

set -u

MODULE=$(realpath "${1:?usage: $0 path/to/mod_define.so}")
APXS=${APXS:-apxs}
HTTPD=${HTTPD:-$($APXS -q SBINDIR)/$($APXS -q TARGET)}
WRAPPER=${WRAPPER:-}
MODULES=${MODULES:-$($APXS -q LIBEXECDIR)}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

PASS=0
FAIL=0

# run_case NAME STATUS PATTERN... < config
#   STATUS is "ok" (the configuration loads) or "fail" (it does not).
#   Each PATTERN must appear in the output; "!PATTERN" must not.
run_case() {
    local name=$1 want=$2 got pattern out ok=1
    shift 2
    {
        echo "ServerRoot $WORK"
        echo "ErrorLog /dev/stderr"
        echo "LoadModule mpm_prefork_module $MODULES/mod_mpm_prefork.so"
        echo "LoadModule authz_core_module $MODULES/mod_authz_core.so"
        echo "LoadModule info_module $MODULES/mod_info.so"
        echo "LoadModule macro_module $MODULES/mod_macro.so"
        echo "LoadModule define_module $MODULE"
        echo "ServerName test.invalid"
        cat
    } > "$WORK/$name.conf"
    out=$($WRAPPER "$HTTPD" -f "$WORK/$name.conf" -t -D DUMP_CONFIG 2>&1)
    if [ $? -eq 0 ]; then got=ok; else got=fail; fi
    [ "$got" = "$want" ] || ok=0
    for pattern in "$@"; do
        if [ "${pattern:0:1}" = "!" ]; then
            grep -qF -- "${pattern:1}" <<< "$out" && ok=0
        else
            grep -qF -- "$pattern" <<< "$out" || ok=0
        fi
    done
    if [ $ok -eq 1 ]; then
        PASS=$((PASS + 1))
        echo "ok   $name"
    else
        FAIL=$((FAIL + 1))
        echo "FAIL $name (status $got, expected $want)"
        sed 's/^/     | /' <<< "$out"
    fi
}

run_case basic ok 'ErrorDocument 404 "hello world"' <<'EOF'
DefineStr greeting "hello world"
ErrorDocument 404 "~{greeting}"
EOF

run_case late-binding ok 'ErrorDocument 404 "site"' '!default' <<'EOF'
DefineStr mode default
ErrorDocument 404 "~{mode}"
DefineStr mode site
EOF

run_case used-before-defined ok 'ErrorDocument 404 "later"' <<'EOF'
ErrorDocument 404 "~{late}"
DefineStr late later
EOF

run_case empty-values ok 'ErrorDocument 404 "[][]"' <<'EOF'
DefineStr quoted ""
DefineStr bare
ErrorDocument 404 "[~{quoted}][~{bare}]"
EOF

run_case nested-late ok 'ErrorDocument 404 "phase:2,deny,log"' <<'EOF'
DefineStr action "phase:2,~{disruptive}"
DefineStr disruptive "pass,log"
ErrorDocument 404 "~{action}"
DefineStr disruptive "deny,log"
EOF

run_case case-insensitive ok 'ErrorDocument 404 "v"' <<'EOF'
DefineStr TestRule v
ErrorDocument 404 "~{testrule}"
EOF

run_case name-characters ok 'ErrorDocument 404 "x"' <<'EOF'
DefineStr a-b_c:1 x
ErrorDocument 404 "~{a-b_c:1}"
EOF

run_case literal-tilde ok 'ErrorDocument 404 "~[a] ~/ ~% ~a ~ v"' <<'EOF'
DefineStr a v
ErrorDocument 404 "~[a] ~/ ~% ~a ~ ~{a}"
EOF

run_case escape ok 'ErrorDocument 404 "\~{a} v"' <<'EOF'
DefineStr a v
ErrorDocument 404 "\~{a} ~{a}"
EOF

run_case section-arguments ok '<VirtualHost 127.0.0.1:8080>' 'ServerName v.invalid' <<'EOF'
DefineStr ip 127.0.0.1
DefineStr port 8080
DefineStr vhost v.invalid
<VirtualHost ~{ip}:~{port}>
  ServerName ~{vhost}
</VirtualHost>
EOF

run_case undefined ok 'ErrorDocument 404 "~{missing} v"' \
    "variable 'missing' not defined, left as it is" <<'EOF'
DefineStr a v
ErrorDocument 404 "~{missing} ~{a}"
EOF

run_case illegal-reference ok 'ErrorDocument 404 "~{a b} ~{} v"' \
    "illegal character ' ' in variable reference" \
    "empty variable reference" <<'EOF'
DefineStr a v
ErrorDocument 404 "~{a b} ~{} ~{a}"
EOF

run_case unterminated ok 'ServerAdmin ~{a' \
    'unterminated variable reference' <<'EOF'
DefineStr a v
ServerAdmin ~{a
EOF

run_case loop fail 'more than 200 nested variables' <<'EOF'
DefineStr a "x~{b}"
DefineStr b "~{a}"
ErrorDocument 404 "~{a}"
EOF

run_case max-recursion fail 'more than 2 nested variables' <<'EOF'
DefineMaxRecursion 2
DefineStr a "~{b}"
DefineStr b "~{c}"
DefineStr c "~{d}"
DefineStr d x
ErrorDocument 404 "~{a}"
EOF

run_case max-recursion-enough ok 'ErrorDocument 404 "x"' <<'EOF'
DefineMaxRecursion 4
DefineStr a "~{b}"
DefineStr b "~{c}"
DefineStr c "~{d}"
DefineStr d x
ErrorDocument 404 "~{a}"
EOF

run_case max-string fail 'line longer than 20 characters' <<'EOF'
DefineMaxString 20
DefineStr long "0123456789012345678901234567890"
ErrorDocument 404 "~{long}"
EOF

run_case meta-characters ok 'ErrorDocument 404 "v ~{a} %x !%[a]"' <<'EOF'
DefineMetaDelim %
DefineMetaBraceOpen [
DefineMetaBraceClose ]
DefineMetaEscape !
DefineStr a v
ErrorDocument 404 "%[a] ~{a} %x !%[a]"
EOF

run_case meta-same fail 'the four meta characters must differ' <<'EOF'
DefineMetaDelim {
EOF

run_case meta-invalid fail 'DefineMetaDelim takes one character' <<'EOF'
DefineMetaDelim ab
EOF

run_case invalid-name fail "illegal character '.' in variable name" <<'EOF'
DefineStr a.b x
EOF

run_case macro-arguments ok 'ErrorDocument 404 "one two|last"' <<'EOF'
DefineStr spaced "one two"
<Macro Show $p $q>
  ErrorDocument 404 "$p|$q"
</Macro>
Use Show ~{spaced} last
EOF

run_case macro-sets-global ok 'ErrorDocument 404 "from-macro"' <<'EOF'
<Macro SetMode $v>
  DefineStr mode $v
</Macro>
ErrorDocument 404 "~{mode}"
Use SetMode from-macro
EOF

run_case feature-defines ok 'ErrorDocument 404 "features"' <<'EOF'
<IfDefine MODDEFINE_EmptyValue>
<IfDefine MODDEFINE_MaxRecursion>
<IfDefine MODDEFINE_MaxString>
ErrorDocument 404 "features"
</IfDefine>
</IfDefine>
</IfDefine>
EOF

run_case core-define-coexists ok 'ErrorDocument 404 "core mine"' <<'EOF'
Define coreval core
DefineStr mine mine
ErrorDocument 404 "${coreval} ~{mine}"
EOF

echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
