#!/bin/bash
set -euo pipefail

# regenerate.sh — One-shot regeneration of all Plan-V4D OBS packages
#
# After making changes to the project (this repo, branch `rollback`) and/or the
# OpenCV checkout ($OPENCV_DIR, branch `GCV`), run this script to:
#   1. regenerate the source tarballs from the local git checkouts,
#   2. update all four OBS packages (openSUSE_Tumbleweed, Fedora, Ubuntu_24.04,
#      Raspbian_12) with the new sources,
#   3. commit them (bypassing the broken _service), and
#   4. trigger rebuilds.
#
# Usage:
#   ./regenerate.sh                           # uses default OBS_USER from osc config
#   ./regenerate.sh myobsuser                 # explicit OBS username
#   OPENCV_DIR=/path/to/opencv ./regenerate.sh   # override OpenCV checkout
#   ./regenerate.sh --no-rebuild user         # regenerate+commit but skip 'osc rebuild'
#   VERSION=... REVISION=2 ./regenerate.sh    # override version/revision
#
# Prerequisites:
#   - osc installed and configured
#   - git available
#   - local Plan-V4D checkout (this repo) on branch 'rollback'
#   - OpenCV checkout at $OPENCV_DIR (branch GCV) or will be cloned

# ====================================================================
# Configuration (single source of truth)
# ====================================================================
VERSION="${VERSION:-4.13.0~beta~kallaballa}"
REVISION="${REVISION:-1}"
OPENCV_BRANCH="${OPENCV_BRANCH:-GCV}"
PLANV4D_BRANCH="${PLANV4D_BRANCH:-rollback}"
OPENCV_REPO_URL="${OPENCV_REPO_URL:-https://github.com/kallaballa/opencv.git}"
PLANV4D_REPO_URL="${PLANV4D_REPO_URL:-https://github.com/kallaballa/Plan-V4D.git}"

OBS_API="${OBS_API:-https://api.opensuse.org}"
OBS_USER=""
NO_REBUILD=false

# ---- Parse args ----
for arg in "$@"; do
    case "$arg" in
        --no-rebuild) NO_REBUILD=true ;;
        *)
            if [[ -z "$OBS_USER" ]]; then
                OBS_USER="$arg"
            else
                echo "ERROR: unexpected argument '$arg'" >&2
                exit 1
            fi
            ;;
    esac
done
OBS_USER="${OBS_USER:-${OSC_USER:-}}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PACKAGING_DIR="$SCRIPT_DIR/packaging"
OBS_DIR="$SCRIPT_DIR"

# Prevent any interactive editor from opening (osc commit)
EDITOR=true
export EDITOR

# ====================================================================
# Resolve OBS user from oscrc if not provided
# ====================================================================
if [[ -z "$OBS_USER" ]]; then
    for rc in "$HOME/.config/osc/oscrc" "$HOME/.oscrc"; do
        if [[ -f "$rc" ]]; then
            OBS_USER=$(grep -m1 '^\s*user\s*=' "$rc" | sed 's/.*=\s*//')
            [[ -n "$OBS_USER" ]] && break
        fi
    done
fi
if [[ -z "$OBS_USER" ]]; then
    echo "ERROR: Could not determine OBS username." >&2
    echo "Usage: $0 [--no-rebuild] <obs-username>" >&2
    echo "   or: OSC_USER=<username> $0" >&2
    exit 1
fi

# ====================================================================
# Locate or clone OpenCV checkout
# ====================================================================
# Default OpenCV directory: project sibling or cache
if [[ -z "${OPENCV_DIR:-}" ]]; then
    if [[ -d "$HOME/devel/opencv/.git" ]]; then
        OPENCV_DIR="$HOME/devel/opencv"
    else
        OPENCV_DIR="$SCRIPT_DIR/.cache/opencv"
    fi
fi

