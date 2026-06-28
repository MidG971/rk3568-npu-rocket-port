#!/bin/bash
# rock3b-ssh.sh - SSH to ROCK 3B with auto-discovery
# Usage: ./rock3b-ssh.sh [command]
# If no command given, opens interactive shell

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_FILE="$HOME/.rock3b_ip"
USER="radxa"

# Get IP from cache or discover
get_ip() {
    # Check if cached IP is still valid (less than 1 hour old and responsive)
    if [ -f "$CACHE_FILE" ]; then
        local cached_ip=$(cat "$CACHE_FILE")
        local cache_age=$(($(date +%s) - $(stat -c %Y "$CACHE_FILE" 2>/dev/null || echo 0)))
        
        # If cache is less than 1 hour old, test connectivity
        if [ "$cache_age" -lt 3600 ]; then
            if ping -c 1 -W 1 "$cached_ip" &>/dev/null; then
                echo "$cached_ip"
                return 0
            fi
        fi
    fi
    
    # Cache invalid or expired, discover
    echo "Discovering ROCK 3B..." >&2
    "$SCRIPT_DIR/find-rock3b.sh" >/dev/null 2>&1
    
    if [ -f "$CACHE_FILE" ]; then
        cat "$CACHE_FILE"
        return 0
    fi
    
    return 1
}

# Main
IP=$(get_ip)
if [ -z "$IP" ]; then
    echo "Error: Could not find ROCK 3B" >&2
    echo "Run: $SCRIPT_DIR/find-rock3b.sh" >&2
    exit 1
fi

if [ $# -eq 0 ]; then
    # Interactive shell
    exec ssh "$USER@$IP"
else
    # Execute command
    exec ssh "$USER@$IP" "$@"
fi
