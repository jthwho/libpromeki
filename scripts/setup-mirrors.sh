#!/bin/bash
# Configures git to use internal mirrors for third-party dependencies.
# Run once per clone: ./scripts/setup-mirrors.sh
# Use --global to apply to all repos on this machine.

SCOPE="${1:---local}"
MIRROR="https://git.howardlogic.com/thirdparty"

declare -A MIRRORS=(
    ["https://github.com/libjpeg-turbo/libjpeg-turbo.git"]="$MIRROR/libjpeg-turbo.git"
    ["https://github.com/nlohmann/json.git"]="$MIRROR/nlohmann-json.git"
    ["https://github.com/libsndfile/libsndfile.git"]="$MIRROR/libsndfile.git"
)

for upstream in "${!MIRRORS[@]}"; do
    mirror="${MIRRORS[$upstream]}"
    git config $SCOPE url."$mirror".insteadOf "$upstream"
    echo "  $upstream -> $mirror"
done

echo "Done. Submodules will now fetch from internal mirrors."
