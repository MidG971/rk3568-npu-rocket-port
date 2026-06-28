#!/bin/bash
# find-rock3b.sh - Discover ROCK 3B IP address on local network
# Uses nmap to find device by MAC address

# ROCK 3B MAC addresses
MAC_ETH0="<BOARD-MAC>"   # end0
MAC_ETH1="<BOARD-MAC>"   # end1
MAC_WIFI="<BOARD-MAC>"   # wlp1s0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get local network range
get_network_range() {
    # Get the default interface IP and calculate network range
    local ip=$(ip route get 1.1.1.1 2>/dev/null | grep -oP 'src \K[\d.]+')
    if [ -z "$ip" ]; then
        echo "192.168.1.0/24"  # fallback
        return
    fi
    # Convert to /24 network
    echo "${ip%.*}.0/24"
}

# Search for ROCK 3B
find_rock3b() {
    local network=$(get_network_range)
    echo -e "${YELLOW}Scanning network: $network${NC}"
    echo "Looking for ROCK 3B (this may take 10-30 seconds)..."
    
    # Run nmap scan (requires sudo for MAC detection)
    local nmap_output
    if command -v nmap &> /dev/null; then
        nmap_output=$(sudo nmap -sn "$network" 2>/dev/null)
    else
        echo -e "${RED}Error: nmap not installed. Install with: sudo apt install nmap${NC}"
        exit 1
    fi
    
    # Search for each MAC address
    local found_ip=""
    local found_interface=""
    
    for mac in "$MAC_ETH0" "$MAC_ETH1" "$MAC_WIFI"; do
        # nmap outputs MAC in uppercase
        local mac_upper=$(echo "$mac" | tr '[:lower:]' '[:upper:]')
        local ip=$(echo "$nmap_output" | grep -B2 "$mac_upper" | grep -oP 'Nmap scan report for \K[\d.]+' | head -1)
        
        if [ -n "$ip" ]; then
            found_ip="$ip"
            case "$mac" in
                "$MAC_ETH0") found_interface="eth0 (end0)" ;;
                "$MAC_ETH1") found_interface="eth1 (end1)" ;;
                "$MAC_WIFI") found_interface="WiFi (wlp1s0)" ;;
            esac
            break
        fi
    done
    
    if [ -n "$found_ip" ]; then
        echo -e "${GREEN}✓ Found ROCK 3B!${NC}"
        echo -e "  IP: ${GREEN}$found_ip${NC}"
        echo -e "  Interface: $found_interface"
        echo ""
        echo "$found_ip"
        
        # Save to cache file
        echo "$found_ip" > ~/.rock3b_ip
        echo "IP cached in ~/.rock3b_ip"
        
        # Test SSH connectivity
        echo ""
        echo "Testing SSH connection..."
        if ssh -o ConnectTimeout=5 -o BatchMode=yes radxa@"$found_ip" "echo 'SSH OK'" 2>/dev/null; then
            echo -e "${GREEN}✓ SSH connection successful${NC}"
        else
            echo -e "${YELLOW}⚠ SSH may require password or key setup${NC}"
            echo "  Run: ssh-copy-id radxa@$found_ip"
        fi
    else
        echo -e "${RED}✗ ROCK 3B not found on network${NC}"
        echo ""
        echo "Troubleshooting:"
        echo "  1. Is the ROCK 3B powered on?"
        echo "  2. Is it connected to the same network?"
        echo "  3. Try specifying a different network range:"
        echo "     $0 192.168.0.0/24"
        exit 1
    fi
}

# Alternative: scan specific network range
if [ -n "$1" ]; then
    network="$1"
    echo -e "${YELLOW}Scanning specified network: $network${NC}"
    nmap_output=$(sudo nmap -sn "$network" 2>/dev/null)
    
    for mac in "$MAC_ETH0" "$MAC_ETH1" "$MAC_WIFI"; do
        local mac_upper=$(echo "$mac" | tr '[:lower:]' '[:upper:]')
        ip=$(echo "$nmap_output" | grep -B2 "$mac_upper" | grep -oP 'Nmap scan report for \K[\d.]+' | head -1)
        if [ -n "$ip" ]; then
            echo -e "${GREEN}Found: $ip${NC}"
            echo "$ip" > ~/.rock3b_ip
            exit 0
        fi
    done
    echo -e "${RED}Not found in $network${NC}"
    exit 1
fi

find_rock3b
