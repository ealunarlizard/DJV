// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/NetworkDrive.h>

#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <cctype>

// The Windows headers stay in this file: the macros they define would
// otherwise reach everything that includes the models.
#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif // NOMINMAX
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnetwk.h>
#endif // _WIN32

namespace djv
{
    namespace models
    {
        namespace
        {
            //! A UNC path for comparing: backslashes, no trailing one, upper
            //! case. Windows compares share names without regard to case.
            std::string uncKey(const std::string& value)
            {
                std::string out = value;
                std::replace(out.begin(), out.end(), '/', '\\');
                while (out.size() > 2 && '\\' == out.back())
                {
                    out.pop_back();
                }
                std::transform(
                    out.begin(),
                    out.end(),
                    out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                return out;
            }

#if defined(_WIN32)
            std::wstring localName(const std::string& letter)
            {
                return ftk::toWide(letter + ":");
            }

            bool isDriveInUse(const std::string& letter)
            {
                const DWORD drives = GetLogicalDrives();
                return drives & (1u << (letter[0] - 'A'));
            }

            std::string systemMessage(DWORD error)
            {
                wchar_t* buffer = nullptr;
                const DWORD size = FormatMessageW(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,
                    error,
                    0,
                    reinterpret_cast<wchar_t*>(&buffer),
                    0,
                    nullptr);
                std::string out;
                if (size > 0 && buffer)
                {
                    out = ftk::fromWide(std::wstring(buffer, size));
                    while (!out.empty() && (' ' == out.back() || '\r' == out.back() || '\n' == out.back() || '.' == out.back()))
                    {
                        out.pop_back();
                    }
                }
                if (buffer)
                {
                    LocalFree(buffer);
                }
                if (out.empty())
                {
                    out = ftk::Format("error {0}").arg(static_cast<int>(error));
                }
                return out;
            }
#endif // _WIN32
        }

        bool isNetworkDriveSupported()
        {
#if defined(_WIN32)
            return true;
#else // _WIN32
            return false;
#endif // _WIN32
        }

