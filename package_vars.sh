# intended to be sourced by other /bin/sh scripts

PACKAGE_NAME="`dpkg-parsechangelog --show-field Source`"
PACKAGE_VERSION="`dpkg-parsechangelog --show-field Version`"
PACKAGE_ORIG_VERSION="${PACKAGE_VERSION%-*}"
PACKAGE_DIR="${PACKAGE_NAME}-${PACKAGE_ORIG_VERSION}"
PACKAGE_FILE_PREFIX="${PACKAGE_NAME}_${PACKAGE_VERSION}"
PACKAGE_ORIG_FILE="${PACKAGE_NAME}_${PACKAGE_ORIG_VERSION}.orig.tar.gz"
CHANGES_FILE="${PACKAGE_FILE_PREFIX}_source.changes"
