#!/usr/bin/env bash
#
# Discovery: run our zserio-c generator extension on every schema in the
# upstream `ndsev/zserio-test-data` submodule under language/ and others/.
# Tally pass/fail and the most common rejection reasons.
#
# Advisory by default — the script returns 0 even when schemas fail to
# generate, so the calling CI job can post the summary as feedback
# without red-failing on known-unsupported features. Pass --strict to
# return a non-zero exit code if any schema fails (intended for use once
# the corpus is fully covered).

set -uo pipefail

STRICT=0
if [[ "${1:-}" == "--strict" ]]; then
    STRICT=1
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="${REPO_ROOT}/third_party/zserio-test-data"

if [[ ! -d "${DATA_DIR}/language" ]]; then
    echo "discover: zserio-test-data submodule is not initialised at ${DATA_DIR}" >&2
    echo "discover: run 'git submodule update --init third_party/zserio-test-data'" >&2
    exit 2
fi

CORE_JAR="$("${REPO_ROOT}/tools/build_zserio_core_jar.sh")"
GENERATOR_JAR="$("${REPO_ROOT}/tools/build_generator_jar.sh")"

TP="${REPO_ROOT}/third_party/zserio/3rdparty/java"
CP="${CORE_JAR}:${GENERATOR_JAR}"
CP+=":${TP}/commons-cli-1.4.jar"
CP+=":${TP}/antlr-4.7.2-complete.jar"
CP+=":${TP}/freemarker-2.3.28.jar"
CP+=":${TP}/commonmark-0.15.2.jar"
CP+=":${TP}/autolink-0.10.0.jar"
CP+=":${TP}/commonmark-ext-autolink-0.15.2.jar"
CP+=":${TP}/commonmark-ext-gfm-tables-0.15.2.jar"
CP+=":${TP}/commonmark-ext-heading-anchor-0.15.2.jar"

BUILD="${REPO_ROOT}/build-discover"
RESULTS="${BUILD}/results.csv"
ERRORS="${BUILD}/errors.log"
SUMMARY="${BUILD}/summary.md"

rm -rf "${BUILD}"
mkdir -p "${BUILD}"
: > "${RESULTS}"
: > "${ERRORS}"

PASS=0
FAIL=0

# We iterate the per-test schema files (depth 4 under <area>/<feature>/zs/...)
# rather than the umbrella .zs at depth 3, because failure attribution per
# individual feature test is far more useful than "the whole umbrella
# rejected".
SCHEMAS=()
while IFS= read -r f; do
    SCHEMAS+=("$f")
done < <(find "${DATA_DIR}/language" "${DATA_DIR}/others" \
        -mindepth 4 -name '*.zs' -path '*/zs/*' | sort)

TOTAL=${#SCHEMAS[@]}
echo "discover: walking ${TOTAL} schemas across language/ + others/"

i=0
for ZS in "${SCHEMAS[@]}"; do
    i=$((i+1))
    REL="${ZS#${DATA_DIR}/}"

    # ZserioTool resolves imports relative to -src. The convention in
    # this corpus is that every schema lives under <feature>/zs/, so we
    # split the path at the last "/zs/" segment.
    ABOVE_ZS="${ZS%%/zs/*}"
    SRC="${ABOVE_ZS}/zs"
    SCHEMA_REL="${ZS#${SRC}/}"

    OUT_DIR=$(mktemp -d)
    LOG="${OUT_DIR}/log.txt"
    if java -cp "${CP}" zserio.tools.ZserioTool \
            -src "${SRC}" "${SCHEMA_REL}" \
            -c "${OUT_DIR}/c" \
            > "${LOG}" 2>&1; then
        PASS=$((PASS+1))
        printf 'PASS\t%s\n' "${REL}" >> "${RESULTS}"
    else
        FAIL=$((FAIL+1))
        # Capture the first [ERROR] line (zserio core convention) or the
        # last few lines if no [ERROR] tag is present (Java exception).
        REASON=$(grep -m1 '^\[ERROR\]' "${LOG}" 2>/dev/null \
                | sed -E 's/^\[ERROR\][^:]*: //' \
                | tr -d '\t' \
                | head -c 200)
        if [[ -z "${REASON}" ]]; then
            REASON=$(tail -3 "${LOG}" | tr '\n' ' ' | tr -d '\t' | head -c 200)
        fi
        printf 'FAIL\t%s\t%s\n' "${REL}" "${REASON}" >> "${RESULTS}"
        {
            printf '=== %s ===\n' "${REL}"
            cat "${LOG}"
            printf '\n'
        } >> "${ERRORS}"
    fi
    rm -rf "${OUT_DIR}"
done

# ----------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------

PCT=0
if [[ ${TOTAL} -gt 0 ]]; then
    PCT=$(( (PASS * 100) / TOTAL ))
fi

{
    printf '## zserio-test-data discovery\n\n'
    printf '%d / %d schemas accepted (%d%%).\n\n' "${PASS}" "${TOTAL}" "${PCT}"
    printf '### Top rejection reasons\n\n'
    printf '| count | reason |\n'
    printf '|------:|--------|\n'
    awk -F'\t' '$1 == "FAIL" { print $3 }' "${RESULTS}" \
        | sort | uniq -c | sort -rn | head -15 \
        | awk '{ count=$1; $1=""; sub(/^ /, ""); printf "| %d | %s |\n", count, $0 }'
    printf '\n### Failing schemas (first 30)\n\n'
    awk -F'\t' '$1 == "FAIL" { print "- `" $2 "` — " $3 }' "${RESULTS}" \
        | head -30
} > "${SUMMARY}"

cat "${SUMMARY}"

if [[ ${STRICT} -eq 1 && ${FAIL} -gt 0 ]]; then
    exit 1
fi
exit 0
