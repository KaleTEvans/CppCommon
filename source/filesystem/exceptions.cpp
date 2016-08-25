/*!
    \file exceptions.cpp
    \brief File system exceptions implementation
    \author Ivan Shynkarenka
    \date 24.08.2016
    \copyright MIT License
*/

#include "filesystem/exceptions.h"

namespace CppCommon {

std::string FileSystemException::to_string() const
{
    if (_cache.empty())
    {
        std::stringstream stream;
        stream << "File system exception: " << _message << std::endl;
        stream << "File system path: " << _path << std::endl;
        stream << "System error: " << _system_error << std::endl;
        stream << "System message: " << _system_message << std::endl;
        std::string location = _location.to_string();
        if (!location.empty())
            stream << "Source location: " << location << std::endl;
        _cache = stream.str();
    }
    return _cache;
}

} // namespace CppCommon