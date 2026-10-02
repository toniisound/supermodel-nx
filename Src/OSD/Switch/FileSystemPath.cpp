/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **/

/*
 * FileSystemPath.cpp (Nintendo Switch)
 *
 * Everything lives under one folder on the SD card (SWITCH_SUPERMODEL_ROOT).
 * Absolute paths are used because Main.cpp builds some of them in static
 * initializers, before main() has changed directory.
 */

#include "../FileSystemPath.h"
#include "SwitchPlatform.h"
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace FileSystemPath
{
    bool PathExists(std::string fileSystemPath)
    {
        struct stat pathInfo;
        return stat(fileSystemPath.c_str(), &pathInfo) == 0 && S_ISDIR(pathInfo.st_mode);
    }

    int MakeDir(std::string dir)
    {
        if (!PathExists(dir))
            return mkdir(dir.c_str(), 0777);
        return 0;
    }

    std::string GetPath(PathType pathType)
    {
        const char *sub = "";
        switch (pathType)
        {
        case Analysis:    sub = "Analysis";    break;
        case Config:      sub = "Config";      break;
        case Log:         sub = "Log";         break;
        case NVRAM:       sub = "NVRAM";       break;
        case Saves:       sub = "Saves";       break;
        case Screenshots: sub = "Screenshots"; break;
        case Assets:      sub = "Assets";      break;
        }

        const std::string root = SWITCH_SUPERMODEL_ROOT;
        MakeDir("sdmc:/switch");
        MakeDir(root);
        std::string path = root + "/" + sub;
        MakeDir(path);
        return path + "/";
    }
}
