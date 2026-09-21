#!/usr/bin/env bash

set -u

# Configuration
SERVER_HOST="sakuya.weeb"
SERVER_IP="192.168.100.11"
MOUNT_ROOT="/home/seth/Mount"
# Synology NFS exports are normally rooted at the storage volume.
# Change this to the volume containing the shared folders if needed.
NFS_EXPORT_ROOT="/volume1"
NFS_OPTIONS="nfsvers=4"

# Terminal colors (automatically disabled when output is redirected).
if [[ -t 1 && -t 2 ]]; then
    RESET=$'\033[0m'
    BOLD=$'\033[1m'
    DIM=$'\033[2m'
    BLUE=$'\033[34m'
    CYAN=$'\033[36m'
    GREEN=$'\033[32m'
    YELLOW=$'\033[33m'
    RED=$'\033[31m'
else
    RESET="" BOLD="" DIM="" BLUE="" CYAN="" GREEN="" YELLOW="" RED=""
fi

# List of available shares
SHARES=("Usenet" "Anime" "Storage" "Unfinished" "Torrents" "TV_Shows" "Music" "Movies" "Games")

die() {
    printf '%s✖ Error:%s %s\n' "$RED" "$RESET" "$*" >&2
    exit 1
}

section() { printf '\n%s%s%s\n' "$BOLD$BLUE" "$1" "$RESET"; }
status_ok() { printf '%s✔%s %s\n' "$GREEN" "$RESET" "$*"; }
status_warn() { printf '%s⚠%s %s\n' "$YELLOW" "$RESET" "$*" >&2; }
status_fail() { printf '%s✖%s %s\n' "$RED" "$RESET" "$*" >&2; }

is_mounted() {
    mountpoint -q -- "$1"
}

mount_share() {
    local share="$1"
    local target_path="$MOUNT_ROOT/$share"
    local export_path="${NFS_EXPORT_ROOT%/}/$share"

    mkdir -p -- "$target_path" || return 1

    if is_mounted "$target_path"; then
        status_ok "$share is already mounted"
        return 0
    fi

    printf '%s→%s Mounting %s%s%s %s(via NFS)%s\n' "$CYAN" "$RESET" "$BOLD" "$share" "$RESET" "$DIM" "$RESET"
    if sudo mount -t nfs "$SERVER_ADDRESS:$export_path" "$target_path" \
        -o "$NFS_OPTIONS"; then
        status_ok "$share mounted"
        return 0
    fi

    status_fail "Failed to mount $share"
    return 1
}

unmount_all() {
    section "Unmounting shares"
    printf '%s%s%s\n' "$DIM" "$MOUNT_ROOT" "$RESET"
    for share in "${SHARES[@]}"; do
        local target_path="$MOUNT_ROOT/$share"
        if is_mounted "$target_path"; then
            if sudo umount -- "$target_path"; then
                status_ok "Disconnected $share"
            else
                status_fail "Failed to unmount $share"
            fi
        fi
    done
    status_ok "Unmount complete"
}

printf '\n%s%s Sakuya Server Manager %s\n' "$BOLD$BLUE" "━━" "$RESET"
active_mounts=0
for share in "${SHARES[@]}"; do
    if is_mounted "$MOUNT_ROOT/$share"; then
        ((active_mounts++))
    fi
done

printf '%s●%s %s%d%s shares active\n' "$GREEN" "$RESET" "$BOLD" "$active_mounts" "$RESET"
printf '%s%s%s\n' "$DIM" "$MOUNT_ROOT" "$RESET"
printf '\n%s1)%s Mount a specific share\n' "$CYAN" "$RESET"
printf '%s2)%s Mount all shares\n' "$CYAN" "$RESET"
printf '%su)%s Unmount all shares\n' "$CYAN" "$RESET"
printf '%sq)%s Quit\n\n' "$CYAN" "$RESET"
read -r -p "Selection: " main_choice

case "$main_choice" in
    1)
        section "Select a share"
        for i in "${!SHARES[@]}"; do
            printf '%d) %s\n' "$((i + 1))" "${SHARES[$i]}"
        done
        read -r -p "Selection: " sub_choice
        if [[ "$sub_choice" =~ ^[0-9]+$ ]] && ((sub_choice >= 1 && sub_choice <= ${#SHARES[@]})); then
            TARGETS=("${SHARES[$((sub_choice - 1))]}")
        else
            die "Invalid selection."
        fi
        ;;
    2) TARGETS=("${SHARES[@]}") ;;
    u) unmount_all; exit 0 ;;
    q) exit 0 ;;
    *) die "Invalid option." ;;
esac

# Prefer DNS, but use the inventory IP when the local DNS server is unavailable.
if getent ahostsv4 "$SERVER_HOST" >/dev/null 2>&1; then
    SERVER_ADDRESS="$SERVER_HOST"
else
    SERVER_ADDRESS="$SERVER_IP"
    status_warn "'$SERVER_HOST' did not resolve; using $SERVER_IP"
fi

ping -c 1 -W 2 "$SERVER_ADDRESS" >/dev/null 2>&1 || \
    die "Server '$SERVER_HOST' ($SERVER_ADDRESS) is unreachable."

failed_shares=()
for share in "${TARGETS[@]}"; do
    if ! mount_share "$share"; then
        failed_shares+=("$share")
        # Continue when mounting all shares so one missing export does not
        # prevent the remaining valid exports from being mounted.
        if [[ "$main_choice" != "2" ]]; then
            echo "Check the NFS export path, server status, and client permissions." >&2
            exit 1
        fi
    fi
done

if ((${#failed_shares[@]} > 0)); then
    section "Mount summary"
    status_fail "Could not mount: ${failed_shares[*]}"
    printf '%sVerify these shared folders exist and have NFS enabled on Synology.%s\n' "$DIM" "$RESET" >&2
    exit 1
fi

section "Mount summary"
status_ok "All requested shares are mounted"
