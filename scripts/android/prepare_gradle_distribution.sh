#!/usr/bin/env bash
# Warm the exact Qt-supplied wrapper cache before compiling APKs. Only downloads
# the distribution (--version), never retries compilation or executes app tasks.
set -euo pipefail
template="${1:?usage: prepare_gradle_distribution.sh <Qt Gradle template>}"
[[ -f "$template/gradlew" && -f "$template/gradle/wrapper/gradle-wrapper.properties" ]]
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$template/gradlew" "$work/gradlew"
cp -R "$template/gradle" "$work/gradle"
for attempt in 1 2 3; do
    if (cd "$work" && bash ./gradlew --version --no-daemon); then
        exit 0
    else
        status=$?
    fi
    if [[ "$attempt" -eq 3 ]]; then
        echo "Gradle distribution preparation failed after 3 attempts" >&2
        exit "$status"
    fi
    delay=$((5 * 2 ** (attempt - 1)))
    echo "Gradle distribution attempt $attempt failed; retrying in ${delay}s" >&2
    sleep "$delay"
done
