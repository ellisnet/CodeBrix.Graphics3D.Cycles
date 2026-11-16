#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Survey phase 1 render chain (scratch). Run from the repository root. Each run writes a NEW directory under
# native/work/oracles/macos-arm64-survey-20261113-<name>/; logs go to native/work/macos-arm64-survey-20261113/logs/.
# (no set -u: macOS bash 3.2 treats an empty array as unbound)
cd "$(dirname "$0")/../../.."
SW=native/work/macos-arm64-survey-20261113
O=native/work/oracles/macos-arm64-survey-20261113
S=$SW/scenes
BASE_RUNNER=native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle
BASE_HOST=native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib
SURV_RUNNER=$SW/tree/native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle
SURV_HOST=$SW/tree/native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib
BACKENDS=(--backend cpu-arm64-macos --backend metal-m2pro --backend metal-m2pro-metalrt)
WARM=(--scene native/oracle/scenes/starter/diffuse-triangle.json)
ROW6=(--scene $S/curves-thick-diffuse-both.json --scene $S/points-diffuse-both.json)
ROW9=(); for f in 1-gpu 2-gpu 4-gpu 1.5-gpu 2-cpu-oidn 1-gpu-catcher 2-gpu-catcher; do ROW9+=(--scene $S/denoise-upscale-$f.json); done
ROW11=(); for f in curves-b/motion-deform-thick curves-b/motion-deform-ribbon curves-b/motion-deform-linear curves-b/motion-radius-thick \
  curves-b/shadow-motion points/deform-3 points/deform-5 points/deform-transparent linux-gates/deforming-motion-0 \
  light-motion/camera-deforming-motion points/object-motion; do ROW11+=(--scene native/oracle/scenes/$f.json); done
