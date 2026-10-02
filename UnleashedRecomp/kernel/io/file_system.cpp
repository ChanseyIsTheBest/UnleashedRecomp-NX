#include "file_system.h"
#include <cpu/guest_thread.h>
#include <kernel/xam.h>
#include <kernel/xdm.h>
#include <kernel/function.h>
#include <mod/mod_loader.h>
#include <os/logger.h>
#include <user/config.h>
#include <stdafx.h>
#if defined(__SWITCH__)
#include <user/paths.h>
#include <switch.h>
#include <climits>
#endif

struct FileHandle : KernelObject
{
    std::fstream stream;
    std::filesystem::path path;
#if defined(__SWITCH__)
    bool readOnly = false;
#endif
};

#if defined(__SWITCH__)
// [Switch] Round 11. Each std::filesystem query of a path on the SD card is several IPCs to the file system
// service: a stat is 5 (entry type, open, size, close, timestamps) plus 3 time zone conversions.

// SwitchNativeFindFile: whether the directory lies in the game's own content (game, update, dlc), which nothing
// writes while it runs; the save and user folders keep the std path (a file open for writing stats differently).
static bool IsGameContentDirectory(const std::string& directory)
{
    for (const char* root : { "game", "update", "dlc" })
    {
        const std::string rootPath = (const char*)(GetGamePath() / root).u8string().c_str();
        if (directory.size() >= rootPath.size() && directory.compare(0, rootPath.size(), rootPath) == 0 &&
            (directory.size() == rootPath.size() || directory[rootPath.size()] == '/'))
        {
            return true;
        }
    }

    return false;
}

// SwitchNativeFindFile: the entries of a directory from the directory reads alone, where std::filesystem stats every
// file for its size. The same entries in the same order (the service's), the same names (an ASCII name is copied
// as it is by fsdev_dirnext too), the same sizes (the entry's size is the file's). Anything else (a directory that
// cannot be read, an entry that is neither a file nor a directory, a name the std path would convert or refuse)
// leaves the directory to the std path, which then does exactly what it did.
static bool AddDirectoryNative(const std::filesystem::path& directory,
    ankerl::unordered_dense::map<std::u8string, std::pair<size_t, bool>>& searchResult)
{
    const std::string path = (const char*)directory.u8string().c_str();
    if (!IsGameContentDirectory(path))
        return false;

    FsFileSystem* fileSystem = nullptr;
    char fsPath[FS_MAX_PATH];
    if (fsdevTranslatePath(path.c_str(), &fileSystem, fsPath) == -1)
        return false;

    FsDir dir;
    if (R_FAILED(fsFsOpenDirectory(fileSystem, fsPath, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir)))
        return false;

    std::vector<FsDirectoryEntry> entries(64);
    std::vector<std::pair<std::u8string, std::pair<size_t, bool>>> found;
    bool usable = true;
    while (usable)
    {
        s64 count = 0;
        if (R_FAILED(fsDirRead(&dir, &count, entries.size(), entries.data())))
        {
            usable = false;
            break;
        }

        if (count == 0)
            break;

        for (s64 i = 0; i < count && usable; i++)
        {
            const FsDirectoryEntry& entry = entries[i];
            const size_t length = strnlen(entry.name, sizeof(entry.name));
            usable = (entry.type == FsDirEntryType_Dir || entry.type == FsDirEntryType_File) && length != 0 && length < NAME_MAX;
            for (size_t k = 0; k < length && usable; k++)
                usable = uint8_t(entry.name[k]) < 0x80;

            if (usable)
            {
                const bool isDirectory = entry.type == FsDirEntryType_Dir;
                const std::u8string relativePath =
                    (directory / std::u8string_view((const char8_t*)entry.name, length)).lexically_relative(directory).u8string();
                found.emplace_back(relativePath, std::make_pair(isDirectory ? size_t(0) : size_t(entry.file_size), isDirectory));
            }
        }
    }

    fsDirClose(&dir);
    if (!usable)
        return false;

    for (auto& [relativePath, value] : found)
        searchResult.emplace(std::move(relativePath), value);

    return true;
}

