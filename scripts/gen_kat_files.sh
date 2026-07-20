#!/usr/bin/env bash

set -euo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"${repo_root}/build"}
output_dir=${2:-"${repo_root}/KAT/compact-v1"}
vectors=${KAT_VECTORS:-1}

case ${build_dir} in
    /*) ;;
    *) build_dir="$(pwd)/${build_dir}" ;;
esac
case ${output_dir} in
    /*) ;;
    *) output_dir="$(pwd)/${output_dir}" ;;
esac

kat_tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/compact-sqisign-kat.XXXXXX")
publish_tmp_dir=
cleanup() {
    rm -rf -- "${kat_tmp_dir}"
    if [[ -n ${publish_tmp_dir} ]]; then
        rm -rf -- "${publish_tmp_dir}"
    fi
}
trap cleanup EXIT

for level in lvl1 lvl3 lvl5; do
    echo "Generating ${vectors} Compact-SQIsign KAT vector(s) for ${level}..."
    "${build_dir}/apps/PQCgenKAT_sign_${level}" \
        --vectors "${vectors}" --output-dir "${kat_tmp_dir}"
done

mkdir -p "${output_dir}"
publish_tmp_dir=$(mktemp -d "${output_dir}/.publish.XXXXXX")
for generated_file in "${kat_tmp_dir}"/*.req "${kat_tmp_dir}"/*.rsp; do
    install -m 0644 "${generated_file}" \
        "${publish_tmp_dir}/$(basename -- "${generated_file}")"
done
for staged_file in "${publish_tmp_dir}"/*; do
    mv -f "${staged_file}" "${output_dir}/$(basename -- "${staged_file}")"
done
rmdir "${publish_tmp_dir}"
publish_tmp_dir=

echo "Installed Compact-SQIsign KAT files in ${output_dir}"