if [[ ! -d "$OPENCV_DIR/.git" ]]; then
    echo "--- OpenCV checkout not found at $OPENCV_DIR, cloning ---"
    mkdir -p "$(dirname "$OPENCV_DIR")"
    git clone --depth 1 --branch "$OPENCV_BRANCH" "$OPENCV_REPO_URL" "$OPENCV_DIR"
fi

# Verify branches
OPENCV_CUR_BRANCH=$(git -C "$OPENCV_DIR" branch --show-current)
PLANV4D_CUR_BRANCH=$(git -C "$PROJECT_DIR" branch --show-current)

if [[ "$OPENCV_CUR_BRANCH" != "$OPENCV_BRANCH" ]]; then
    echo "WARNING: OpenCV checkout is on branch '$OPENCV_CUR_BRANCH', expected '$OPENCV_BRANCH'"
fi
if [[ "$PLANV4D_CUR_BRANCH" != "$PLANV4D_BRANCH" ]]; then
    echo "WARNING: Plan-V4D checkout is on branch '$PLANV4D_CUR_BRANCH', expected '$PLANV4D_BRANCH'"
fi

echo "=== Plan-V4D OBS Regeneration ==="
echo "OBS API:    $OBS_API"
echo "OBS User:   $OBS_USER"
echo "VERSION:    $VERSION"
echo "REVISION:   $REVISION"
echo "OpenCV dir: $OPENCV_DIR (branch $OPENCV_CUR_BRANCH)"
echo "Plan-V4D:   $PROJECT_DIR (branch $PLANV4D_CUR_BRANCH)"
echo "Rebuild:    $([ "$NO_REBUILD" = true ] && echo skip || echo yes)"
echo ""

# Verify osc
if ! command -v osc &>/dev/null; then
    echo "ERROR: 'osc' not found. Install it:" >&2
    echo "  openSUSE: sudo zypper install obs-service-obs_scm osc" >&2
    echo "  Fedora:   sudo dnf install osc" >&2
    exit 1
fi

# ====================================================================
# Project names
# ====================================================================
TOP_PROJECT="home:${OBS_USER}"
declare -a PROJECTS=(
    "${TOP_PROJECT}:Plan-V4D:openSUSE_Tumbleweed"
    "${TOP_PROJECT}:Plan-V4D:Fedora"
    "${TOP_PROJECT}:Plan-V4D:Ubuntu_24.04"
    "${TOP_PROJECT}:Plan-V4D:Raspbian_12"
)
PACKAGE="plan-v4d"

# ====================================================================
# Build source tarballs from local checkouts
# ====================================================================
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

echo "--- Building opencv source tarball (plain, for RPM targets) ---"
(cd "$OPENCV_DIR" && tar czf "$STAGE/opencv-${VERSION}.tar.gz" \
    --transform "s,^.,opencv-${VERSION}," \
    --exclude=./.git \
    --exclude=./build \
    --exclude=./build_wasm \
    --exclude=./.cache \
    --exclude=./compile_commands.json \
    --exclude=./imgui.ini \
    --exclude='./kristen.webm*' \
    .)
echo "  -> $(du -h "$STAGE/opencv-${VERSION}.tar.gz" | cut -f1)"

echo "--- Building plan-v4d source tarball (for RPM targets) ---"
# modules/v4d/third is a symlink in git to a local bgfx/imgui/nanovg tree;
# -h dereferences it so the real sources are packaged.
(cd "$PROJECT_DIR" && tar czf "$STAGE/plan-v4d-${VERSION}.tar.gz" -h \
    --transform "s,^.,plan-v4d-${VERSION}," \
    --exclude=./.git \
    --exclude=./.git-rewrite \
    --exclude=./.github \
    --exclude=./.gitmodules \
    --exclude=./.kilo \
    --exclude=./cmake \
    --exclude=./debian.rules \
    --exclude=./debian.tar.gz \
    --exclude=./imgui.ini \
    --exclude=./obs \
    --exclude=./video \
    --exclude=./plan-v4d.dsc \
    --exclude=./build \
    --exclude=./.cache \
    --exclude=./compile_commands.json \
    .)
