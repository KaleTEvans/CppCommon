/*!
    \file stack_trace.cpp
    \brief Stack trace snapshot provider implementation
    \author Ivan Shynkarenka
    \date 09.02.2016
    \copyright MIT License
*/

#include "debug/stack_trace.h"
#include "threads/critical_section.h"

#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#if defined(DBGHELP_SUPPORT)
#include <DbgHelp.h>
#endif
#elif defined(unix) || defined(__unix) || defined(__unix__)
#include <execinfo.h>
#if defined(LIBDL_SUPPORT)
#include <cxxabi.h>
#include <dlfcn.h>
#endif
#endif

namespace CppCommon {

std::string StackTrace::Frame::to_string() const
{
    std::stringstream stream;
    // Format stack trace frame address
    std::ios_base::fmtflags flags = stream.flags();
    stream << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2 * sizeof(uintptr_t)) << (uintptr_t)address << ": ";
    stream.flags(flags);
    // Format stack trace frame other fields
    stream << (module.empty() ? "<unknown>" : module) << '!';
    stream << (function.empty() ? "??" : function) << ' ';
    stream << filename;
    if (line > 0)
        stream << '(' << line << ')';
    return stream.str();
}

StackTrace::StackTrace(int skip)
{
#if defined(_WIN32) || defined(_WIN64)
    const int capacity = 1024;
    void* frames[capacity];

    // Capture the current stack trace
    USHORT captured = CaptureStackBackTrace(skip + 1, capacity, frames, nullptr);

    // Resize stack trace frames vector
    _frames.resize(captured);

    // Capture stack trace snapshot under the critical section
    static CriticalSection cs;
    std::lock_guard<CriticalSection> locker(cs);

    // Get the current process handle
    HANDLE hProcess = GetCurrentProcess();

    // Fill all captured frames with symbol information
    for (int i = 0; i < captured; ++i)
    {
        auto& frame = _frames[i];

        // Get the frame address
        frame.address = frames[i];

#if defined(DBGHELP_SUPPORT)
        // Get the frame module
        IMAGEHLP_MODULE64 module;
        ZeroMemory(&module, sizeof(module));
        module.SizeOfStruct = sizeof(module);
        if (SymGetModuleInfo64(hProcess, (DWORD64)frame.address, &module))
        {
            const char* image = std::strrchr(module.ImageName, '\\');
            if (image != nullptr)
                frame.module = image + 1;
        }

        // Get the frame function
        char symbol[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        ZeroMemory(&symbol, sizeof(symbol));
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symbol;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;
        if (SymFromAddr(hProcess, (DWORD64)frame.address, nullptr, pSymbol))
        {
            char buffer[4096];
            if (UnDecorateSymbolName(pSymbol->Name, buffer, sizeof(buffer), UNDNAME_NAME_ONLY) > 0)
                frame.function = buffer;
        }

        // Get the frame file name and line number
        DWORD offset = 0;
        IMAGEHLP_LINE64 line;
        ZeroMemory(&line, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        if (SymGetLineFromAddr64(hProcess, (DWORD64)frame.address, &offset, &line))
        {
            if (line.FileName != nullptr)
                frame.filename = line.FileName;
            frame.line = line.LineNumber;
        }
#endif
    }
#elif defined(LIBDL_SUPPORT) && defined(unix) || defined(__unix) || defined(__unix__)
    const int capacity = 1024;
    void* frames[capacity];

    // Capture the current stack trace
    int captured = backtrace(frames, capacity);
    int index = skip + 1;
    int size = captured - index;

    // Check the current stack trace size
    if (size <= 0)
        return;

    // Resize stack trace frames vector
    _frames.resize(size);

    // Fill all captured frames with symbol information
    for (int i = 0; i < size; ++i)
    {
        auto& frame = _frames[i];

        // Get the frame address
        frame.address = frames[index + i];

#if defined(LIBDL_SUPPORT)
        // Get the frame information
        Dl_info info;
        if (dladdr(frames[index + i], &info) == 0)
            continue;

        // Get the frame module
        if (info.dli_fname != nullptr)
        {
            const char* module = std::strrchr(info.dli_fname, '/');
            if (module != nullptr)
                frame.module = module + 1;
        }

        // Get the frame function
        if (info.dli_sname != nullptr)
        {
            // Demangle symbol name if need
            int status;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, 0, &status);
            if ((status == 0) && (demangled != nullptr))
            {
                frame.function = demangled;
                free(demangled);
            }
            else
                frame.function = info.dli_sname;
        }
#endif
#if defined(LIBBFD_SUPPORT)
        if ((frame.address == nullptr) || (info.dli_fname == nullptr))
            continue;

        bfd *abfd = bfd_openr(info.dli_fname, nullptr);
        if (abfd == nullptr)
            continue;

        if (bfd_check_format (abfd, bfd_archive))
            goto cleanup;

        char **matching;
        if (!bfd_check_format_matches(abfd, bfd_object, &matching))
            goto cleanup;

        if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
            goto cleanup;

        unsigned int size;
        long symcount = bfd_read_minisymbols(abfd, FALSE, (void*)&syms, &size);
        if (symcount == 0)
            symcount = bfd_read_minisymbols(abfd, TRUE, (void*)&syms, &size);
        if (symcount < 0)
            goto cleanup;

        const char* filename;
        const char* functionname;
        unsigned int line;

        bfd_boolean found = false;
        bfd_vma pc = bfd_scan_vma(frame.address, NULL, 16);
        bfd_map_over_sections(abfd, [&fount, &pc, &filename, &functionname, &line](bfd* abfd, asection* section, void* data)
        {
            if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
                return;

            bfd_vma vma = bfd_get_section_vma(abfd, section);
            if (pc < vma)
                return;

            bfd_size_type size = bfd_get_section_size(section);
            if (pc >= vma + size)
                return;

            found = bfd_find_nearest_line(abfd, section, syms, pc - vma, &filename, &functionname, &line);
        }, NULL);

        if (!found)
            goto cleanup;

        if (filename != nullptr)
            frame.filename = filename;
        frame.line = line;

cleanup:
        if (syms != nullptr)
            free (syms);

        bfd_close(abfd);
#endif
    }
#endif
}

std::string StackTrace::to_string() const
{
    std::stringstream stream;
    for (auto& frame : _frames)
        stream << frame.to_string() << std::endl;
    return stream.str();
}

} // namespace CppCommon