// SwitchNativeFileHandles: a read-only file's size from its open handle (one IPC) instead of a stat of its path. The
// file stream's own buffer seeks to the end and back, which leaves the stream's state (fail, eof) as it was and
// only makes it read its next bytes again from the file.
static bool NativeFileSize(FileHandle* hFile, uint64_t& size)
{
    if (!Config::SwitchNativeFileHandles || !hFile->readOnly)
        return false;

    std::filebuf* buffer = hFile->stream.rdbuf();
    const std::streampos invalid = std::streampos(std::streamoff(-1));
    const std::streampos position = buffer->pubseekoff(0, std::ios::cur, std::ios::in);
    if (position == invalid)
        return false;

    const std::streampos end = buffer->pubseekoff(0, std::ios::end, std::ios::in);
    const std::streampos back = buffer->pubseekpos(position, std::ios::in);
    if (end == invalid || back != position)
        return false;

    size = uint64_t(std::streamoff(end));
    return true;
}

// SwitchNativeFileHandles: whether a path exists, from its entry type (one IPC) instead of a stat. Anything but
// "found" or "not found" asks std::filesystem.
static bool PathExists(const std::string& path)
{
    std::error_code ec;
    if (!Config::SwitchNativeFileHandles)
        return std::filesystem::exists(path, ec);

    FsFileSystem* fileSystem = nullptr;
    char fsPath[FS_MAX_PATH];
    if (fsdevTranslatePath(path.c_str(), &fileSystem, fsPath) == -1)
        return std::filesystem::exists(path, ec);

    FsDirEntryType type;
    const Result result = fsFsGetEntryType(fileSystem, fsPath, &type);
    if (R_SUCCEEDED(result))
        return true;
    if (R_MODULE(result) == 2 && R_DESCRIPTION(result) == 1) // fs: path not found (2-0001)
        return false;

    return std::filesystem::exists(path, ec);
}
#endif

struct FindHandle : KernelObject
{
    std::error_code ec;
    ankerl::unordered_dense::map<std::u8string, std::pair<size_t, bool>> searchResult; // Relative path, file size, is directory
    decltype(searchResult)::iterator iterator;

    FindHandle(const std::string_view& path)
    {
        auto addDirectory = [&](const std::filesystem::path& directory)
            {
#if defined(__SWITCH__)
                if (Config::SwitchNativeFindFile && AddDirectoryNative(directory, searchResult))
                    return;
#endif
                for (auto& entry : std::filesystem::directory_iterator(directory, ec))
                {
                    std::u8string relativePath = entry.path().lexically_relative(directory).u8string();
                    searchResult.emplace(relativePath, std::make_pair(entry.is_directory(ec) ? 0 : entry.file_size(ec), entry.is_directory(ec)));
                }
            };

        std::string_view pathNoPrefix = path;
        size_t index = pathNoPrefix.find(":\\");
        if (index != std::string_view::npos)
            pathNoPrefix.remove_prefix(index + 2);

        // Force add a work folder to let the game see the files in mods,
        // if by some rare chance the user has no DLC or update files.
        if (pathNoPrefix.empty())
            searchResult.emplace(u8"work", std::make_pair(0, true));

        // Look for only work folder in mod folders, AR files cause issues.
        if (pathNoPrefix.starts_with("work"))
        {
            std::string pathStr(pathNoPrefix);
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

            for (size_t i = 0; ; i++)
            {
                auto* includeDirs = ModLoader::GetIncludeDirectories(i);
                if (includeDirs == nullptr)
                    break;

                for (auto& includeDir : *includeDirs)
                    addDirectory(includeDir / pathStr);
            }
        }

        addDirectory(FileSystem::ResolvePath(path, false));

        iterator = searchResult.begin();
    }

    void fillFindData(WIN32_FIND_DATAA* lpFindFileData)
    {
        if (iterator->second.second)
            lpFindFileData->dwFileAttributes = ByteSwap(FILE_ATTRIBUTE_DIRECTORY);
        else
            lpFindFileData->dwFileAttributes = ByteSwap(FILE_ATTRIBUTE_NORMAL);

        strncpy(lpFindFileData->cFileName, (const char *)(iterator->first.c_str()), sizeof(lpFindFileData->cFileName));
        lpFindFileData->nFileSizeLow = ByteSwap(uint32_t(iterator->second.first >> 32U));
        lpFindFileData->nFileSizeHigh = ByteSwap(uint32_t(iterator->second.first));
        lpFindFileData->ftCreationTime = {};
        lpFindFileData->ftLastAccessTime = {};
        lpFindFileData->ftLastWriteTime = {};
    }
};