echo "  -> $(du -h "$STAGE/plan-v4d-${VERSION}.tar.gz" | cut -f1)"

echo "--- Building merged opencv tarball (opencv + extra_modules, for deb targets) ---"
mkdir -p "$STAGE/deb"
tar -xzf "$STAGE/opencv-${VERSION}.tar.gz" -C "$STAGE/deb"
tar -xzf "$STAGE/plan-v4d-${VERSION}.tar.gz" -C "$STAGE/deb"
mv "$STAGE/deb/plan-v4d-${VERSION}/modules" "$STAGE/deb/opencv-${VERSION}/extra_modules"
rm -rf "$STAGE/deb/plan-v4d-${VERSION}"
(cd "$STAGE/deb" && tar czf "$STAGE/opencv-deb-${VERSION}.tar.gz" "opencv-${VERSION}")
echo "  -> $(du -h "$STAGE/opencv-deb-${VERSION}.tar.gz" | cut -f1)"

# Sanity: the merged deb tarball must contain extra_modules with plan + v4d
# Use a temp file to avoid pipefail issues with grep -q exiting early (SIGPIPE)
tar -tzf "$STAGE/opencv-deb-${VERSION}.tar.gz" 2>/dev/null > "$STAGE/tar_list.txt"
if ! grep -qE "extra_modules/(plan|v4d)/" "$STAGE/tar_list.txt"; then
    echo "ERROR: merged opencv tarball is missing extra_modules/{plan,v4d}." >&2
    exit 1
fi
echo ""

# ====================================================================
# Generate deb packaging files from tracked sources
# ====================================================================
generate_deb_files() {
    local target="$1"   # ubuntu or raspbian
    local outdir="$2"
    local srcdir="$PACKAGING_DIR/$target"

    # Generate debian.changelog
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@REVISION@/$REVISION/g" \
        "$srcdir/debian.changelog.in" > "$outdir/debian.changelog"

    # Copy debian.control
    cp "$srcdir/debian.control" "$outdir/debian.control"

    # Copy debian.rules
    cp "$srcdir/debian.rules" "$outdir/debian.rules"
    chmod +x "$outdir/debian.rules"

    # Stage the debian/ overlay. debtransform picks up the top-level
    # debian.rules / debian.control / debian.changelog files from the package
    # source directory on its own; bundling them into debian.tar.gz as well
    # triggers '"rules" exists in both the debian archive as well as the
    # package source directory.' So the tarball only carries files that are
    # not surfaced as separate top-level files: .install files, compat,
    # copyright, source/format.
    local overlay="$outdir/debian-overlay"
    rm -rf "$overlay"
    mkdir -p "$overlay/debian/source"
    if [[ -f "$srcdir/debian/compat" ]]; then
        cp "$srcdir/debian/compat" "$overlay/debian/compat"
    fi
    cp "$srcdir/debian/copyright"       "$overlay/debian/copyright"
    cp "$srcdir/debian/source/format"   "$overlay/debian/source/format"
    cp "$srcdir/debian/"*.install       "$overlay/debian/"
    (cd "$overlay" && tar czf "$outdir/debian.tar.gz" debian)
    rm -rf "$overlay"

    # Generate plan-v4d.dsc
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@REVISION@/$REVISION/g" \
        "$srcdir/plan-v4d.dsc.in" > "$outdir/plan-v4d.dsc"
}

echo "--- Generating Ubuntu_24.04 deb packaging ---"
mkdir -p "$STAGE/ubuntu"
generate_deb_files "ubuntu" "$STAGE/ubuntu"

echo "--- Generating Raspbian_12 deb packaging ---"
mkdir -p "$STAGE/raspbian"
generate_deb_files "raspbian" "$STAGE/raspbian"

echo ""

