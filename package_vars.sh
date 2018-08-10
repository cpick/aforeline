# intended to be sourced by other /bin/sh scripts

PACKAGE_NAME="`dpkg-parsechangelog --show-field Source`"
PACKAGE_VERSION="`dpkg-parsechangelog --show-field Version`"
PACKAGE_DIR="${PACKAGE_NAME}-${PACKAGE_VERSION%-*}"
PACKAGE_FILE_PREFIX="${PACKAGE_NAME}_${PACKAGE_VERSION}"
CHANGES_FILE="${PACKAGE_FILE_PREFIX}_source.changes"
