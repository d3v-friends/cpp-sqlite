set shell := ["bash", "-cu"]

latest:
    @echo "$(git tag --list 'v*.*.*' --sort=-v:refname | head -n1)"

@tag version:
    #!/usr/bin/env bash
    set -euo pipefail

    if ! [[ "{{ version }}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        printf "\n❌ 버전 형식이 올바르지 않습니다: {{ version }} (예: 1.2.3)\n"
        exit 1
    fi
    TAG=v{{ version }}
    ORIGIN=origin

    if git rev-parse "$TAG" >/dev/null 2>&1; then
        printf "\n❌ $TAG 는 이미 존재하는 태그입니다.\n" "$TAG"
        exit 1
    fi
    git tag "$TAG"
    git push --tags "$ORIGIN" "$TAG"
    printf "\n✅ %s 태그 작성완료!\n" "$TAG"

@vcpkg:
    just git tag $(jq -r '."version-string"' "vcpkg.json")