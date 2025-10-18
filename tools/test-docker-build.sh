#!/bin/bash

# Test script for Dockerized C64 Stream build system
# This script validates that the containerized build works correctly

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Test 1: Check if Docker is available
test_docker_availability() {
    log_info "Testing Docker availability..."

    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed or not in PATH"
        return 1
    fi

    if ! docker info &> /dev/null; then
        log_error "Docker daemon is not running or not accessible"
        return 1
    fi

    log_success "Docker is available and running"
    return 0
}

# Test 2: Build the Ubuntu container image locally
test_container_build() {
    log_info "Testing container build..."

    cd "$PROJECT_ROOT"

    if ! docker build -f .github/docker/Dockerfile.ubuntu-build -t c64stream-test-build .; then
        log_error "Container build failed"
        return 1
    fi

    log_success "Container built successfully"
    return 0
}

# Test 3: Test the container can run basic commands
test_container_functionality() {
    log_info "Testing container functionality..."

    # Test OBS version
    if ! docker run --rm c64stream-test-build obs --version > /dev/null 2>&1; then
        log_error "OBS is not working in container"
        return 1
    fi

    # Test CMake version
    if ! docker run --rm c64stream-test-build cmake --version > /dev/null 2>&1; then
        log_error "CMake is not working in container"
        return 1
    fi

    # Test Ninja
    if ! docker run --rm c64stream-test-build ninja --version > /dev/null 2>&1; then
        log_error "Ninja is not working in container"
        return 1
    fi

    log_success "Container functionality verified"
    return 0
}

# Test 4: Test actual plugin build in container
test_plugin_build() {
    log_info "Testing plugin build in container..."

    cd "$PROJECT_ROOT"

    # Clean any existing build directory
    rm -rf build_x86_64 || true

    # Run build in container
    if ! docker run --rm -v "$(pwd):/workspace" c64stream-test-build bash -c "
        cd /workspace
        cmake --preset ubuntu-x86_64
        cmake --build build_x86_64 --parallel
    "; then
        log_error "Plugin build failed in container"
        return 1
    fi

    # Check if plugin was built
    if [[ ! -f "build_x86_64/c64stream.so" ]]; then
        log_error "Plugin binary not found after build"
        return 1
    fi

    log_success "Plugin built successfully in container"

    # Show file info
    ls -la build_x86_64/c64stream.so
    file build_x86_64/c64stream.so

    return 0
}

# Test 5: Validate build performance
test_build_performance() {
    log_info "Testing build performance..."

    cd "$PROJECT_ROOT"

    # Clean build
    rm -rf build_x86_64 || true

    # Measure build time
    start_time=$(date +%s)

    docker run --rm -v "$(pwd):/workspace" c64stream-test-build bash -c "
        cd /workspace
        cmake --preset ubuntu-x86_64
        cmake --build build_x86_64 --parallel
    " > /dev/null 2>&1

    end_time=$(date +%s)
    build_time=$((end_time - start_time))

    log_info "Build completed in ${build_time} seconds"

    # Build should be reasonably fast (less than 5 minutes for local test)
    if [[ $build_time -gt 300 ]]; then
        log_warning "Build took longer than expected (${build_time}s > 300s)"
        return 1
    fi

    log_success "Build performance is acceptable (${build_time}s)"
    return 0
}

# Test 6: Cleanup
cleanup() {
    log_info "Cleaning up test artifacts..."

    # Remove test container image
    docker rmi c64stream-test-build > /dev/null 2>&1 || true

    # Clean build directory
    cd "$PROJECT_ROOT"
    rm -rf build_x86_64 || true

    log_success "Cleanup completed"
}

# Main test runner
main() {
    log_info "Starting C64 Stream Docker build system tests..."
    echo

    local failed_tests=0
    local total_tests=5

    # Run all tests
    test_docker_availability || ((failed_tests++))
    echo

    test_container_build || ((failed_tests++))
    echo

    test_container_functionality || ((failed_tests++))
    echo

    test_plugin_build || ((failed_tests++))
    echo

    test_build_performance || ((failed_tests++))
    echo

    # Cleanup
    cleanup
    echo

    # Summary
    local passed_tests=$((total_tests - failed_tests))

    if [[ $failed_tests -eq 0 ]]; then
        log_success "All tests passed! (${passed_tests}/${total_tests})"
        log_success "Docker build system is working correctly 🎉"
        exit 0
    else
        log_error "${failed_tests}/${total_tests} tests failed"
        log_error "Docker build system needs attention ❌"
        exit 1
    fi
}

# Handle script interruption
trap cleanup EXIT

# Run main function
main "$@"
