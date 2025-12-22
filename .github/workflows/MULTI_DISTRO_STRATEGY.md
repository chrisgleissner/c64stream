# Multi-Distribution Linux Build Matrix Strategy
#
# This file documents the strategy for adding multi-distribution support to the build workflow.
# The implementation uses a matrix build approach to test on Ubuntu, Debian, Fedora, and Arch Linux.
#
# ## Implementation Strategy
#
# 1. **Maintain Backward Compatibility**: Keep existing `ubuntu-build` job as primary
# 2. **Add Matrix Job**: Create new `linux-distro-build` job with distribution matrix
# 3. **Reuse Docker Images**: Leverage pre-built distribution-specific Docker images
# 4. **Parallel Execution**: Run all distributions in parallel for fast feedback
#
# ## Workflow Structure
#
# ```yaml
# jobs:
#   # Existing: ensure-docker-image (Ubuntu only - kept for primary build)
#   ensure-docker-image:
#     ... (unchanged)
#
#   # Existing: ubuntu-build (primary build - kept as-is)
#   ubuntu-build:
#     ... (unchanged)
#
#   # NEW: Multi-distribution Docker image management
#   ensure-docker-images-matrix:
#     name: Ensure Docker Images (Multi-Distro) 🐳
#     runs-on: ubuntu-24.04
#     needs: [check-event]
#     strategy:
#       matrix:
#         distro: [debian, fedora, arch]
#         include:
#           - distro: debian
#             dockerfile: Dockerfile.debian-build
#             image_name: debian-build
#             display_name: Debian 12
#           - distro: fedora
#             dockerfile: Dockerfile.fedora-build
#             image_name: fedora-build
#             display_name: Fedora 40
#           - distro: arch
#             dockerfile: Dockerfile.arch-build
#             image_name: arch-build
#             display_name: Arch Linux
#     outputs:
#       debian-tag: ${{ steps.image.outputs.tag }}
#       fedora-tag: ${{ steps.image.outputs.tag }}
#       arch-tag: ${{ steps.image.outputs.tag }}
#     steps:
#       - uses: actions/checkout@v4
#       - name: Check Image Age for ${{ matrix.display_name }} 🔍
#         # ... (similar to ensure-docker-image but parameterized)
#       - name: Build and Push ${{ matrix.display_name }} Image 🚀
#         # ... (parameterized Docker build)
#
#   # NEW: Multi-distribution build job
#   linux-distro-build:
#     name: Build on ${{ matrix.display_name }} 🐧
#     runs-on: ubuntu-24.04
#     needs: [check-event, ensure-docker-images-matrix]
#     strategy:
#       fail-fast: false  # Continue testing other distros even if one fails
#       matrix:
#         distro: [debian, fedora, arch]
#         include:
#           - distro: debian
#             image_name: debian-build
#             display_name: Debian 12
#             os_label: debian-12
#           - distro: fedora
#             image_name: fedora-build
#             display_name: Fedora 40
#             os_label: fedora-40
#           - distro: arch
#             image_name: arch-build
#             display_name: Arch Linux
#             os_label: arch-latest
#     container:
#       image: ghcr.io/${{ github.repository }}/${{ matrix.image_name }}:latest
#     steps:
#       - uses: actions/checkout@v4
#       - name: Build Plugin on ${{ matrix.display_name }} 🧱
#         # ... (standard CMake build)
#       - name: Upload ${{ matrix.display_name }} Artifact 📦
#         # ... (upload with distro-specific name)
#
#   # NEW: Multi-distribution E2E test job
#   e2e-test-matrix:
#     name: E2E Test on ${{ matrix.display_name }} 🧪
#     runs-on: ubuntu-24.04
#     needs: [check-event, linux-distro-build]
#     if: ${{ inputs.run_e2e }}
#     strategy:
#       fail-fast: false
#       matrix:
#         distro: [ubuntu, debian, fedora, arch]
#         include:
#           - distro: ubuntu
#             image_name: ubuntu-build
#             display_name: Ubuntu 24.04
#           - distro: debian
#             image_name: debian-build
#             display_name: Debian 12
#           - distro: fedora
#             image_name: fedora-build
#             display_name: Fedora 40
#           - distro: arch
#             image_name: arch-build
#             display_name: Arch Linux
#     container:
#       image: ghcr.io/${{ github.repository }}/${{ matrix.image_name }}:latest
#     steps:
#       - uses: actions/checkout@v4
#       - name: Run E2E Tests on ${{ matrix.display_name }} 🧪
#         # ... (run E2E test suite)
# ```
#
# ## Benefits
#
# - **Broad Coverage**: Tests on all major Linux distributions
# - **Early Detection**: Finds distribution-specific issues immediately
# - **Parallel Execution**: Matrix runs all distributions simultaneously
# - **Fail-Safe**: One distro failure doesn't block others (fail-fast: false)
# - **Backward Compatible**: Existing ubuntu-build unchanged, remains primary
#
# ## Migration Path
#
# 1. Phase 1: Add matrix jobs alongside existing ubuntu-build (current)
# 2. Phase 2: Monitor matrix job stability over several releases
# 3. Phase 3: Gradually increase reliance on matrix results
# 4. Phase 4: Consider making ubuntu-build use matrix pattern too
#
# ## Maintenance
#
# - Docker images rebuilt weekly or on Dockerfile changes
# - Distribution versions updated quarterly or when OBS updates
# - E2E test suite shared across all distributions
# - Distribution-specific configs in tests/e2e/config/<distro>/