DIAG_RUNNER=$SW/tree-diag/native/work/macos/osx-arm64/oracle-build/codebrix_cycles_oracle
DIAG_HOST=$SW/tree-diag/native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib
DIAG11=(); for f in curves-b/motion-deform-thick points/deform-3 linux-gates/deforming-motion-0 points/object-motion; do DIAG11+=(--scene native/oracle/scenes/$f.json); done
run() { # name runner host env... -- scenes...
  local name=$1 runner=$2 host=$3; shift 3; local envs=(); while [ "$1" != "--" ]; do envs+=("$1"); shift; done; shift
  echo "== $name start $(date)" | tee -a $SW/logs/chain.txt
  ( /usr/bin/time -p env -u CODEBRIX_SURVEY_BVH_TIME_STEPS "${envs[@]}" python3 native/oracle/render_oracles.py --output $O-$name \
      --runner $runner --host-library $host "${BACKENDS[@]}" "$@" ) > $SW/logs/run-$name.output.txt 2>&1
  echo "== $name exit $? end $(date)" | tee -a $SW/logs/chain.txt
}
for step in "$@"; do case $step in
 R1) run R1-base $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${ROW6[@]}" --scene $S/bake-subdivision.json --scene $S/bake-zero-area-flat-normal.json \
      "${ROW9[@]}" --scene $S/points-gaussian-splats.json --scene $S/denoise-dlss.json --scene $S/denoise-passes-128.json \
      --scene $S/denoise-passes-albedo-normal-256.json ;;
 R1b) run R1b-base-row6 $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${ROW6[@]}" ;;
 R2) run R2-survey $SURV_RUNNER $SURV_HOST -- "${WARM[@]}" --scene $S/bake-subdivision.json "${ROW9[@]}" "${ROW11[@]}" \
      --scene native/oracle/scenes/denoise/beckmann-on.json --scene native/oracle/scenes/bake/combined.json ;;
 R3) run R3-steps1 $SURV_RUNNER $SURV_HOST CODEBRIX_SURVEY_BVH_TIME_STEPS=1 -- "${WARM[@]}" "${ROW11[@]}" ;;
 R4) run R4-steps3 $SURV_RUNNER $SURV_HOST CODEBRIX_SURVEY_BVH_TIME_STEPS=3 -- "${WARM[@]}" "${ROW11[@]}" ;;
 R10) E=native/evidence/metal-comparison-linux-x64-20261031; SC=(); for f in $E/scenes/*.json native/oracle/scenes/bsdf/velvet.json; do SC+=(--scene "$f"); done
      run R10-metal-comparison $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${SC[@]}" ;;
 D11-0) run D11-diag-steps0 $DIAG_RUNNER $DIAG_HOST CODEBRIX_SURVEY_CYCLES_LOG=debug -- "${WARM[@]}" "${DIAG11[@]}" ;;
 D11-1) run D11-diag-steps1 $DIAG_RUNNER $DIAG_HOST CODEBRIX_SURVEY_CYCLES_LOG=debug CODEBRIX_SURVEY_BVH_TIME_STEPS=1 -- "${WARM[@]}" "${DIAG11[@]}" ;;
 D11-3) run D11-diag-steps3 $DIAG_RUNNER $DIAG_HOST CODEBRIX_SURVEY_CYCLES_LOG=debug CODEBRIX_SURVEY_BVH_TIME_STEPS=3 -- "${WARM[@]}" "${DIAG11[@]}" ;;
 P2) DS=$SW/tree/native/oracle/scenes; P2S=(--scene $S/integer-math-wraps.json --scene $S/integer-math-traps.json --scene $S/denoise-upscale-1-gpu-noisy.json --scene $S/denoise-upscale-2-gpu-noisy.json)
     for f in surface-nodes/integer-math surface-nodes/boolean-math surface-nodes/get-vector-component surface-nodes/get-vector-component-bump surface-nodes/image-missing \
       bsdf/glass-multiscatter points/geometry-tangent points/geometry-tangent-bump lamps/area-degenerate-spread-zero lamps/area-degenerate-ellipse-spread-zero \
       lamps/area-degenerate-spread-tiny lamps/area-degenerate-spread-tiny-volume lamps/area-degenerate-infinite-axis lamps/area-degenerate-tree-transmissive \
       world/portal-zero-u world/portal-zero world/portal-underflow; do P2S+=(--scene $DS/$f.json); done
     for f in $DS/bake/zero-area-*.json; do [ "$(basename $f)" = zero-area-smooth-normal.json ] || P2S+=(--scene $f); done
     run P2-base-draft $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${P2S[@]}" ;;
 P2b) P2B=(--scene $SW/tree/native/oracle/scenes/surface-nodes/get-vector-component.json); for k in diffuse diffuse-direct glossy glossy-direct transmission glossy-indirect transmission-direct transmission-indirect transmission-color; do P2B+=(--scene $S/bake-zero-area-$k.json); done
     run P2b-base-draft $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${P2B[@]}" ;;
 P2c) run P2c-base-draft $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" --scene $SW/tree/native/oracle/scenes/surface-nodes/get-vector-component.json ;;
 phase2) P=(); while read -r f; do case "$f" in \#*) continue;; esac; P+=(--scene native/oracle/scenes/$f); done < $SW/phase2-draft/new-scenes.txt
     run phase2 $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${P[@]}" ;;
 phase2-scratch) P=(--scene $S/integer-math-wraps.json --scene $S/integer-math-traps.json --scene $S/denoise-upscale-1-gpu-noisy.json --scene $S/denoise-upscale-2-gpu-noisy.json)
     for k in diffuse diffuse-direct glossy glossy-direct transmission transmission-direct; do P+=(--scene $S/bake-zero-area-$k.json); done
     run phase2-scratch $BASE_RUNNER $BASE_HOST -- "${WARM[@]}" "${P[@]}" ;;
 *) echo "unknown step $step" ;;
esac; done
echo "== chain done $(date)" | tee -a $SW/logs/chain.txt