        std::string normalizeDriveLetter(const std::string& value)
        {
            std::string text = value;
            while (!text.empty() && ('\\' == text.back() || '/' == text.back()))
            {
                text.pop_back();
            }
            if (!text.empty() && ':' == text.back())
            {
                text.pop_back();
            }
            if (text.size() != 1 || !std::isalpha(static_cast<unsigned char>(text[0])))
            {
                return std::string();
            }
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(text[0]))));
        }

        bool isSameUncPath(const std::string& a, const std::string& b)
        {
            return !a.empty() && uncKey(a) == uncKey(b);
        }

        std::string getDriveConnection(const std::string& value)
        {
#if defined(_WIN32)
            const std::string letter = normalizeDriveLetter(value);
            if (letter.empty())
            {
                return std::string();
            }
            wchar_t remote[1024] = {};
            DWORD size = 1024;
            if (WNetGetConnectionW(localName(letter).c_str(), remote, &size) == NO_ERROR)
            {
                return ftk::fromWide(remote);
            }
#endif // _WIN32
            return std::string();
        }

        std::vector<std::string> getFreeDriveLetters()
        {
            std::vector<std::string> out;
#if defined(_WIN32)
            // A, B and C are the floppies and the system disk: never offered,
            // even when free.
            for (char c = 'D'; c <= 'Z'; ++c)
            {
                const std::string letter(1, c);
                if (!isDriveInUse(letter))
                {
                    out.push_back(letter);
                }
            }
#endif // _WIN32
            return out;
        }

        std::string getUncHost(const std::string& unc)
        {
            if (unc.size() < 3 || !(('\\' == unc[0] || '/' == unc[0]) && ('\\' == unc[1] || '/' == unc[1])))
            {
                return std::string();
            }
            const size_t end = unc.find_first_of("\\/", 2);
            return unc.substr(2, std::string::npos == end ? std::string::npos : end - 2);
        }

        std::string shareAccountName(const std::string& user, const std::string& unc)
        {
            if (user.empty() || user.find('\\') != std::string::npos || user.find('@') != std::string::npos)
            {
                return user;
            }
            const std::string host = getUncHost(unc);
            return host.empty() ? user : host + "\\" + user;
        }

        NetworkDriveResult mapNetworkDrive(
            const std::string& value,
            const std::string& unc,
            const std::string& user,
            const std::string& password)
        {
            NetworkDriveResult out;
#if defined(_WIN32)
            const std::string letter = normalizeDriveLetter(value);
            if (letter.empty())
            {
                out.message = ftk::Format("\"{0}\" is not a drive letter.").arg(value);
                return out;
            }
            if (unc.size() < 3 || !(('\\' == unc[0] || '/' == unc[0]) && ('\\' == unc[1] || '/' == unc[1])))
            {
                out.message = ftk::Format("\"{0}\" is not a network share (\\\\server\\share).").arg(unc);
                return out;
            }

            // Mapped already, by an earlier run of DJV in this logon or by
            // hand: the same share is what was wanted; another is someone
            // else's, and is not taken over.
            const std::string current = getDriveConnection(letter);
            if (!current.empty())
            {
                if (isSameUncPath(current, unc))
                {
                    out.ok = true;
                    out.alreadyMapped = true;
                }
                else
                {
                    out.message = ftk::Format(
                        "{0}: is already mapped to {1}. Choose another drive letter in the "
                        "Sync Server settings, or disconnect {2}: in Windows.").
                        arg(letter).
                        arg(current).
                        arg(letter);
                }
                return out;
            }
            if (isDriveInUse(letter))
            {
                out.message = ftk::Format(
                    "{0}: is used by a disk on this PC. Choose another drive letter in the "
                    "Sync Server settings.").arg(letter);
                return out;
            }

            std::wstring local = localName(letter);
            std::string uncBack = unc;
            std::replace(uncBack.begin(), uncBack.end(), '/', '\\');
            while (uncBack.size() > 2 && '\\' == uncBack.back())
            {
                uncBack.pop_back();
            }
            std::wstring remote = ftk::toWide(uncBack);
            NETRESOURCEW resource = {};
            resource.dwType = RESOURCETYPE_DISK;
            resource.lpLocalName = local.data();
            resource.lpRemoteName = remote.data();
            // The account the server gave, or, without one, whatever Windows
            // has for the server: the user's own logon, or a credential
            // saved for it.
            const std::string account = shareAccountName(user, uncBack);
            std::wstring waccount = ftk::toWide(account);
            std::wstring wpassword = ftk::toWide(password);
            // No CONNECT_UPDATE_PROFILE: the mapping lasts for this logon and
            // is not restored by Windows, since DJV maps it when it starts.
            const DWORD result = WNetAddConnection2W(
                &resource,
                account.empty() ? nullptr : wpassword.c_str(),
                account.empty() ? nullptr : waccount.c_str(),
                0);
            switch (result)
            {
            case NO_ERROR:
                out.ok = true;
                break;
            case ERROR_ACCESS_DENIED:
            case ERROR_LOGON_FAILURE:
            case ERROR_INVALID_PASSWORD:
                out.message = account.empty() ?
                    std::string(ftk::Format(
                        "Windows was not let in to {0}: {1}. Sign in to the sync server, which "
                        "gives DJV the share's account, or ask the admin for read access to the "
                        "share.").
                        arg(uncBack).
                        arg(systemMessage(result))) :
                    std::string(ftk::Format(
                        "Windows was not let in to {0} as {1}: {2}. Ask the admin to check the "
                        "share's account and password (see share-setup.md).").
                        arg(uncBack).
                        arg(account).
                        arg(systemMessage(result)));
                break;
            case ERROR_SESSION_CREDENTIAL_CONFLICT:
                out.message = ftk::Format(
                    "This PC is already connected to the server of {0} under another user name, "
                    "and Windows allows only one at a time: {1}. Disconnect the other drives or "
                    "folders open on that server (\"net use\" lists them), or sign out of "
                    "Windows and back in, then map the drive again.").
                    arg(uncBack).
                    arg(systemMessage(result));
                break;
            case ERROR_BAD_NETPATH:
            case ERROR_BAD_NET_NAME:
            case ERROR_NO_NET_OR_BAD_PATH:
            case ERROR_NO_NETWORK:
                out.message = ftk::Format(
                    "Cannot reach {0}: {1}. Check that the dev server is on and that the share "
                    "exists.").
                    arg(uncBack).
                    arg(systemMessage(result));
                break;
            default:
                out.message = ftk::Format("Cannot map {0}: to {1}: {2}.").
                    arg(letter).
                    arg(uncBack).
                    arg(systemMessage(result));
                break;
            }
#else // _WIN32
            out.message = "Drive letters are a Windows feature; set the network media folder instead.";
#endif // _WIN32
            return out;
        }

        std::string toUncPath(const std::string& path)
        {
            if (path.size() < 2 || ':' != path[1])
            {
                return path;
            }
            const std::string unc = getDriveConnection(path.substr(0, 1));
            if (unc.empty())
            {
                return path;
            }
            std::string rest = path.substr(2);
            std::replace(rest.begin(), rest.end(), '/', '\\');
            std::string out = unc;
            while (out.size() > 2 && '\\' == out.back())
            {
                out.pop_back();
            }
            if (!rest.empty() && rest.front() != '\\')
            {
                out += '\\';
            }
            return out + rest;
        }
    }
}
