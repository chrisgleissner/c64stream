#!/bin/bash

# C64 Stream - Local Multi-Platform Build Script
# This script provides local builds for all three platforms without GitHub Actions

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Default values
PLATFORM=""
BUILD_CONFIG="RelWithDebInfo"
CLEAN_BUILD=false
RUN_TESTS=false
INSTALL_DEPS=false
INSTALL_PLUGIN=false
RUN_E2E=false
VERBOSE=false

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

usage() {
    cat << EOF
C64 Stream - Local Multi-Platform Build Script

Usage: $0 <platform> [options]

PLATFORMS:
    linux       Build for Linux (Ubuntu/Debian)
    macos       Build for macOS (requires Xcode)
    windows     Build for Windows (requires MinGW or native tools)

OPTIONS:
    --config CONFIG     Build configuration: Debug, Release, RelWithDebInfo, MinSizeRel
    --clean             Clean build directory before building
    --tests             Run tests after building
    --install-deps      Install build dependencies
    --install           Install plugin to OBS after building
    --e2e               Run E2E tests after building and installing
    --verbose           Enable verbose output
    --help              Show this help message

EXAMPLES:
    $0 linux                                    # Build for Linux with RelWithDebInfo
    $0 linux --config Release --tests          # Build Release for Linux and run tests
    $0 linux --install                         # Build and install to OBS
    $0 linux --e2e --install                   # Build, install and run E2E tests
    $0 windows --clean --install-deps          # Clean build for Windows, install deps
    $0 macos --verbose                          # Build for macOS with verbose output

NOTES:
    - This script replicates CI build behavior locally
    - Dependencies are automatically downloaded where possible
    - Cross-compilation is supported for Windows on Linux (MinGW)
    - E2E tests require OBS Studio, Python3, xvfb, and additional dependencies
    - Each platform may have specific prerequisites (see README.md)
EOF
}

detect_platform() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "cygwin" || "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
        echo "windows"
    else
        log_error "Unsupported platform: $OSTYPE"
        exit 1
    fi
}

check_prerequisites() {
    local platform=$1

    log_info "Checking prerequisites for $platform..."

    # Common requirements
    if ! command -v cmake >/dev/null 2>&1; then
        log_error "CMake is required but not installed"
        exit 1
    fi

    local cmake_version
    cmake_version=$(cmake --version | head -1 | sed 's/cmake version //')
    # Use printf to compare versions properly (3.28.3 >= 3.28)
    if printf '%s\n%s\n' "3.28" "$cmake_version" | sort -V -C; then
        log_info "CMake version $cmake_version is compatible"
    else
        log_error "CMake 3.28+ is required (found $cmake_version)"
        exit 1
    fi

    case $platform in
        linux)
            if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
                log_error "GCC or Clang is required for Linux builds"
                exit 1
            fi
            ;;
        macos)
            if ! command -v xcodebuild >/dev/null 2>&1; then
                log_error "Xcode is required for macOS builds"
                exit 1
            fi
            ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
                    log_warning "MinGW cross-compiler not found. Install with: sudo apt-get install gcc-mingw-w64-x86-64"
                fi
            fi
            ;;
    esac
}

install_dependencies() {
    local platform=$1

    log_info "Installing dependencies for $platform..."

    case $platform in
        linux)
            # Install build essentials and required tools
            if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y \
                    build-essential \
                    cmake \
                    ninja-build \
                    pkg-config \
                    git \
                    clang-format \
                    python3-pip

                # Install gersemi for CMake formatting
                pip3 install --user gersemi

                # Install SIMDe if available, otherwise continue without system libobs
                if apt-cache show libsimde-dev >/dev/null 2>&1; then
                    sudo apt-get install -y libsimde-dev
                fi

                log_info "Note: OBS dependencies will be downloaded automatically by build system"
            else
                log_error "APT package manager not found. Please install dependencies manually."
                exit 1
            fi
            ;;
        macos)
            if command -v brew >/dev/null 2>&1; then
                brew install cmake ninja ccache
                log_info "macOS dependencies installed via Homebrew"
            else
                log_warning "Homebrew not found. Please install dependencies manually."
            fi
            ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                sudo apt-get update
                sudo apt-get install -y gcc-mingw-w64-x86-64 cmake ninja-build
                log_info "MinGW cross-compilation tools installed"
            else
                log_info "On Windows, please ensure you have Visual Studio 2022 or Build Tools installed"
            fi
            ;;
    esac
}

