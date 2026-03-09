#!/bin/bash
# Configures git to use internal mirrors for third-party dependencies.
# Reads mappings from a config file (default: scripts/mirrors.conf).
#
# Usage:
#   ./scripts/setup-mirrors.sh [options] [section ...]
#
# Options:
#   --global        Apply to global git config (default: --local)
#   --clear         Remove existing mirror entries and exit
#   --config FILE   Use a custom config file
#   --dry-run       Show what would be done without making changes
#   --list          List available sections and exit
#
# If no sections are given, all sections are applied.
# Git uses longest-match for insteadOf, so full-URL entries (e.g. gitea)
# take priority over prefix rewrites (e.g. github-ssh).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCOPE="--local"
CONFIG="$SCRIPT_DIR/mirrors.conf"
CLEAR=false
DRY_RUN=false
LIST=false
declare -a SECTIONS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --global)  SCOPE="--global"; shift ;;
        --clear)   CLEAR=true; shift ;;
        --config)  CONFIG="$2"; shift 2 ;;
        --dry-run) DRY_RUN=true; shift ;;
        --list)    LIST=true; shift ;;
        --*)       echo "Unknown option: $1" >&2; exit 1 ;;
        *)         SECTIONS+=("$1"); shift ;;
    esac
done

# Clear any existing insteadOf entries.
clear_mirrors() {
    local keys
    keys=$(git config $SCOPE --get-regexp 'url\..*\.insteadOf' 2>/dev/null | awk '{print $1}') || true
    if [[ -z "$keys" ]]; then
        echo "No existing mirror entries to clear."
        return
    fi
    while IFS= read -r key; do
        if $DRY_RUN; then
            echo "  [dry-run] would unset $key"
        else
            git config $SCOPE --unset "$key"
            echo "  removed $key"
        fi
    done <<< "$keys"
}

echo "Clearing existing mirror entries..."
clear_mirrors

if $CLEAR; then
    echo "Done. All mirror entries removed."
    exit 0
fi

if [[ ! -f "$CONFIG" ]]; then
    echo "Config file not found: $CONFIG" >&2
    exit 1
fi

# Parse the config file.
declare -A VARS
declare -a ALL_SECTIONS=()
current_section=""
count=0

# Returns true if the current section is selected.
section_enabled() {
    # If no sections were requested, everything is enabled.
    if [[ ${#SECTIONS[@]} -eq 0 ]]; then
        return 0
    fi
    for s in "${SECTIONS[@]}"; do
        if [[ "$s" == "$current_section" ]]; then
            return 0
        fi
    done
    return 1
}

expand_vars() {
    local val="$1"
    for var in "${!VARS[@]}"; do
        val="${val//\$\{$var\}/${VARS[$var]}}"
    done
    echo "$val"
}

while IFS= read -r line; do
    # Strip comments and trim whitespace.
    line="${line%%#*}"
    [[ -z "${line// /}" ]] && continue

    # Section header: [name]
    if [[ "$line" =~ ^\[([A-Za-z0-9_-]+)\]$ ]]; then
        current_section="${BASH_REMATCH[1]}"
        ALL_SECTIONS+=("$current_section")
        continue
    fi

    # Variable assignment (only outside sections or in enabled sections).
    if [[ "$line" =~ ^([A-Za-z_][A-Za-z0-9_]*)=(.+)$ ]]; then
        VARS["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]// /}"
        continue
    fi

    # URL mapping: upstream = mirror
    if [[ "$line" =~ ^(.+)=(.+)$ ]]; then
        # Skip if this section isn't enabled.
        if [[ -n "$current_section" ]] && ! section_enabled; then
            continue
        fi

        upstream="$(expand_vars "${BASH_REMATCH[1]// /}")"
        mirror="$(expand_vars "${BASH_REMATCH[2]// /}")"

        if $DRY_RUN; then
            echo "  [dry-run] [$current_section] $upstream -> $mirror"
        else
            git config $SCOPE url."$mirror".insteadOf "$upstream"
            echo "  [$current_section] $upstream -> $mirror"
        fi
        ((count++)) || true
    fi
done < "$CONFIG"

if $LIST; then
    echo ""
    echo "Available sections:"
    for s in "${ALL_SECTIONS[@]}"; do
        echo "  $s"
    done
    exit 0
fi

echo "Applied $count mirror rule(s)."
