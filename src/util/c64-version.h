/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_VERSION_H
#define C64_VERSION_H

// Version information (will be populated by build system)
#ifndef C64_VERSION_TAG
#define C64_VERSION_TAG "dev"
#endif

#ifndef C64_GIT_HASH
#define C64_GIT_HASH "unknown"
#endif

#ifndef C64_BUILD_TIME
#define C64_BUILD_TIME "unknown"
#endif

// Function to get formatted version string
const char *c64_get_version_string(void);

// Function to get build information
const char *c64_get_build_info(void);

#endif /* C64_VERSION_H */
