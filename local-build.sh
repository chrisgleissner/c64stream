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
E2E_SCENARIO=""
GENERATE_E2E_SCENARIOS=false
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
    --install-e2e-deps  Also install E2E testing dependencies (OBS, xvfb, etc.)
    --install           Install plugin to OBS after building
    --e2e[=SCENARIO]    Run E2E tests after building and installing (default scenario: ntsc)
    --e2e-scenarios     Run all scenarios in tests/e2e/scenarios/* and write results to tests/e2e/results/<scenario>
    --verbose           Enable verbose output
    --help              Show this help message

EXAMPLES:
    $0 linux                                    # Build for Linux with RelWithDebInfo
    $0 linux --config Release --tests          # Build Release for Linux and run tests
    $0 linux --install                         # Build and install to OBS
    $0 linux --e2e --install                   # Build, install and run E2E tests
    $0 windows --clean --install-deps          # Clean build for Windows, install deps
    $0 linux --install-e2e-deps --e2e          # Install all deps including E2E and run E2E tests
    $0 macos --verbose                          # Build for macOS with verbose output

NOTES:
    - This script replicates CI build behavior locally
    - Dependencies are automatically downloaded where possible
    - Cross-compilation is supported for Windows on Linux (MinGW)
    - E2E tests require OBS Studio, Python3, xvfb, and additional dependencies (auto-installed)
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
                log_info "Updating package lists..."
                sudo apt-get update -qq

                log_info "Installing core build dependencies..."
                sudo apt-get install -y \
                    build-essential \
                    cmake \
                    ninja-build \
                    pkg-config \
                    git \
                    clang-format \
                    ccache \
                    python3 \
                    python3-pip
                log_info "✅ Core build dependencies installed"

                # Install gersemi for CMake formatting
                if ! command -v gersemi >/dev/null 2>&1; then
                    log_info "Installing gersemi for CMake formatting..."
                    pip3 install --user gersemi
                fi

                # Install SIMDe if available, otherwise continue without system libobs
                if apt-cache show libsimde-dev >/dev/null 2>&1; then
                    sudo apt-get install -y libsimde-dev
                    log_info "Installed libsimde-dev for SIMD optimizations"
                fi

                # Install E2E testing dependencies if requested
                if [[ "${INSTALL_E2E_DEPS:-false}" == "true" ]]; then
                    log_info "Installing E2E testing dependencies..."
                    sudo apt-get install -y \
                        obs-studio \
                        python3-numpy \
                        python3-requests \
                        python3-websocket \
                        xvfb \
                        x11-utils
                    log_info "✅ E2E testing dependencies installed successfully"
                    log_info "   - OBS Studio (video streaming software)"
                    log_info "   - Python libraries (numpy; requests/websocket optional)"
                    log_info "   - Xvfb (virtual display for headless testing)"
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

reset_obs_configuration() {
    local platform=$1
    local skip_e2e_props=${2:-false}

    if [[ "$platform" != "linux" ]]; then
        log_info "OBS configuration reset only supported on Linux"
        return 0
    fi

    log_info "Resetting OBS configuration to clean state..."

    local obs_config_dir="$HOME/.config/obs-studio"

    # Backup existing configuration
    if [[ -d "$obs_config_dir/basic" ]]; then
        local backup_dir="$obs_config_dir/backup_$(date +%Y%m%d_%H%M%S)"
        log_info "Creating backup: $backup_dir"
        cp -r "$obs_config_dir/basic" "$backup_dir"
    fi

    # Remove E2E test profile and scene collection
    if [[ -d "$obs_config_dir/basic/profiles/C64StreamTest" ]]; then
        log_info "Removing E2E test profile: C64StreamTest"
        rm -rf "$obs_config_dir/basic/profiles/C64StreamTest"
    fi

    if [[ -f "$obs_config_dir/basic/scenes/C64StreamTest.json" ]]; then
        log_info "Removing E2E test scene collection: C64StreamTest.json"
        rm -f "$obs_config_dir/basic/scenes/C64StreamTest.json"
        rm -f "$obs_config_dir/basic/scenes/C64StreamTest.json.bak"
    fi

    # Reset scenes.json to use default collection
    local scenes_file="$obs_config_dir/basic/scenes/scenes.json"
    if [[ -f "$scenes_file" ]]; then
        log_info "Resetting default scene collection to Untitled"
    fi

    local untitled_scene="$obs_config_dir/basic/scenes/Untitled.json"
    if [[ -f "$untitled_scene" ]]; then
        log_info "OBS scene configuration ready at $untitled_scene"
    fi

    # Handle E2E properties file restoration/removal
    if [[ "$skip_e2e_props" != "true" ]]; then
        local props_file="$obs_config_dir/plugins/c64stream/data/properties.ini"
        if [[ -f "$props_file" ]]; then
            # Check if it's an E2E properties file (contains localhost)
            if grep -q "localhost" "$props_file"; then
                log_info "Found E2E properties file with localhost settings"
                mv "$props_file" "${props_file}.e2e_backup_$(date +%Y%m%d_%H%M%S)"
                log_info "Backed up E2E properties file"

                # Restore correct properties.ini with real C64 Ultimate settings
                if [[ -f "$PROJECT_ROOT/data/properties.ini" ]]; then
                    cp "$PROJECT_ROOT/data/properties.ini" "$props_file"
                    log_success "Restored default properties.ini (real C64 Ultimate settings: port 64, host c64u)"
                else
                    log_warning "Default properties.ini not found at $PROJECT_ROOT/data/properties.ini"
                fi
            fi
        elif [[ ! -f "$props_file" && -f "$PROJECT_ROOT/data/properties.ini" ]]; then
            # Properties file missing entirely - restore default
            log_info "Properties file missing, restoring default"
            mkdir -p "$(dirname "$props_file")"
            cp "$PROJECT_ROOT/data/properties.ini" "$props_file"
            log_success "Restored default properties.ini"
        fi
    else
        log_info "Skipping E2E properties removal (E2E tests will be run)"
    fi

    log_success "OBS configuration reset completed"
}

kill_obs_processes() {
    local skip_udp_cleanup=${1:-false}

    log_info "Checking for running OBS processes..."

    # Find all OBS processes
    local obs_pids=$(pgrep -x "obs" 2>/dev/null || true)

    if [[ -n "$obs_pids" ]]; then
        log_info "Found running OBS processes: $obs_pids"

        # Try graceful shutdown first
        log_info "Attempting graceful shutdown..."
        pkill -TERM -x "obs" 2>/dev/null || true

        # Wait up to 5 seconds for graceful shutdown
        for i in {1..5}; do
            sleep 1
            if ! pgrep -x "obs" >/dev/null 2>&1; then
                log_success "OBS processes shut down gracefully"
                return 0
            fi
        done

        # Force kill if still running
        log_warning "Graceful shutdown failed, force killing OBS processes..."
        pkill -KILL -x "obs" 2>/dev/null || true

        # Wait a moment for cleanup
        sleep 1

        # Verify all processes are gone
        local remaining_pids=$(pgrep -x "obs" 2>/dev/null || true)
        if [[ -n "$remaining_pids" ]]; then
            log_error "Failed to kill OBS processes: $remaining_pids"
            return 1
        else
            log_success "All OBS processes terminated"
        fi
    else
        log_info "No running OBS processes found"
    fi

    # Also check for any lingering UDP sockets on our ports (skip during E2E tests)
    if [[ "$skip_udp_cleanup" != "true" ]]; then
        local busy_ports=$(ss -ulnp | grep -E ":(21000|21001) " || true)
        if [[ -n "$busy_ports" ]]; then
            log_warning "Found processes still using UDP ports 21000/21001:"
            echo "$busy_ports"
            log_warning "Attempting to kill processes using these ports..."

            # Extract PIDs from ss output and kill them
            local pids_to_kill=$(echo "$busy_ports" | grep -oE 'pid=[0-9]+' | cut -d= -f2 | sort -u || true)
            if [[ -n "$pids_to_kill" ]]; then
                for pid in $pids_to_kill; do
                    if kill -TERM "$pid" 2>/dev/null; then
                        log_info "Killed process $pid using UDP ports"
                        sleep 1
                        # Force kill if still alive
                        if kill -0 "$pid" 2>/dev/null; then
                            kill -KILL "$pid" 2>/dev/null || true
                            log_info "Force killed stubborn process $pid"
                        fi
                    fi
                done
            fi

            # Recheck after cleanup
            busy_ports=$(ss -ulnp | grep -E ":(21000|21001) " || true)
            if [[ -n "$busy_ports" ]]; then
                log_error "Still have processes using UDP ports after cleanup:"
                echo "$busy_ports"
                log_error "E2E tests will likely fail due to port conflicts"
            else
                log_success "Successfully cleared UDP port conflicts"
            fi
        fi
    else
        log_info "Skipping UDP port cleanup (E2E tests will use these ports)"
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

    # Reset OBS configuration to clean state before installing
    reset_obs_configuration "$platform" "$RUN_E2E"

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

        # Ensure we always have the correct properties.ini with real C64 Ultimate settings
        # This overwrites any E2E properties that might be lingering
        local properties_file="$install_dir/data/properties.ini"
        if [[ -f "data/properties.ini" ]]; then
            # Force copy the default properties.ini (real C64 Ultimate settings)
            cp "data/properties.ini" "$properties_file"
            log_success "Installed default properties.ini (real C64 Ultimate settings)"

            # Backup and remove any E2E properties files that might override
            for e2e_props in "$install_dir/data/properties_e2e"*.ini; do
                if [[ -f "$e2e_props" ]]; then
                    local backup_name="${e2e_props}.backup_$(date +%Y%m%d_%H%M%S)"
                    mv "$e2e_props" "$backup_name"
                    log_info "Backed up E2E properties: $(basename "$e2e_props") -> $(basename "$backup_name")"
                fi
            done

            # Verify the installed properties have correct settings
            if grep -q "control_port.*64" "$properties_file" && grep -q "c64u" "$properties_file"; then
                log_success "✅ Verified properties.ini has real C64 Ultimate settings (port 64, host c64u)"
            else
                log_warning "⚠️ Properties.ini may not have correct C64 Ultimate settings - check manually"
            fi
        else
            log_warning "Default properties.ini not found in data/ directory"
        fi
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
    # Limit output but avoid SIGPIPE causing failure under pipefail
    find "$install_dir" -type f | head -20 || true
    fi
}

install_plugin_for_e2e() {
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

    log_info "Installing plugin to OBS for E2E testing..."

    # Reset OBS configuration to clean state with E2E settings
    reset_obs_configuration "$platform" "true"

    # Define installation directory based on platform
    local install_dir
    case $platform in
        linux)
            install_dir="$HOME/.config/obs-studio/plugins/c64stream"
            ;;
        macos)
            # Check both user and system library paths for plugins
            if [[ -d "$HOME/Library/Application Support/obs-studio/plugins" ]]; then
                install_dir="$HOME/Library/Application Support/obs-studio/plugins/c64stream"
            else
                install_dir="/Library/Application Support/obs-studio/plugins/c64stream"
            fi
            ;;
        windows)
            # Use OBS's default plugin directory
            install_dir="$APPDATA/obs-studio/plugins/c64stream"
            ;;
    esac

    # Create installation directories
    mkdir -p "$install_dir/bin/64bit"
    mkdir -p "$install_dir/data"

    # Copy plugin files
    case $platform in
        linux)
            if [[ ! -f "$build_dir/c64stream.so" ]]; then
                log_error "Plugin file not found: $build_dir/c64stream.so"
                log_error "Please build the plugin first with: $0 $platform"
                return 1
            fi
            log_success "Copied c64stream.so to $install_dir/bin/64bit"
            cp "$build_dir/c64stream.so" "$install_dir/bin/64bit/"
            ;;
        macos)
            if [[ ! -f "$build_dir/c64stream.so" ]]; then
                log_error "Plugin file not found: $build_dir/c64stream.so"
                log_error "Please build the plugin first with: $0 $platform"
                return 1
            fi
            cp "$build_dir/c64stream.so" "$install_dir/bin/64bit/"
            log_success "Copied c64stream.so to $install_dir/bin/64bit"
            ;;
        windows)
            if [[ ! -f "$build_dir/c64stream.dll" ]]; then
                log_error "Plugin file not found: $build_dir/c64stream.dll"
                log_error "Please build the plugin first with: $0 $platform"
                return 1
            fi
            cp "$build_dir/c64stream.dll" "$install_dir/bin/64bit/"
            log_success "Copied c64stream.dll to $install_dir/bin/64bit"
            ;;
    esac

    # Copy data files (effects, images, locales, etc.)
    if [[ -d "data" ]]; then
        cp -r data/* "$install_dir/data/"
        log_success "Copied data files to $install_dir/data/"
    fi

    # Install E2E properties file
    local e2e_props=""
    if [[ -f "tests/e2e/properties_e2e_local.ini" ]]; then
        e2e_props="tests/e2e/properties_e2e_local.ini"
    elif [[ -f "tests/e2e/properties_e2e_ci.ini" ]]; then
        e2e_props="tests/e2e/properties_e2e_ci.ini"
    fi

    if [[ -n "$e2e_props" ]]; then
        # Create backup of existing properties.ini if it exists
        if [[ -f "$install_dir/data/properties.ini" ]]; then
            cp "$install_dir/data/properties.ini" "$install_dir/data/properties.ini.e2e_backup_$(date +%Y%m%d_%H%M%S)"
        fi

        cp "$e2e_props" "$install_dir/data/properties.ini"
        log_success "Installed E2E properties.ini from $e2e_props"

        # Verify E2E settings
        if grep -q "control_port=6400" "$install_dir/data/properties.ini"; then
            log_success "✅ Verified properties.ini has E2E settings (port 6400, localhost)"
        else
            log_error "❌ Properties.ini verification failed - E2E settings not found"
            return 1
        fi
    else
        log_warning "No E2E properties file found - using default settings"
    fi

    log_success "Plugin installation for E2E testing completed!"
    log_info "Plugin installed to: $install_dir"

    # Show the installed structure
    if command -v find >/dev/null 2>&1; then
        log_info "Installed files:"
    # Limit output but avoid SIGPIPE causing failure under pipefail
    find "$install_dir" -type f | head -20 || true
    fi
}

run_e2e_tests() {
    local platform=$1
    local scenario_name=${2:-ntsc}
    local scenario_key
    scenario_key="$(echo "$scenario_name" | tr '[:upper:]' '[:lower:]')"
    local scenario_dir="$PROJECT_ROOT/tests/e2e/scenarios/$scenario_key"

    # Only support Linux for E2E tests currently
    if [[ "$platform" != "linux" ]]; then
        log_warning "E2E tests are currently only supported on Linux"
        return 0
    fi

    if [[ ! -d "$scenario_dir" ]]; then
        log_error "E2E scenario not found: $scenario_name (expected directory $scenario_dir)"
        return 1
    fi

    local scenario_yaml="$scenario_dir/scenario.yaml"
    local scenario_label=""
    local scenario_format=""
    local overrides_dir="overrides"

    if [[ -f "$scenario_yaml" ]]; then
        scenario_label="$(parse_scenario_yaml "$scenario_yaml" "name")"
        scenario_format="$(parse_scenario_yaml "$scenario_yaml" "format")"
        local overrides_from_yaml
        overrides_from_yaml="$(parse_scenario_yaml "$scenario_yaml" "overrides_dir")"
        if [[ -n "$overrides_from_yaml" ]]; then
            overrides_dir="$overrides_from_yaml"
        fi
    fi

    [[ -z "$scenario_label" ]] && scenario_label="$scenario_key"
    [[ -z "$scenario_format" ]] && scenario_format="NTSC"
    scenario_format="$(echo "$scenario_format" | tr '[:lower:]' '[:upper:]')"

    local scenario_overrides_path="$scenario_dir/$overrides_dir"
    local overrides_realpath=""
    local -a scenario_override_args=()
    if [[ -d "$scenario_overrides_path" ]]; then
        overrides_realpath=$(realpath "$scenario_overrides_path" 2>/dev/null || echo "$scenario_overrides_path")
        scenario_override_args=("--scenario-overrides" "$overrides_realpath")
    else
        log_warning "Scenario overrides directory not found for $scenario_label: $scenario_overrides_path"
    fi

    log_info "Running E2E tests for scenario '$scenario_label' (format=${scenario_format})"

    # Kill any existing OBS processes to avoid port conflicts
    # Skip UDP port cleanup during E2E tests since the plugin needs those ports
    kill_obs_processes "true"

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

    # Check for Python packages (keep minimal)
    if ! python3 -c "import numpy" >/dev/null 2>&1; then
        missing_deps+=("python3-numpy")
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
    if ! pushd tests/e2e >/dev/null; then
        log_error "Failed to change to E2E test directory"
        return 1
    fi

    # Set E2E test parameters (default to NTSC 60Hz for consistent 1-frame pop visibility)
    local e2e_args=(
        "--format" "$scenario_format"
        "--duration" "5"   # ~5 seconds at 60 FPS => ~300 frames
        "--skip-build"      # We already built and installed
        "--verbose"
    )

    if [[ -n "$scenario_label" ]]; then
        e2e_args+=("--scenario-name" "$scenario_label")
    fi

    if [[ ${#scenario_override_args[@]} -gt 0 ]]; then
        e2e_args+=("${scenario_override_args[@]}")
    fi

    if [[ "$VERBOSE" == "true" ]]; then
        e2e_args+=("--verbose")
    fi

    # Check if E2E script exists and is executable
    if [[ ! -f "./e2e.sh" ]]; then
        log_error "E2E test script not found: tests/e2e/e2e.sh"
        popd >/dev/null
        return 1
    fi

    log_info "Running E2E test with args: ${e2e_args[*]}"

    # Run E2E test
    # Run via bash to avoid executable-bit issues
    if bash ./e2e.sh "${e2e_args[@]}"; then
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
        popd >/dev/null
        return 1
    fi

    # Return to project root
    popd >/dev/null

    # Archive results into scenario-specific directory
    local test_output_dir="$PROJECT_ROOT/tests/e2e/test_output"
    local results_root_dir="$PROJECT_ROOT/tests/e2e/results/$scenario_key"
    rm -rf "$results_root_dir"
    mkdir -p "$results_root_dir"

    # Remove stop recording marker before copying
    local marker_file="$test_output_dir/stop_recording.marker"
    if [[ -f "$marker_file" ]]; then
        rm -f "$marker_file" || true
    fi

    if [[ -d "$test_output_dir" ]]; then
        cp -a "$test_output_dir/." "$results_root_dir/"
    fi

    # Compress MP4 into destination to match scenario suite behavior
    local src_mp4="$test_output_dir/c64_recording.mp4"
    local out_mp4="$results_root_dir/c64_recording.mp4"
    if [[ -f "$src_mp4" ]]; then
        bash "$PROJECT_ROOT/tests/e2e/compress_e2e_mp4.sh" "$src_mp4" "$out_mp4" || true
    else
        log_warning "No source MP4 found at $src_mp4 to compress for scenario $scenario_label"
    fi

    # Copy OBS config used for this run
    local config_used_dir="$results_root_dir/config_used"
    mkdir -p "$config_used_dir"
    local obs_cfg_root="$HOME/.config/obs-studio"
    if [[ -d "$obs_cfg_root/basic/profiles/C64StreamTest" ]]; then
        mkdir -p "$config_used_dir/basic/profiles"
        cp -a "$obs_cfg_root/basic/profiles/C64StreamTest" "$config_used_dir/basic/profiles/"
    fi
    if [[ -f "$obs_cfg_root/basic/scenes/C64StreamTest.json" ]]; then
        mkdir -p "$config_used_dir/basic/scenes"
        cp -a "$obs_cfg_root/basic/scenes/C64StreamTest.json" "$config_used_dir/basic/scenes/"
    fi
}

# Parse minimal scenario.yaml (key: value per line); supports keys: name, format, overrides_dir
parse_scenario_yaml() {
    local yaml_file=$1
    local key=$2
    local val=""
    if [[ -f "$yaml_file" ]]; then
        val=$(grep -E "^${key}:[[:space:]]*" "$yaml_file" | sed -E "s/^${key}:[[:space:]]*//" | tr -d '\r')
    fi
    echo "$val"
}