# ====================================================================
# Helper: find or create osc working copy
# ====================================================================
get_working_copy() {
    local project="$1"
    local target_name="${project##*:}"

    # Known stable working copy paths (matching current layout with colons)
    local wc_path="$OBS_DIR/$project/$PACKAGE"
    if [[ -d "$wc_path/.osc" ]]; then
        echo "$wc_path"
        return 0
    fi

    # Check if .osc is at project level (some checkouts have it there)
    local wc_proj="$OBS_DIR/$project"
    if [[ -d "$wc_proj/.osc" && -d "$wc_proj/$PACKAGE" ]]; then
        echo "$wc_proj/$PACKAGE"
        return 0
    fi

    # Check nested structure (e.g., Raspbian has project/project/package)
    local wc_nested="$OBS_DIR/$project/$project/$PACKAGE"
    if [[ -d "$wc_nested/.osc" ]]; then
        echo "$wc_nested"
        return 0
    fi

    # Fallback: search for .osc dir matching THIS project specifically
    local found
    found=$(find "$OBS_DIR" -maxdepth 5 -type d -name .osc -path "*/$project/$PACKAGE/.osc" 2>/dev/null | head -1 | sed 's#/\.osc$##')
    if [[ -n "$found" && -d "$found/.osc" ]]; then
        echo "$found"
        return 0
    fi

    # Not found - create new checkout
    echo "  No working copy found for $project, checking out..."
    mkdir -p "$OBS_DIR"
    (cd "$OBS_DIR" && osc -A "$OBS_API" checkout "$project" "$PACKAGE" >/dev/null)
    local new_wc="$OBS_DIR/$project/$PACKAGE"
    if [[ -d "$new_wc/.osc" ]]; then
        echo "$new_wc"
        return 0
    fi

    # Check if .osc is at project level
    if [[ -d "$OBS_DIR/$project/.osc" && -d "$OBS_DIR/$project/$PACKAGE" ]]; then
        echo "$OBS_DIR/$project/$PACKAGE"
        return 0
    fi

    # Check nested
    if [[ -d "$OBS_DIR/$project/$project/$PACKAGE/.osc" ]]; then
        echo "$OBS_DIR/$project/$project/$PACKAGE"
        return 0
    fi

    echo "ERROR: Failed to create working copy for $project" >&2
    return 1
}

# ====================================================================
# Update each OBS package
# ====================================================================
TARGETS=("openSUSE_Tumbleweed" "Fedora" "Ubuntu_24.04" "Raspbian_12")

