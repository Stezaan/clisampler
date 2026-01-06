#!/bin/bash



set -e  

PROJECT_NAME="clisampler"
INSTALL_DIR="/usr/local/bin"
VERSION="1.0.0"


RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' 


print_usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -h, --help      Show this help message"
    echo "  -v, --version   Show version"
    echo "  --prefix DIR    Install to custom directory (default: /usr/local/bin)"
    echo ""
    echo "Examples:"
    echo "  $0                     # Install to /usr/local/bin"
    echo "  $0 --prefix ~/.local/bin  # Install to user directory"
}


print_version() {
    echo "$PROJECT_NAME installer v$VERSION"
}


command_exists() {
    command -v "$1" >/dev/null 2>&1
}


check_dependencies() {
    echo "Checking dependencies..."
    
    local missing_deps=()
    

    if ! command_exists g++; then
        missing_deps+=("g++")
    fi
   
    if ! pkg-config --exists libavcodec libavformat libavutil libswresample 2>/dev/null; then
        missing_deps+=("FFmpeg development libraries")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo "Missing dependencies:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        echo ""
        echo "Please install dependencies before continuing:"
        echo ""

        if [ -f /etc/os-release ]; then
            . /etc/os-release
            case $ID in
                debian|ubuntu)
                    echo "  sudo apt-get update"
                    echo "  sudo apt-get install g++ libavcodec-dev libavformat-dev libavutil-dev libswresample-dev"
                    ;;
                fedora)
                    echo "  sudo dnf install gcc-c++ ffmpeg-devel"
                    ;;
                arch|manjaro)
                    echo "  sudo pacman -S gcc ffmpeg"
                    ;;
                *)
                    echo "  Please install: g++ and FFmpeg development libraries"
                    ;;
            esac
        elif command_exists brew; then
            echo "  brew install gcc ffmpeg"
        else
            echo "  Please install: g++ and FFmpeg development libraries"
        fi
        
        echo ""
        read -p "Do you want to continue anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        echo "All dependencies found."
    fi
}


build_project() {
    echo "Building $PROJECT_NAME..."
    
    # Clean previous build
    if [ -f "$PROJECT_NAME" ]; then
        rm -f "$PROJECT_NAME"
    fi
    
   
    g++ -std=c++11 -Wall -Wextra clisampler.cpp -o "$PROJECT_NAME" \
        -lavcodec -lavformat -lavutil -lswresample
    
    if [ $? -ne 0 ]; then
        echo "Build failed."
        exit 1
    fi
    
    echo "Build successful."
}


install_binary() {
    local target_dir="$1"
    
    echo "Installing to $target_dir..."
    
    # Create directory if it doesn't exist
    mkdir -p "$target_dir"
    
    
    if cp "$PROJECT_NAME" "$target_dir/"; then
        echo "Successfully installed $PROJECT_NAME to $target_dir"
        
        
        if [[ ":$PATH:" != *":$target_dir:"* ]]; then
            echo ""
            echo "Note: $target_dir is not in your PATH."
            echo "Add this to your shell profile to use $PROJECT_NAME anywhere:"
            echo "  export PATH=\"\$PATH:$target_dir\""
        fi
    else
        echo "Installation failed. Try running with sudo:"
        echo "  sudo $0"
        exit 1
    fi
}


verify_installation() {
    local install_path="$1/$PROJECT_NAME"
    
    if [ -x "$install_path" ]; then
        echo ""
        echo "$PROJECT_NAME installation verified."
        echo "Run '$PROJECT_NAME --help' to get started."
    else
        echo "Installation verification failed."
        exit 1
    fi
}


main() {
    local install_dir="$INSTALL_DIR"
    

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                print_usage
                exit 0
                ;;
            -v|--version)
                print_version
                exit 0
                ;;
            --prefix)
                install_dir="$2"
                shift
                ;;
            *)
                echo "Unknown option: $1"
                print_usage
                exit 1
                ;;
        esac
        shift
    done
    
    echo "Installing $PROJECT_NAME v$VERSION"
    echo "========================================"
    
    # Check dependencies
    check_dependencies
    
    # Build
    build_project
    
    # Install
    install_binary "$install_dir"
    
    # Verify
    verify_installation "$install_dir"
    
    echo ""
    echo "Installation complete!"
}


main "$@"