FileHandle* XCreateFileA
(
    const char* lpFileName,
    uint32_t dwDesiredAccess,
    uint32_t dwShareMode,
    void* lpSecurityAttributes,
    uint32_t dwCreationDisposition,
    uint32_t dwFlagsAndAttributes
)
{
    assert(((dwDesiredAccess & ~(GENERIC_READ | GENERIC_WRITE | FILE_READ_DATA)) == 0) && "Unknown desired access bits.");
    assert(((dwShareMode & ~(FILE_SHARE_READ | FILE_SHARE_WRITE)) == 0) && "Unknown share mode bits.");
    assert(((dwCreationDisposition & ~(CREATE_NEW | CREATE_ALWAYS)) == 0) && "Unknown creation disposition bits.");

    std::filesystem::path filePath = FileSystem::ResolvePath(lpFileName, true);
    std::fstream fileStream;
    std::ios::openmode fileOpenMode = std::ios::binary;
    if (dwDesiredAccess & (GENERIC_READ | FILE_READ_DATA))
    {
        fileOpenMode |= std::ios::in;
    }

    if (dwDesiredAccess & GENERIC_WRITE)
    {
        fileOpenMode |= std::ios::out;
    }

    fileStream.open(filePath, fileOpenMode);
    if (!fileStream.is_open())
    {
#ifdef _WIN32
        GuestThread::SetLastError(GetLastError());
#endif
        return GetInvalidKernelObject<FileHandle>();
    }

    FileHandle *fileHandle = CreateKernelObject<FileHandle>();
    fileHandle->stream = std::move(fileStream);
    fileHandle->path = std::move(filePath);
#if defined(__SWITCH__)
    fileHandle->readOnly = (dwDesiredAccess & GENERIC_WRITE) == 0;
#endif
    return fileHandle;
}

static uint32_t XGetFileSizeA(FileHandle* hFile, be<uint32_t>* lpFileSizeHigh)
{
    std::error_code ec;
#if defined(__SWITCH__)
    uint64_t nativeSize;
    auto fileSize = NativeFileSize(hFile, nativeSize) ? nativeSize : std::filesystem::file_size(hFile->path, ec);
#else
    auto fileSize = std::filesystem::file_size(hFile->path, ec);
#endif
    if (!ec)
    {
        if (lpFileSizeHigh != nullptr)
        {
            *lpFileSizeHigh = uint32_t(fileSize >> 32U);
        }
    
        return (uint32_t)(fileSize);
    }

    return INVALID_FILE_SIZE;
}

uint32_t XGetFileSizeExA(FileHandle* hFile, LARGE_INTEGER* lpFileSize)
{
    std::error_code ec;
#if defined(__SWITCH__)
    uint64_t nativeSize;
    auto fileSize = NativeFileSize(hFile, nativeSize) ? nativeSize : std::filesystem::file_size(hFile->path, ec);
#else
    auto fileSize = std::filesystem::file_size(hFile->path, ec);
#endif
    if (!ec)
    {
        if (lpFileSize != nullptr)
        {
            lpFileSize->QuadPart = ByteSwap(fileSize);
        }

        return TRUE;
    }

    return FALSE;
}

uint32_t XReadFile
(
    FileHandle* hFile,
    void* lpBuffer,
    uint32_t nNumberOfBytesToRead,
    be<uint32_t>* lpNumberOfBytesRead,
    XOVERLAPPED* lpOverlapped
)
{
    uint32_t result = FALSE;
    if (lpOverlapped != nullptr)
    {
        std::streamoff streamOffset = lpOverlapped->Offset + (std::streamoff(lpOverlapped->OffsetHigh.get()) << 32U);
        hFile->stream.clear();
        hFile->stream.seekg(streamOffset, std::ios::beg);
        if (hFile->stream.bad())
        {
            return FALSE;
        }
    }

    uint32_t numberOfBytesRead;
    hFile->stream.read((char *)(lpBuffer), nNumberOfBytesToRead);
    if (!hFile->stream.bad())
    {
        numberOfBytesRead = uint32_t(hFile->stream.gcount());
        result = TRUE;
    }

    if (result)
    {
        if (lpOverlapped != nullptr)
        {
            lpOverlapped->Internal = 0;
            lpOverlapped->InternalHigh = numberOfBytesRead;
        }
        else if (lpNumberOfBytesRead != nullptr)
        {
            *lpNumberOfBytesRead = numberOfBytesRead;
        }
    }

    return result;
}