for target in "${TARGETS[@]}"; do
    PROJECT="${TOP_PROJECT}:Plan-V4D:${target}"
    REPO="$target"
    echo "=== ${target} ==="

    WORK_DIR=$(get_working_copy "$PROJECT")
    if [[ -z "$WORK_DIR" ]]; then
        exit 1
    fi
    echo "  working copy: $WORK_DIR"

    # Sync with OBS
    (cd "$WORK_DIR" && osc -A "$OBS_API" up || true)

    pushd "$WORK_DIR"

    # Remove stale _service (broken obs_scm) if it ever reappears
    if [[ -e _service ]]; then
        osc -A "$OBS_API" rm _service >/dev/null 2>&1 || rm -f _service
    fi

    case "$target" in
        openSUSE_Tumbleweed|Fedora)
            # RPM targets: opencv tarball, plan-v4d tarball, spec file
            cp "$STAGE/opencv-${VERSION}.tar.gz" .
            cp "$STAGE/plan-v4d-${VERSION}.tar.gz" .
            cp "$SCRIPT_DIR/plan-v4d.spec" .

            # Remove deb-only leftovers
            for f in debian.changelog debian.control debian.rules debian.tar.gz plan-v4d.dsc; do
                if [[ -e "$f" ]]; then
                    osc -A "$OBS_API" rm --force "$f" >/dev/null 2>&1 || rm -f "$f"
                fi
            done

            osc -A "$OBS_API" add --force "opencv-${VERSION}.tar.gz" "plan-v4d-${VERSION}.tar.gz" "plan-v4d.spec" >/dev/null 2>&1 || true
            FILES=("opencv-${VERSION}.tar.gz" "plan-v4d-${VERSION}.tar.gz" "plan-v4d.spec")
            ;;
        Ubuntu_24.04)
            # Ubuntu: merged opencv tarball + deb transform files
            cp "$STAGE/opencv-deb-${VERSION}.tar.gz" "opencv-${VERSION}.tar.gz"
            cp "$STAGE/ubuntu/debian.changelog" .
            cp "$STAGE/ubuntu/debian.control" .
            cp "$STAGE/ubuntu/debian.rules" .
            cp "$STAGE/ubuntu/debian.tar.gz" .
            cp "$STAGE/ubuntu/plan-v4d.dsc" .

            # Remove RPM-only leftovers
            if [[ -e "plan-v4d-${VERSION}.tar.gz" ]]; then
                osc -A "$OBS_API" rm --force "plan-v4d-${VERSION}.tar.gz" >/dev/null 2>&1 || true
            fi
            if [[ -e plan-v4d.spec ]]; then
                osc -A "$OBS_API" rm --force plan-v4d.spec >/dev/null 2>&1 || true
            fi

            osc -A "$OBS_API" add --force "opencv-${VERSION}.tar.gz" debian.changelog debian.control debian.rules debian.tar.gz plan-v4d.dsc >/dev/null 2>&1 || true
            FILES=("opencv-${VERSION}.tar.gz" "debian.changelog" "debian.control" "debian.rules" "debian.tar.gz" "plan-v4d.dsc")
            ;;
        Raspbian_12)
            # Raspbian: merged opencv tarball + deb transform files (arm-specific)
            cp "$STAGE/opencv-deb-${VERSION}.tar.gz" "opencv-${VERSION}.tar.gz"
            cp "$STAGE/raspbian/debian.changelog" .
            cp "$STAGE/raspbian/debian.control" .
            cp "$STAGE/raspbian/debian.rules" .
            cp "$STAGE/raspbian/debian.tar.gz" .
            cp "$STAGE/raspbian/plan-v4d.dsc" .

            # Remove RPM-only leftovers
            if [[ -e "plan-v4d-${VERSION}.tar.gz" ]]; then
                osc -A "$OBS_API" rm --force "plan-v4d-${VERSION}.tar.gz" >/dev/null 2>&1 || true
            fi
            if [[ -e plan-v4d.spec ]]; then
                osc -A "$OBS_API" rm --force plan-v4d.spec >/dev/null 2>&1 || true
            fi

            osc -A "$OBS_API" add --force "opencv-${VERSION}.tar.gz" debian.changelog debian.control debian.rules debian.tar.gz plan-v4d.dsc >/dev/null 2>&1 || true
            FILES=("opencv-${VERSION}.tar.gz" "debian.changelog" "debian.control" "debian.rules" "debian.tar.gz" "plan-v4d.dsc")
            ;;
    esac

    echo "  committing ${FILES[*]}"
    osc -A "$OBS_API" commit -m "Regenerate sources: Plan-V4D ${VERSION}-${REVISION} (${target})" --noservice

    if [[ "$NO_REBUILD" != "true" ]]; then
        echo "  triggering rebuild (${REPO})"
        osc -A "$OBS_API" rebuild "$PROJECT/$PACKAGE" "$REPO" || echo "  (rebuild request failed)"
    fi

    popd >/dev/null
    echo ""
done

echo "=== Done ==="
echo ""
echo "Monitor builds with: ./osc-build.sh --status"
if [[ "$NO_REBUILD" == "true" ]]; then
    echo "Restart builds with: for t in openSUSE_Tumbleweed Fedora Ubuntu_24.04 Raspbian_12; do ./osc-build.sh --rebuild \$t; done"
fi
