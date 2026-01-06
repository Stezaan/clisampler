#!/bin/bash

# Installation script for clisampler

echo "=== Installing clisampler ==="

# Check for dependencies
echo "Checking dependencies..."

# Check for g++
if ! command -v g++ &> /dev/null; then
    echo "Error: g++ compiler not found. Installing..."
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        sudo apt-get update
        sudo apt-get install -y g++
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        if ! command -v brew &> /dev/null; then
            echo "Homebrew not found. Installing Homebrew..."
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        fi
        brew install gcc
    else
        echo "Please install g++ manually"
        exit 1
    fi
fi

# Check for FFmpeg libraries
echo "Checking for FFmpeg libraries..."
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Ubuntu/Debian
    if ! dpkg -l | grep -q libavcodec-dev; then
        echo "Installing FFmpeg development libraries..."
        sudo apt-get update
        sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    if ! brew list | grep -q ffmpeg; then
        echo "Installing FFmpeg via Homebrew..."
        brew install ffmpeg
    fi
else
    echo "Note: Please ensure FFmpeg development libraries are installed"
    echo "For other systems, you may need to install manually"
fi

# Compile the program
echo "Compiling clisampler..."
g++ -std=c++11 -Wall -Wextra clisampler.cpp -o clisampler \
    -lavcodec -lavformat -lavutil -lswresample

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
else
    echo "Compilation failed. Please check errors above."
    exit 1
fi

# Test the executable
echo "Testing the executable..."
./clisampler --help 2>/dev/null || echo "Test run completed"

# Install to system
echo "Installing to /usr/local/bin..."
if [ -w /usr/local/bin ]; then
    cp clisampler /usr/local/bin/
else
    sudo cp clisampler /usr/local/bin/
fi

if [ $? -eq 0 ]; then
    echo "Installation complete!"
    echo "You can now run 'clisampler' from anywhere in the terminal."
    echo ""
    echo "Usage examples:"
    echo "  clisampler input.mp3 22050"
    echo "  clisampler input.wav 16000 output.wav"
else
    echo "System installation failed. Trying user installation..."
    mkdir -p ~/.local/bin
    cp clisampler ~/.local/bin/
    
    # Add to PATH if not already there
    if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc 2>/dev/null || true
        echo "Added ~/.local/bin to PATH"
    fi
    
    echo "Installed to ~/.local/bin"
    echo "Please restart your terminal or run:"
    echo "  source ~/.bashrc"
fi

echo ""
echo "To test, run: clisampler --help"
