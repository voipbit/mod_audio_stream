#!/usr/bin/env bash
# =============================================================================
# mod_audio_stream - Docker Build Script
# =============================================================================
#
# This script builds the mod_audio_stream Docker image with proper versioning
# and metadata for development and production deployments.
#
# Usage:
#   ./docker/build.sh [options]
#
# Options:
#   --version VERSION     Set the image version (default: auto-detect from git)
#   --tag TAG             Set additional image tag
#   --push                Push image to registry after build
#   --no-cache            Build without using cache
#   --platform PLATFORM   Target platform (e.g., linux/amd64,linux/arm64)
#   --registry REGISTRY   Docker registry URL
#   --help                Show this help message
#

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================

# Default values
DEFAULT_VERSION="1.0.0"
DEFAULT_REGISTRY="docker.io"
DEFAULT_NAMESPACE="freeswitch"
DEFAULT_IMAGE_NAME="mod_audio_stream"
DEFAULT_PLATFORM="linux/amd64"
DEFAULT_FREESWITCH_VERSION="1.10.11"

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Initialize variables
VERSION=""
ADDITIONAL_TAG=""
PUSH_IMAGE=false
NO_CACHE=false
PLATFORM="${DEFAULT_PLATFORM}"
REGISTRY="${DEFAULT_REGISTRY}"
NAMESPACE="${DEFAULT_NAMESPACE}"
IMAGE_NAME="${DEFAULT_IMAGE_NAME}"
FREESWITCH_VERSION="${DEFAULT_FREESWITCH_VERSION}"
HELP=false

# =============================================================================
# Functions
# =============================================================================

show_help() {
    cat << EOF
mod_audio_stream Docker Build Script

USAGE:
    $(basename "$0") [OPTIONS]

OPTIONS:
    --version VERSION     Set the image version (default: auto-detect from git)
    --tag TAG             Set additional image tag
    --push                Push image to registry after build
    --no-cache            Build without using cache
    --platform PLATFORM   Target platform (default: ${DEFAULT_PLATFORM})
    --registry REGISTRY   Docker registry URL (default: ${DEFAULT_REGISTRY})
    --namespace NAMESPACE Image namespace (default: ${DEFAULT_NAMESPACE})
    --image-name NAME     Image name (default: ${DEFAULT_IMAGE_NAME})
    --freeswitch-version  FreeSWITCH version (default: ${DEFAULT_FREESWITCH_VERSION})
    --help                Show this help message

EXAMPLES:
    # Build with auto-detected version
    $(basename "$0")

    # Build specific version and push
    $(basename "$0") --version 1.2.3 --push

    # Build multi-platform image
    $(basename "$0") --platform linux/amd64,linux/arm64

    # Build for custom registry
    $(basename "$0") --registry myregistry.com --namespace myorg
EOF
}

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >&2
}

error() {
    log "ERROR: $*" >&2
    exit 1
}

# Detect version from git if not specified
detect_version() {
    if [ -n "${VERSION}" ]; then
        return
    fi

    if command -v git >/dev/null 2>&1 && [ -d "${PROJECT_ROOT}/.git" ]; then
        # Try to get version from git tag
        if git describe --tags --exact-match >/dev/null 2>&1; then
            VERSION=$(git describe --tags --exact-match | sed 's/^v//')
            log "Detected version from git tag: ${VERSION}"
        else
            # Use commit hash if no tag
            local commit_hash
            commit_hash=$(git rev-parse --short HEAD)
            local branch
            branch=$(git rev-parse --abbrev-ref HEAD)
            VERSION="${DEFAULT_VERSION}-${branch}-${commit_hash}"
            log "Generated version from git: ${VERSION}"
        fi
    else
        VERSION="${DEFAULT_VERSION}"
        log "Using default version: ${VERSION}"
    fi
}

# Get build metadata
get_build_metadata() {
    local build_date
    build_date=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    
    local vcs_ref=""
    if command -v git >/dev/null 2>&1 && [ -d "${PROJECT_ROOT}/.git" ]; then
        vcs_ref=$(git rev-parse HEAD)
    fi
    
    echo "BUILD_DATE=${build_date}"
    echo "VCS_REF=${vcs_ref}"
}