uint32_t XSetFilePointer(FileHandle* hFile, int32_t lDistanceToMove, be<int32_t>* lpDistanceToMoveHigh, uint32_t dwMoveMethod)
{
    int32_t distanceToMoveHigh = lpDistanceToMoveHigh ? lpDistanceToMoveHigh->get() : 0;
    std::streamoff streamOffset = lDistanceToMove + (std::streamoff(distanceToMoveHigh) << 32U);
    std::fstream::seekdir streamSeekDir = {};
    switch (dwMoveMethod)
    {
    case FILE_BEGIN:
        streamSeekDir = std::ios::beg;
        break;
    case FILE_CURRENT:
        streamSeekDir = std::ios::cur;
        break;
    case FILE_END:
        streamSeekDir = std::ios::end;
        break;
    default:
        assert(false && "Unknown move method.");
        break;
    }

    hFile->stream.clear();
    hFile->stream.seekg(streamOffset, streamSeekDir);
    if (hFile->stream.bad())
    {
        return INVALID_SET_FILE_POINTER;
    }

    std::streampos streamPos = hFile->stream.tellg();
    if (lpDistanceToMoveHigh != nullptr)
        *lpDistanceToMoveHigh = int32_t(streamPos >> 32U);

    return uint32_t(streamPos);
}

uint32_t XSetFilePointerEx(FileHandle* hFile, int32_t lDistanceToMove, LARGE_INTEGER* lpNewFilePointer, uint32_t dwMoveMethod)
{
    std::fstream::seekdir streamSeekDir = {};
    switch (dwMoveMethod)
    {
    case FILE_BEGIN:
        streamSeekDir = std::ios::beg;
        break;
    case FILE_CURRENT:
        streamSeekDir = std::ios::cur;
        break;
    case FILE_END:
        streamSeekDir = std::ios::end;
        break;
    default:
        assert(false && "Unknown move method.");
        break;
    }

    hFile->stream.clear();
    hFile->stream.seekg(lDistanceToMove, streamSeekDir);
    if (hFile->stream.bad())
    {
        return FALSE;
    }

    if (lpNewFilePointer != nullptr)
    {
        lpNewFilePointer->QuadPart = ByteSwap(int64_t(hFile->stream.tellg()));
    }

    return TRUE;
}

FindHandle* XFindFirstFileA(const char* lpFileName, WIN32_FIND_DATAA* lpFindFileData)
{
    std::string_view path = lpFileName;
    if (path.find("\\*") == (path.size() - 2) || path.find("/*") == (path.size() - 2))
    {
        path.remove_suffix(1);
    }
    else if (path.find("\\*.*") == (path.size() - 4) || path.find("/*.*") == (path.size() - 4))
    {
        path.remove_suffix(3);
    }
    else
    {
        assert(!std::filesystem::path(path).has_extension() && "Unknown search pattern.");
    }

    FindHandle findHandle(path);

    if (findHandle.searchResult.empty())
        return GetInvalidKernelObject<FindHandle>();

    findHandle.fillFindData(lpFindFileData);

    return CreateKernelObject<FindHandle>(std::move(findHandle));
}

uint32_t XFindNextFileA(FindHandle* Handle, WIN32_FIND_DATAA* lpFindFileData)
{
    Handle->iterator++;

    if (Handle->iterator == Handle->searchResult.end())
    {
        return FALSE;
    }
    else
    {
        Handle->fillFindData(lpFindFileData);
        return TRUE;
    }
}

uint32_t XReadFileEx(FileHandle* hFile, void* lpBuffer, uint32_t nNumberOfBytesToRead, XOVERLAPPED* lpOverlapped, uint32_t lpCompletionRoutine)
{
    uint32_t result = FALSE;
    uint32_t numberOfBytesRead;
    std::streamoff streamOffset = lpOverlapped->Offset + (std::streamoff(lpOverlapped->OffsetHigh.get()) << 32U);
    hFile->stream.clear();
    hFile->stream.seekg(streamOffset, std::ios::beg);
    if (hFile->stream.bad())
        return FALSE;

    hFile->stream.read((char *)(lpBuffer), nNumberOfBytesToRead);
    if (!hFile->stream.bad())
    {
        numberOfBytesRead = uint32_t(hFile->stream.gcount());
        result = TRUE;
    }

    if (result)
    {
        lpOverlapped->Internal = 0;
        lpOverlapped->InternalHigh = numberOfBytesRead;
    }

    return result;
}

