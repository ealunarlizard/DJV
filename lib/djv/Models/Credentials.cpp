// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/Credentials.h>

#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>

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
#include <wincred.h>
#endif // _WIN32

#include <vector>

namespace djv
{
    namespace models
    {
        namespace
        {
#if defined(_WIN32)
            std::string lastError()
            {
                const DWORD code = GetLastError();
                wchar_t* buffer = nullptr;
                const DWORD size = FormatMessageW(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,
                    code,
                    0,
                    reinterpret_cast<LPWSTR>(&buffer),
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
                return out.empty() ? std::string(ftk::Format("error {0}").arg(code)) : out;
            }

            bool write(
                DWORD type,
                const std::string& target,
                const std::string& user,
                const std::vector<BYTE>& blob,
                std::string* error)
            {
                std::wstring wtarget = ftk::toWide(target);
                std::wstring wuser = ftk::toWide(user);
                std::wstring comment = L"DJV";
                CREDENTIALW credential = {};
                credential.Type = type;
                credential.TargetName = wtarget.data();
                credential.Comment = comment.data();
                credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
                credential.CredentialBlob = const_cast<LPBYTE>(blob.data());
                // Kept across logons, for this Windows user on this PC only.
                credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
                credential.UserName = wuser.data();
                if (!CredWriteW(&credential, 0))
                {
                    if (error)
                    {
                        *error = lastError();
                    }
                    return false;
                }
                return true;
            }
#endif // _WIN32
        }

        bool isCredentialStoreSupported()
        {
#if defined(_WIN32)
            return true;
#else // _WIN32
            return false;
#endif // _WIN32
        }

        std::string syncCredentialTarget(const std::string& server, const std::string& username)
        {
            std::string host = server;
            while (!host.empty() && '/' == host.back())
            {
                host.pop_back();
            }
            return ftk::Format("DJV sync/{0}/{1}").arg(host).arg(username);
        }

        bool saveSyncPassword(
            const std::string& server,
            const std::string& username,
            const std::string& password,
            std::string* error)
        {
#if defined(_WIN32)
            // A generic credential: only DJV reads it back, so its bytes are
            // the password as DJV holds it, in UTF-8.
            return write(
                CRED_TYPE_GENERIC,
                syncCredentialTarget(server, username),
                username,
                std::vector<BYTE>(password.begin(), password.end()),
                error);
#else // _WIN32
            if (error)
            {
                *error = "Passwords are only kept on Windows.";
            }
            return false;
#endif // _WIN32
        }

        std::string loadSyncPassword(const std::string& server, const std::string& username)
        {
            std::string out;
#if defined(_WIN32)
            const std::wstring target = ftk::toWide(syncCredentialTarget(server, username));
            PCREDENTIALW credential = nullptr;
            if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) && credential)
            {
                if (credential->CredentialBlob && credential->CredentialBlobSize > 0)
                {
                    out.assign(
                        reinterpret_cast<const char*>(credential->CredentialBlob),
                        credential->CredentialBlobSize);
                }
                CredFree(credential);
            }
#endif // _WIN32
            return out;
        }

        void forgetSyncPassword(const std::string& server, const std::string& username)
        {
#if defined(_WIN32)
            const std::wstring target = ftk::toWide(syncCredentialTarget(server, username));
            CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#endif // _WIN32
        }

        bool saveShareCredential(
            const std::string& host,
            const std::string& user,
            const std::string& password,
            std::string* error)
        {
#if defined(_WIN32)
            // A domain credential, which is what Windows itself looks up
            // when it connects to the host: its password is UTF-16, with no
            // terminating zero.
            const std::wstring wpassword = ftk::toWide(password);
            const BYTE* bytes = reinterpret_cast<const BYTE*>(wpassword.data());
            return write(
                CRED_TYPE_DOMAIN_PASSWORD,
                host,
                user,
                std::vector<BYTE>(bytes, bytes + wpassword.size() * sizeof(wchar_t)),
                error);
#else // _WIN32
            if (error)
            {
                *error = "Share credentials are only kept on Windows.";
            }
            return false;
#endif // _WIN32
        }
    }
}
