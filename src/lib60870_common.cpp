/*
 *  Copyright 2016 MZ Automation GmbH
 *
 *  This file is part of lib60870-C
 *
 *  lib60870-C is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  lib60870-C is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with lib60870-C.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  See COPYING file for the complete license text.
 */

#include "iec60870_common.h"
#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern "C"
{
    void
    lib60870_debug_print(const char *format, ...)
    {
        if (Debug.IsEnabled()) {
            char buf[128] = {};
            va_list ap;
            va_start(ap, format);
            vsnprintf(buf, sizeof(buf), format, ap);
            va_end(ap);
            auto l = strlen(buf);
            if (l > 0) {
                buf[l-1] = 0;
            }
            Debug.Log() << "[IEC] " << buf;
        }
    }

    void
    Lib60870_enableDebugOutput(bool)
    { }


    Lib60870VersionInfo
    Lib60870_getLibraryVersionInfo()
    {
        Lib60870VersionInfo versionInfo;

        versionInfo.major = LIB60870_VERSION_MAJOR;
        versionInfo.minor = LIB60870_VERSION_MINOR;
        versionInfo.patch = LIB60870_VERSION_PATCH;

        return versionInfo;
    }

}