uint32_t XGetFileAttributesA(const char* lpFileName)
{
    std::filesystem::path filePath = FileSystem::ResolvePath(lpFileName, true);
    if (std::filesystem::is_directory(filePath))
        return FILE_ATTRIBUTE_DIRECTORY;
    else if (std::filesystem::is_regular_file(filePath))
        return FILE_ATTRIBUTE_NORMAL;
    else
        return INVALID_FILE_ATTRIBUTES;
}

uint32_t XWriteFile(FileHandle* hFile, const void* lpBuffer, uint32_t nNumberOfBytesToWrite, be<uint32_t>* lpNumberOfBytesWritten, void* lpOverlapped)
{
    assert(lpOverlapped == nullptr && "Overlapped not implemented.");

    hFile->stream.write((const char *)(lpBuffer), nNumberOfBytesToWrite);
    if (hFile->stream.bad())
        return FALSE;

    if (lpNumberOfBytesWritten != nullptr)
        *lpNumberOfBytesWritten = uint32_t(hFile->stream.gcount());

    return TRUE;
}

std::filesystem::path FileSystem::ResolvePath(const std::string_view& path, bool checkForMods)
{
    if (checkForMods)
    {
        std::filesystem::path resolvedPath = ModLoader::ResolvePath(path);

        if (!resolvedPath.empty())
        {
            if (ModLoader::s_isLogTypeConsole)
                LOGF_IMPL(Utility, "Mod Loader", "Loading file: \"{}\"", reinterpret_cast<const char*>(resolvedPath.u8string().c_str()));

            return resolvedPath;
        }
    }

    thread_local std::string builtPath;
    builtPath.clear();

    size_t index = path.find(":\\");
    if (index != std::string::npos)
    {
        // rooted folder, handle direction
        std::string_view root = path.substr(0, index);

        // HACK: The game tries to load the work folder from the "game" root path
        // for Application and shader archives. Prefer the update overlay when the
        // requested path exists there, but do not force partial installs into a
        // missing update path.
        const std::string_view pathNoRoot = path.substr(index + 2);
        if (path.starts_with("game:\\work\\"))
        {
            const auto updateRoot = XamGetRootPath("update");
            if (!updateRoot.empty())
            {
                std::string updatePath(updateRoot);
                updatePath += '/';
                updatePath += pathNoRoot;
                std::replace(updatePath.begin(), updatePath.end(), '\\', '/');

#if defined(__SWITCH__)
                if (PathExists(updatePath))
                    root = "update";
#else
                std::error_code ec;
                if (std::filesystem::exists(updatePath, ec))
                    root = "update";
#endif
            }
        }

        const auto newRoot = XamGetRootPath(root);

        if (!newRoot.empty())
        {
            builtPath += newRoot;
            builtPath += '/';
        }
        
        builtPath += pathNoRoot;
    }
    else
    {
        builtPath += path;
    }

    std::replace(builtPath.begin(), builtPath.end(), '\\', '/');

    return std::u8string_view((const char8_t*)builtPath.c_str());
}

GUEST_FUNCTION_HOOK(sub_82BD4668, XCreateFileA);
GUEST_FUNCTION_HOOK(sub_82BD4600, XGetFileSizeA);
GUEST_FUNCTION_HOOK(sub_82BD5608, XGetFileSizeExA);
GUEST_FUNCTION_HOOK(sub_82BD4478, XReadFile);
GUEST_FUNCTION_HOOK(sub_831CD3E8, XSetFilePointer);
GUEST_FUNCTION_HOOK(sub_831CE888, XSetFilePointerEx);
GUEST_FUNCTION_HOOK(sub_831CDC58, XFindFirstFileA);
GUEST_FUNCTION_HOOK(sub_831CDC00, XFindNextFileA);
GUEST_FUNCTION_HOOK(sub_831CDF40, XReadFileEx);
GUEST_FUNCTION_HOOK(sub_831CD6E8, XGetFileAttributesA);
GUEST_FUNCTION_HOOK(sub_831CE3F8, XCreateFileA);
GUEST_FUNCTION_HOOK(sub_82BD4860, XWriteFile);
