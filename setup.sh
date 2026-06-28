#!/bin/bash
# setup.sh - Setup script for ROCK 3B NPU project on Debian 12
# Run this once on your PC to configure everything

set -e

echo "=== ROCK 3B NPU Project Setup ==="
echo ""

# Check for required tools
echo "Checking dependencies..."

if ! command -v nmap &> /dev/null; then
    echo "Installing nmap..."
    sudo apt update
    sudo apt install -y nmap
fi

if ! command -v ssh &> /dev/null; then
    echo "Installing openssh-client..."
    sudo apt install -y openssh-client
fi

echo "✓ Dependencies OK"
echo ""

# Make scripts executable
chmod +x find-rock3b.sh
chmod +x rock3b-ssh.sh
echo "✓ Scripts made executable"
echo ""

# Try to find ROCK 3B
echo "Searching for ROCK 3B on network..."
./find-rock3b.sh || true
echo ""

# Setup SSH key if needed
if [ -f ~/.rock3b_ip ]; then
    ROCK3B_IP=$(cat ~/.rock3b_ip)
    echo "ROCK 3B found at: $ROCK3B_IP"
    echo ""
    
    read -p "Setup SSH key for passwordless login? [Y/n] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
        # Generate SSH key if doesn't exist
        if [ ! -f ~/.ssh/id_rsa ]; then
            echo "Generating SSH key..."
            ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ""
        fi
        
        echo "Copying SSH key to ROCK 3B..."
        echo "(You may be prompted for password: radxa)"
        ssh-copy-id -o StrictHostKeyChecking=no radxa@$ROCK3B_IP || true
    fi
fi

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Usage:"
echo "  ./find-rock3b.sh     # Discover ROCK 3B IP"
echo "  ./rock3b-ssh.sh      # SSH to ROCK 3B"
echo "  ./rock3b-ssh.sh 'cmd' # Run command on ROCK 3B"
echo ""
echo "For Claude Code:"
echo "  cd $(pwd)"
echo "  claude"
echo ""