# Build Docker image
build_image() {
    local build_metadata
    build_metadata=$(get_build_metadata)
    
    local build_date vcs_ref
    eval "${build_metadata}"
    
    local full_image_name="${REGISTRY}/${NAMESPACE}/${IMAGE_NAME}"
    local cache_args=""
    local platform_args=""
    
    if [ "${NO_CACHE}" = true ]; then
        cache_args="--no-cache"
    fi
    
    if [ -n "${PLATFORM}" ]; then
        platform_args="--platform ${PLATFORM}"
    fi
    
    log "Building Docker image: ${full_image_name}:${VERSION}"
    log "FreeSWITCH version: ${FREESWITCH_VERSION}"
    log "Platform: ${PLATFORM}"
    log "Build date: ${build_date}"
    log "VCS ref: ${vcs_ref}"
    
    # Build command
    local build_cmd=(
        docker build
        ${cache_args}
        ${platform_args}
        --file "${SCRIPT_DIR}/Dockerfile"
        --build-arg "BUILD_DATE=${build_date}"
        --build-arg "VCS_REF=${vcs_ref}"
        --build-arg "VERSION=${VERSION}"
        --build-arg "FREESWITCH_VERSION=${FREESWITCH_VERSION}"
        --tag "${full_image_name}:${VERSION}"
        --tag "${full_image_name}:latest"
    )
    
    # Add additional tag if specified
    if [ -n "${ADDITIONAL_TAG}" ]; then
        build_cmd+=(--tag "${full_image_name}:${ADDITIONAL_TAG}")
    fi
    
    # Add project root as build context
    build_cmd+=("${PROJECT_ROOT}")
    
    # Execute build
    log "Executing: ${build_cmd[*]}"
    "${build_cmd[@]}"
    
    log "Build completed successfully!"
    
    # Show image information
    docker images "${full_image_name}" --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.CreatedAt}}\t{{.Size}}"
}

# Push image to registry
push_image() {
    local full_image_name="${REGISTRY}/${NAMESPACE}/${IMAGE_NAME}"
    
    log "Pushing images to registry..."
    
    docker push "${full_image_name}:${VERSION}"
    docker push "${full_image_name}:latest"
    
    if [ -n "${ADDITIONAL_TAG}" ]; then
        docker push "${full_image_name}:${ADDITIONAL_TAG}"
    fi
    
    log "Push completed successfully!"
}

# Validate requirements
validate_requirements() {
    if ! command -v docker >/dev/null 2>&1; then
        error "Docker is not installed or not in PATH"
    fi
    
    if [ ! -f "${SCRIPT_DIR}/Dockerfile" ]; then
        error "Dockerfile not found: ${SCRIPT_DIR}/Dockerfile"
    fi
    
    if [ ! -d "${PROJECT_ROOT}" ]; then
        error "Project root directory not found: ${PROJECT_ROOT}"
    fi
}

# =============================================================================
# Argument Parsing
# =============================================================================

while [[ $# -gt 0 ]]; do
    case $1 in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --tag)
            ADDITIONAL_TAG="$2"
            shift 2
            ;;
        --push)
            PUSH_IMAGE=true
            shift
            ;;
        --no-cache)
            NO_CACHE=true
            shift
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --registry)
            REGISTRY="$2"
            shift 2
            ;;
        --namespace)
            NAMESPACE="$2"
            shift 2
            ;;
        --image-name)
            IMAGE_NAME="$2"
            shift 2
            ;;
        --freeswitch-version)
            FREESWITCH_VERSION="$2"
            shift 2
            ;;
        --help)
            HELP=true
            shift
            ;;
        *)
            error "Unknown option: $1"
            ;;
    esac
done

# =============================================================================
# Main Execution
# =============================================================================

main() {
    if [ "${HELP}" = true ]; then
        show_help
        exit 0
    fi
    
    log "Starting mod_audio_stream Docker build..."
    
    # Validate requirements
    validate_requirements
    
    # Detect version if not specified
    detect_version
    
    # Build the image
    build_image
    
    # Push if requested
    if [ "${PUSH_IMAGE}" = true ]; then
        push_image
    fi
    
    log "Build process completed successfully!"
    log "Image: ${REGISTRY}/${NAMESPACE}/${IMAGE_NAME}:${VERSION}"
}

# Execute main function
main "$@"