format_code() {
    log_info "Formatting source code..."

    # Check if clang-format is available
    local clang_format_cmd=""
    local clang_format_version=""

    # Try to find clang-format-21 first (preferred), then clang-format
    if command -v clang-format-21 >/dev/null 2>&1; then
        clang_format_cmd="clang-format-21"
    elif command -v clang-format >/dev/null 2>&1; then
        clang_format_cmd="clang-format"
    elif [[ -f "/usr/bin/clang-format" ]]; then
        clang_format_cmd="/usr/bin/clang-format"
    elif [[ -f "/usr/local/bin/clang-format" ]]; then
        clang_format_cmd="/usr/local/bin/clang-format"
    elif [[ -f "/c/Program Files/LLVM/bin/clang-format.exe" ]]; then
        clang_format_cmd="/c/Program Files/LLVM/bin/clang-format.exe"
    elif [[ -f "C:/Program Files/LLVM/bin/clang-format.exe" ]]; then
        clang_format_cmd="C:/Program Files/LLVM/bin/clang-format.exe"
    fi

    if [[ -z "$clang_format_cmd" ]]; then
        log_warning "clang-format not found, skipping code formatting"
        log_warning "Install clang-format 21.1.1+ to enable automatic formatting"
        return 0
    fi

    # Check version - require 21.1.1 or later (latest versions are now accepted)
    clang_format_version=$("$clang_format_cmd" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)

    if [[ -n "$clang_format_version" ]]; then
        # Parse version components
        local major=$(echo "$clang_format_version" | cut -d. -f1)
        local minor=$(echo "$clang_format_version" | cut -d. -f2)
        local patch=$(echo "$clang_format_version" | cut -d. -f3)

        # Check if version is at least 21.1.1
        if [[ "$major" -lt 21 ]] || [[ "$major" -eq 21 && "$minor" -lt 1 ]] || [[ "$major" -eq 21 && "$minor" -eq 1 && "$patch" -lt 1 ]]; then
            log_error "clang-format version $clang_format_version is too old (require 21.1.1+)"
            log_error "Install clang-format 21.1.1 or later"
            log_error "Skipping formatting - THIS WILL CAUSE CI FAILURES!"
            return 0
        fi

        log_info "Using clang-format version $clang_format_version"
    fi

    # Format all C source and header files using same flags as CI
    # CI uses: -style=file -fallback-style=none -i
    local files_formatted=0
    for file in src/*.c src/*.h src/*.cpp src/*.hpp src/*.m src/*.mm tests/*.c tests/*.cpp tests/*.h tests/*.hpp; do
        if [[ -f "$file" ]]; then
            if "$clang_format_cmd" -style=file -fallback-style=none -i "$file" 2>/dev/null; then
                files_formatted=$((files_formatted + 1))
            fi
        fi
    done

    if [[ $files_formatted -gt 0 ]]; then
        log_success "Formatted $files_formatted source files with clang-format"
    else
        log_warning "No source files found to format"
    fi
}

build_platform() {
    local platform=$1
    local config=$2

    log_info "Building C64 Stream for $platform ($config)..."

    # Determine build directory and preset
    local build_dir preset_name
    case $platform in
        linux)
            build_dir="build_x86_64"
            preset_name="ubuntu-x86_64"
            ;;
        macos)
            build_dir="build_macos"
            preset_name="macos"
            ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                build_dir="build_mingw"
                preset_name="windows-x64"
                log_warning "Cross-compiling for Windows using MinGW"
            else
                build_dir="build_x64"
                preset_name="windows-x64"
            fi
            ;;
    esac

    # Clean build if requested
    if [[ "$CLEAN_BUILD" == "true" ]]; then
        log_info "Cleaning build directory: $build_dir"
        rm -rf "$build_dir"
    fi

    # Format code before building (ensures consistency across platforms)
    format_code

    # Configure
    log_info "Configuring build..."
    if [[ "$VERBOSE" == "true" ]]; then
        cmake --preset "$preset_name" -DCMAKE_BUILD_TYPE="$config" --log-level=VERBOSE
    else
        cmake --preset "$preset_name" -DCMAKE_BUILD_TYPE="$config"
    fi

    # Build
    log_info "Building..."
    local build_args=("--build" "$build_dir" "--config" "$config")
    if [[ "$VERBOSE" == "true" ]]; then
        build_args+=("--verbose")
    fi
    build_args+=("--parallel")

    cmake "${build_args[@]}"

    log_success "Build completed successfully!"

    # List output files
    log_info "Build artifacts:"
    if [[ -d "$build_dir" ]]; then
        find "$build_dir" -name "*.so" -o -name "*.dll" -o -name "*.dylib" 2>/dev/null | head -10
    fi
}

run_tests() {
    local platform=$1
    local build_dir

    case $platform in
        linux) build_dir="build_x86_64" ;;
        macos) build_dir="build_macos" ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                build_dir="build_mingw"
            else
                build_dir="build_x64"
            fi
            ;;
    esac

    log_info "Running tests..."

    if [[ -f "$build_dir/CTestTestfile.cmake" ]]; then
        cd "$build_dir"
        ctest --output-on-failure --parallel 2
        cd "$PROJECT_ROOT"
    else
        log_warning "No tests found in build directory"
    fi
}

install_plugin() {
    local platform=$1
    local build_dir

    case $platform in
        linux) build_dir="build_x86_64" ;;
        macos) build_dir="build_macos" ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                build_dir="build_mingw"
            else
                build_dir="build_x64"
            fi
            ;;
    esac

    log_info "Installing plugin to OBS..."

    # Define installation directory based on platform
    local install_dir
    case $platform in
        linux)
            install_dir="$HOME/.config/obs-studio/plugins/c64stream"
            ;;
        macos)
            install_dir="$HOME/Library/Application Support/obs-studio/plugins/c64stream"
            ;;
        windows)
            # Use ProgramData for system-wide installation on Windows
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                # Cross-compiling on Linux - cannot install to Windows paths
                install_dir="./dist/windows/c64stream"
                log_warning "Cross-compiling: Installing to local dist directory instead of Windows system path"
            else
                # Running on Windows - install to ProgramData
                install_dir="/c/ProgramData/obs-studio/plugins/c64stream"
                log_info "Installing to Windows system-wide location: C:\\ProgramData\\obs-studio\\plugins\\c64stream"
            fi
            ;;
    esac

    # Create directory structure
    mkdir -p "$install_dir/bin/64bit"
    mkdir -p "$install_dir/data"

    # Copy binary based on platform
    case $platform in
        linux)
            if [[ -f "$build_dir/c64stream.so" ]]; then
                cp "$build_dir/c64stream.so" "$install_dir/bin/64bit/"
                log_success "Copied c64stream.so to $install_dir/bin/64bit/"
            else
                log_error "Plugin binary not found: $build_dir/c64stream.so"
                return 1
            fi
            ;;
        macos)
            if [[ -f "$build_dir/c64stream.so" ]]; then
                cp "$build_dir/c64stream.so" "$install_dir/bin/64bit/"
                log_success "Copied c64stream.so to $install_dir/bin/64bit/"
            else
                log_error "Plugin binary not found: $build_dir/c64stream.so"
                return 1
            fi
            ;;
        windows)
            # Try different possible locations for the DLL
            local dll_found=false
            local dll_locations=(
                "$build_dir/c64stream.dll"
                "$build_dir/$BUILD_CONFIG/c64stream.dll"
                "$build_dir/Debug/c64stream.dll"
                "$build_dir/RelWithDebInfo/c64stream.dll"
                "$build_dir/Release/c64stream.dll"
            )

            for dll_path in "${dll_locations[@]}"; do
                if [[ -f "$dll_path" ]]; then
                    cp "$dll_path" "$install_dir/bin/64bit/"
                    log_success "Copied c64stream.dll from $dll_path to $install_dir/bin/64bit/"
                    dll_found=true
                    break
                fi
            done

            if [[ "$dll_found" == "false" ]]; then
                log_error "Plugin DLL not found in any of the expected locations:"
                for dll_path in "${dll_locations[@]}"; do
                    log_error "  - $dll_path"
                done
                return 1
            fi
            ;;
    esac

    # Copy data files
    if [[ -d "data" ]]; then
        cp -r data/* "$install_dir/data/"
        log_success "Copied data files to $install_dir/data/"
    else
        log_warning "Data directory not found, skipping data files"
    fi

    log_success "Plugin installation completed!"

    # Platform-specific installation messages
    case $platform in
        linux)
            log_info "Plugin installed to: $install_dir"
            log_info "Start OBS Studio to test the plugin"
            ;;
        macos)
            log_info "Plugin installed to: $install_dir"
            log_info "Start OBS Studio to test the plugin"
            ;;
        windows)
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                log_info "Plugin files prepared in: $install_dir"
                log_info "Copy these files to your Windows system for installation"
            else
                log_info "Plugin installed to: C:\\ProgramData\\obs-studio\\plugins\\c64stream"
                log_info "System-wide installation completed - all users can access the plugin"
                log_info "Start OBS Studio to test the plugin"
                log_warning "Note: You may need administrator privileges to write to ProgramData"
            fi
            ;;
    esac

    # Show the installed structure
    if command -v find >/dev/null 2>&1; then
        log_info "Installed files:"
        find "$install_dir" -type f | head -20
    fi
}

run_e2e_tests() {
    local platform=$1
    
    # Only support Linux for E2E tests currently
    if [[ "$platform" != "linux" ]]; then
        log_warning "E2E tests are currently only supported on Linux"
        return 0
    fi
    
    log_info "Running E2E tests..."
    
    # Check if E2E test directory exists
    if [[ ! -d "tests/e2e" ]]; then
        log_error "E2E test directory not found: tests/e2e"
        return 1
    fi
    
    # Check if plugin is installed
    local plugin_installed=false
    local plugin_locations=(
        "$HOME/.config/obs-studio/plugins/c64stream/bin/64bit/c64stream.so"
        "/usr/lib/obs-plugins/c64stream.so"
    )
    
    for plugin_path in "${plugin_locations[@]}"; do
        if [[ -f "$plugin_path" ]]; then
            log_success "Found plugin at: $plugin_path"
            plugin_installed=true
            break
        fi
    done
    
    if [[ "$plugin_installed" == "false" ]]; then
        log_error "Plugin not found in expected locations. Run with --install first."
        log_error "Expected locations:"
        for plugin_path in "${plugin_locations[@]}"; do
            log_error "  - $plugin_path"
        done
        return 1
    fi
    
    # Check dependencies
    local missing_deps=()
    
    # Check for required system packages
    if ! command -v obs >/dev/null 2>&1; then
        missing_deps+=("obs-studio")
    fi
    
    if ! command -v python3 >/dev/null 2>&1; then
        missing_deps+=("python3")
    fi
    
    if ! command -v xvfb-run >/dev/null 2>&1; then
        missing_deps+=("xvfb")
    fi
    
    # Check for Python packages
    if ! python3 -c "import numpy" >/dev/null 2>&1; then
        missing_deps+=("python3-numpy")
    fi
    
    if ! python3 -c "import PIL" >/dev/null 2>&1; then
        missing_deps+=("python3-pil")
    fi
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_warning "Missing dependencies for E2E tests: ${missing_deps[*]}"
        log_info "Install them with: sudo apt-get install ${missing_deps[*]}"
        log_info "Continuing with E2E tests (may fail)..."
    fi
    
    # Build E2E tools if needed
    log_info "Building E2E tools..."
    if ! cmake --build build_x86_64 --target udp_replay; then
        log_error "Failed to build E2E tools"
        return 1
    fi
    
    # Change to E2E directory (with error handling)
    if ! cd tests/e2e; then
        log_error "Failed to change to E2E test directory"
        return 1
    fi
    
    # Set E2E test parameters
    local e2e_args=(
        "--format" "PAL"
        "--frames" "250"
        "--skip-build"  # We already built and installed
        "--verbose"
    )
    
    if [[ "$VERBOSE" == "true" ]]; then
        e2e_args+=("--verbose")
    fi
    
    # Check if E2E script exists and is executable
    if [[ ! -x "./e2e.sh" ]]; then
        log_error "E2E test script not found or not executable: tests/e2e/e2e.sh"
        cd "$PROJECT_ROOT"
        return 1
    fi
    
    log_info "Running E2E test with args: ${e2e_args[*]}"
    
    # Run E2E test
    if ./e2e.sh "${e2e_args[@]}"; then
        log_success "E2E tests completed successfully!"
        
        # Show test results if available
        if [[ -d "test_output" ]]; then
            log_info "Test output directory: tests/e2e/test_output"
            if [[ -f "test_output/validation_results.json" ]]; then
                log_info "Validation results:"
                cat test_output/validation_results.json | jq . 2>/dev/null || cat test_output/validation_results.json
            fi
        fi
    else
        log_error "E2E tests failed!"
        return 1
    fi
    
    # Return to project root
    cd "$PROJECT_ROOT"
}

main() {
    # Parse arguments
    if [[ $# -eq 0 ]]; then
        usage
        exit 1
    fi

    # First argument is platform, but allow auto-detection
    if [[ "$1" == "--help" || "$1" == "-h" ]]; then
        usage
        exit 0
    fi

    if [[ "$1" != --* ]]; then
        PLATFORM="$1"
        shift
    else
        PLATFORM=$(detect_platform)
        log_info "Auto-detected platform: $PLATFORM"
    fi

    # Validate platform
    case $PLATFORM in
        linux|macos|windows) ;;
        *)
            log_error "Invalid platform: $PLATFORM"
            usage
            exit 1
            ;;
    esac

    # Parse options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --config)
                BUILD_CONFIG="$2"
                shift 2
                ;;
            --clean)
                CLEAN_BUILD=true
                shift
                ;;
            --tests)
                RUN_TESTS=true
                shift
                ;;
            --install-deps)
                INSTALL_DEPS=true
                shift
                ;;
            --install)
                INSTALL_PLUGIN=true
                shift
                ;;
            --e2e)
                RUN_E2E=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done

    # Validate build config
    case "$BUILD_CONFIG" in
        Debug|RelWithDebInfo|Release|MinSizeRel) ;;
        *)
            log_error "Invalid build configuration: $BUILD_CONFIG"
            log_info "Valid options: Debug, RelWithDebInfo, Release, MinSizeRel"
            exit 1
            ;;
    esac

    log_info "C64 Stream - Local Build"
    log_info "Platform: $PLATFORM"
    log_info "Config: $BUILD_CONFIG"

    # Execute workflow
    check_prerequisites "$PLATFORM"

    if [[ "$INSTALL_DEPS" == "true" ]]; then
        install_dependencies "$PLATFORM"
    fi

    build_platform "$PLATFORM" "$BUILD_CONFIG"

    if [[ "$RUN_TESTS" == "true" ]]; then
        run_tests "$PLATFORM"
    fi

    if [[ "$INSTALL_PLUGIN" == "true" ]]; then
        install_plugin "$PLATFORM"
    fi

    if [[ "$RUN_E2E" == "true" ]]; then
        # E2E tests require the plugin to be installed
        if [[ "$INSTALL_PLUGIN" != "true" ]]; then
            log_warning "E2E tests require plugin installation. Installing plugin first..."
            install_plugin "$PLATFORM"
        fi
        run_e2e_tests "$PLATFORM"
    fi

    log_success "Local build workflow completed!"
    log_info ""
    log_info "Next steps:"
    if [[ "$INSTALL_PLUGIN" != "true" ]]; then
        log_info "  - Install plugin: $0 $PLATFORM --install"
    fi
    if [[ "$RUN_E2E" != "true" ]]; then
        log_info "  - Run E2E tests: $0 $PLATFORM --e2e --install"
    fi
    log_info "  - Test with OBS: Start OBS and add C64 Stream source"
    log_info "  - Package: cmake --build <build_dir> --target package"
}

# Ensure script is run from correct directory
cd "$PROJECT_ROOT"

main "$@"