run_e2e_scenarios() {
    local platform=$1

    if [[ "$platform" != "linux" ]]; then
        log_warning "E2E scenarios are currently only supported on Linux"
        return 0
    fi

    local scenarios_root="tests/e2e/scenarios"
    local results_root="tests/e2e/results"
    local suite_start_ts=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
    local scenario_list=()

    if [[ ! -d "$scenarios_root" ]]; then
        log_warning "No scenarios directory found at $scenarios_root; creating starters..."
        mkdir -p "$scenarios_root/pal/overrides" "$scenarios_root/ntsc/overrides"
        cat > "$scenarios_root/pal/scenario.yaml" <<EOS
name: PAL Baseline
format: PAL
overrides_dir: overrides
EOS
        cat > "$scenarios_root/ntsc/scenario.yaml" <<EOS
name: NTSC Baseline
format: NTSC
overrides_dir: overrides
EOS
        # Provide example override of properties (optional)
        mkdir -p "$scenarios_root/pal/overrides/plugins/c64stream/data" "$scenarios_root/ntsc/overrides/plugins/c64stream/data"
        # Leave overrides empty by default; users can add files mirroring ~/.config/obs-studio
        log_info "Created starter PAL/NTSC scenarios"
    fi

    mkdir -p "$results_root"

    # Discover scenarios: direct subdirectories with scenario.yaml
    while IFS= read -r -d '' scen; do
        scenario_list+=("$scen")
    done < <(find "$scenarios_root" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)

    if [[ ${#scenario_list[@]} -eq 0 ]]; then
        log_warning "No scenarios found under $scenarios_root"
        return 0
    fi

    log_info "Running ${#scenario_list[@]} scenario(s) from $scenarios_root"

    # Top-level README builder
    local suite_readme="$results_root/README.md"
    echo "# C64 Stream E2E Scenarios" > "$suite_readme"
    echo >> "$suite_readme"
    echo "Generated: $suite_start_ts" >> "$suite_readme"
    echo >> "$suite_readme"
    echo "## Results" >> "$suite_readme"
    echo >> "$suite_readme"

    for scen_dir in "${scenario_list[@]}"; do
        local scen_name
        scen_name=$(basename "$scen_dir")
        local yaml="$scen_dir/scenario.yaml"
        if [[ ! -f "$yaml" ]]; then
            log_warning "Skipping $scen_name - no scenario.yaml"
            continue
        fi

        local name format overrides_dir
        name=$(parse_scenario_yaml "$yaml" "name")
        format=$(parse_scenario_yaml "$yaml" "format")
        overrides_dir=$(parse_scenario_yaml "$yaml" "overrides_dir")
        [[ -z "$format" ]] && format="NTSC"
        [[ -z "$overrides_dir" ]] && overrides_dir="overrides"
    local overrides_path="$scen_dir/$overrides_dir"
    overrides_path=$(realpath "$overrides_path" 2>/dev/null || echo "$overrides_path")

        log_info "=== Scenario: ${name:-$scen_name} (format=$format) ==="

        # Ensure plugin is installed for E2E each time (safe no-op if already)
        install_plugin_for_e2e "$platform"

        # Run E2E for this scenario
    pushd tests/e2e >/dev/null
        local e2e_args=(
            "--format" "$format"
            "--duration" "5"
            "--skip-build"
            "--verbose"
            "--scenario-overrides" "$overrides_path"
        )
        if bash ./e2e.sh "${e2e_args[@]}"; then
            log_success "Scenario $scen_name completed"
        else
            log_warning "Scenario $scen_name had issues"
        fi

        # Remove stop recording marker before archiving
        local marker_file="$PROJECT_ROOT/tests/e2e/test_output/stop_recording.marker"
        if [[ -f "$marker_file" ]]; then
            rm -f "$marker_file" || true
        fi

        # Move outputs to results/<scenario> (absolute path to avoid cwd issues)
        local dest_dir_abs="$PROJECT_ROOT/$results_root/$scen_name"
        mkdir -p "$dest_dir_abs"
        if [[ -d "test_output" ]]; then
            cp -a test_output/. "$dest_dir_abs/"
        fi

        # Copy the OBS config actually used for this run
        local config_used_dir="$dest_dir_abs/config_used"
        mkdir -p "$config_used_dir"
        local obs_cfg_root="$HOME/.config/obs-studio"
        # Profile config
        if [[ -d "$obs_cfg_root/basic/profiles/C64StreamTest" ]]; then
            mkdir -p "$config_used_dir/basic/profiles"
            cp -a "$obs_cfg_root/basic/profiles/C64StreamTest" "$config_used_dir/basic/profiles/"
        fi
        # Scene collection JSON (deterministic).
        #
        # We intentionally do NOT fall back to copying "latest" because OBS may create additional
        # collections (e.g. Untitled.json) that are not the test collection and cause noisy diffs.
        if [[ -f "$obs_cfg_root/basic/scenes/C64StreamTest.json" ]]; then
            mkdir -p "$config_used_dir/basic/scenes"
            cp -a "$obs_cfg_root/basic/scenes/C64StreamTest.json" "$config_used_dir/basic/scenes/"
        else
            log_warning "Scene collection not found: $obs_cfg_root/basic/scenes/C64StreamTest.json (skipping config_used scene copy)"
        fi

        # Compress from standard source to scenario result target per spec
        local src_mp4="$PROJECT_ROOT/tests/e2e/test_output/c64_recording.mp4"
        local out_mp4="$dest_dir_abs/c64_recording.mp4"
        if [[ -f "$src_mp4" ]]; then
            # Overwrite the copied file with compressed file at destination
            bash "$PROJECT_ROOT/tests/e2e/compress_e2e_mp4.sh" "$src_mp4" "$out_mp4" || true
        else
            log_warning "No source MP4 found at $src_mp4 to compress for scenario $scen_name"
        fi

        popd >/dev/null

        # Link in suite README
        if [[ -f "$dest_dir_abs/README.md" ]]; then
            echo "- [$scen_name](./$scen_name/README.md)" >> "$suite_readme"
        else
            echo "- $scen_name (no README.md)" >> "$suite_readme"
        fi
    done

    # Do not add an end time per request
    log_success "Scenario suite complete. See $suite_readme"
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
            --install-e2e-deps)
                INSTALL_DEPS=true
                INSTALL_E2E_DEPS=true
                shift
                ;;
            --install)
                INSTALL_PLUGIN=true
                shift
                ;;
            --e2e=*)
                RUN_E2E=true
                E2E_SCENARIO="${1#--e2e=}"
                shift
                ;;
            --e2e)
                RUN_E2E=true
                if [[ $# -gt 1 && "$2" != --* ]]; then
                    E2E_SCENARIO="$2"
                    shift 2
                else
                    shift
                fi
                ;;
            --e2e-scenarios)
                GENERATE_E2E_SCENARIOS=true
                shift
                ;;
            --e2e-report)
                log_warning "--e2e-report has been renamed to --e2e-scenarios and the timestamped folder was removed"
                GENERATE_E2E_SCENARIOS=true
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

    # If scenarios requested, imply E2E
    if [[ "$GENERATE_E2E_SCENARIOS" == "true" ]]; then
        RUN_E2E=true
    fi

    if [[ "$RUN_E2E" == "true" ]]; then
        if [[ -z "$E2E_SCENARIO" ]]; then
            E2E_SCENARIO="ntsc"
        fi
        E2E_SCENARIO="$(echo "$E2E_SCENARIO" | tr '[:upper:]' '[:lower:]')"
    fi

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
    if [[ "$RUN_E2E" == "true" && "$GENERATE_E2E_SCENARIOS" != "true" ]]; then
        log_info "E2E scenario: $E2E_SCENARIO"
    fi

    # Execute workflow
    check_prerequisites "$PLATFORM"

    # Auto-install E2E dependencies if E2E is requested and dependencies are missing
    if [[ "$RUN_E2E" == "true" && "$INSTALL_DEPS" == "false" ]]; then
        # Check if essential E2E dependencies are missing
        local missing_e2e_deps=()

        if ! command -v obs >/dev/null 2>&1; then
            missing_e2e_deps+=("obs-studio")
        fi
        if ! command -v xvfb-run >/dev/null 2>&1; then
            missing_e2e_deps+=("xvfb")
        fi
        if ! python3 -c "import numpy" >/dev/null 2>&1; then
            missing_e2e_deps+=("python3-numpy")
        fi
        # Keep Python deps minimal; only numpy is required

        if [[ ${#missing_e2e_deps[@]} -gt 0 ]]; then
            log_info "E2E testing requested but missing dependencies: ${missing_e2e_deps[*]}"
            log_info "Auto-installing missing E2E dependencies..."
            INSTALL_DEPS=true
            INSTALL_E2E_DEPS=true
        else
            log_info "E2E testing requested - all dependencies already installed"
        fi
    fi

    if [[ "$INSTALL_DEPS" == "true" ]]; then
        install_dependencies "$PLATFORM"
    fi

    build_platform "$PLATFORM" "$BUILD_CONFIG"

    if [[ "$RUN_TESTS" == "true" ]]; then
        run_tests "$PLATFORM"
    fi

    if [[ "$INSTALL_PLUGIN" == "true" ]]; then
        # If E2E tests will also be run, install with E2E settings
        if [[ "$RUN_E2E" == "true" ]]; then
            install_plugin_for_e2e "$PLATFORM"
        else
            install_plugin "$PLATFORM"
        fi
    fi

    if [[ "$RUN_E2E" == "true" ]]; then
        # E2E tests require the plugin to be installed
        if [[ "$INSTALL_PLUGIN" != "true" ]]; then
            log_warning "E2E tests require plugin installation. Installing plugin first..."
            install_plugin_for_e2e "$PLATFORM"
        fi
        if [[ "$GENERATE_E2E_SCENARIOS" == "true" ]]; then
            run_e2e_scenarios "$PLATFORM"
        else
            run_e2e_tests "$PLATFORM" "$E2E_SCENARIO"
        fi
    fi

    log_success "Local build workflow completed!"
    log_info ""
    log_info "Next steps:"
    if [[ "$INSTALL_PLUGIN" != "true" ]]; then
        log_info "  - Install plugin: $0 $PLATFORM --install"
    fi
    if [[ "$RUN_E2E" != "true" ]]; then
        log_info "  - Run E2E tests: $0 $PLATFORM --e2e --install"
    else
        log_info "  - View E2E report: tests/e2e/test_output/README.md"
        if [[ "$GENERATE_E2E_SCENARIOS" == "true" ]]; then
            log_info "  - View scenarios: tests/e2e/results/README.md"
        fi
    fi
    log_info "  - Test with OBS: Start OBS and add C64 Stream source"
    log_info "  - Package: cmake --build <build_dir> --target package"
}

# Ensure script is run from correct directory
cd "$PROJECT_ROOT"

main "$@"
