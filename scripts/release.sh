#!/bin/bash

# npad release script
# Usage: ./scripts/release.sh [version]
# Example: ./scripts/release.sh v1.0.0

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if version is provided
if [ $# -eq 0 ]; then
    print_error "No version provided"
    echo "Usage: $0 <version>"
    echo "Example: $0 v1.0.0"
    exit 1
fi

VERSION=$1

# Validate semantic version format
if ! [[ $VERSION =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.-]+)?(\+[a-zA-Z0-9.-]+)?$ ]]; then
    print_error "Invalid semantic version format: $VERSION"
    echo "Expected format: v1.2.3, v1.2.3-alpha, v1.2.3+build"
    exit 1
fi

print_status "Preparing release $VERSION"

# Check if we're on main branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$CURRENT_BRANCH" != "main" ]; then
    print_warning "Not on main branch (currently on: $CURRENT_BRANCH)"
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_error "Aborted"
        exit 1
    fi
fi

# Check if working directory is clean
if ! git diff-index --quiet HEAD --; then
    print_error "Working directory is not clean. Please commit or stash changes."
    exit 1
fi

# Check if tag already exists
if git rev-parse "$VERSION" >/dev/null 2>&1; then
    print_error "Tag $VERSION already exists"
    exit 1
fi

print_status "Running pre-release checks..."

# Run linting
print_status "Running code linting..."
make lint || {
    print_error "Linting failed"
    exit 1
}

# Run formatting check
print_status "Checking code formatting..."
make format-check || {
    print_error "Code formatting check failed. Run 'make format' to fix."
    exit 1
}

# Build all targets to ensure they compile
print_status "Testing builds..."

print_status "Building Windows (x64 GUI + terminal)..."
make clean && make windows || {
    print_error "Windows build failed"
    exit 1
}

print_status "Building Linux variants..."
make clean && make linux || {
    print_error "Linux build failed"
    exit 1
}

print_status "Running unit tests..."
make test || {
    print_error "Unit tests failed"
    exit 1
}

print_status "All builds successful!"

# Update version in README if needed
print_status "Checking version references..."

# Clean up build artifacts
make clean

print_status "Creating and pushing tag..."

# Create the tag
git tag -a "$VERSION" -m "Release $VERSION"

# Push the tag
git push origin "$VERSION"

print_status "Tag $VERSION created and pushed successfully!"
print_status "GitHub Actions will now build and create the release automatically."
print_status "Check the progress at: https://github.com/platima/npad/actions"

echo
print_status "Release $VERSION initiated successfully! 🎉"
print_status "The automated build will create release artifacts for:"
print_status "  - Windows installer (npad-setup-<v>.exe) and MSI"
print_status "  - Windows portable GUI executable"
print_status "  - SHA256 checksums for everything"
print_status "(Linux/terminal variants are unimplemented stubs: compile-checked in CI, not released)"