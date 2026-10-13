// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/DeepLink.h>

#include <djv/Models/ShellCommand.h>
#include <djv/Models/SyncModel.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>
#include <ftk/Core/Timer.h>

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
#include <sddl.h>
#endif // _WIN32

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace djv
{
    namespace models
    {
        namespace
        {
            std::string lower(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            int hexDigit(char c)
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            }

            //! Undo a URL's percent encoding. A "%" not followed by two hex
            //! digits is kept as it is.
            std::string decode(const std::string& value)
            {
                std::string out;
                out.reserve(value.size());
                for (size_t i = 0; i < value.size(); ++i)
                {
                    if ('%' == value[i] && i + 2 < value.size())
                    {
                        const int hi = hexDigit(value[i + 1]);
                        const int lo = hexDigit(value[i + 2]);
                        if (hi >= 0 && lo >= 0)
                        {
                            out.push_back(static_cast<char>(hi * 16 + lo));
                            i += 2;
                            continue;
                        }
                    }
                    out.push_back(value[i]);
                }
                return out;
            }

            //! A server address in one spelling: the scheme and host in
            //! lower case, the default port written out, no trailing slash.
            std::string normalServer(const std::string& value)
            {
                std::string url = value;
                const char* space = " \t\r\n";
                url.erase(0, url.find_first_not_of(space));
                url.erase(url.find_last_not_of(space) + 1);
                while (!url.empty() && '/' == url.back())
                {
                    url.pop_back();
                }
                const size_t schemeEnd = url.find("://");
                if (std::string::npos == schemeEnd)
                {
                    return lower(url);
                }
                const std::string scheme = lower(url.substr(0, schemeEnd));
                const size_t hostStart = schemeEnd + 3;
                const size_t pathStart = url.find('/', hostStart);
                std::string host = lower(url.substr(
                    hostStart,
                    std::string::npos == pathStart ? std::string::npos : pathStart - hostStart));
                const std::string path = std::string::npos == pathStart ? std::string() : url.substr(pathStart);
                // A port after the last colon, unless the colon is inside an
                // IPv6 address's brackets.
                const size_t colon = host.rfind(':');
                const size_t bracket = host.rfind(']');
                const bool hasPort = colon != std::string::npos &&
                    (std::string::npos == bracket || colon > bracket);
                if (!hasPort)
                {
                    host += "http" == scheme ? ":80" : "https" == scheme ? ":443" : "";
                }
                return scheme + "://" + host + path;
            }
        }

        bool isDeepLink(const std::string& value)
        {
            return value.size() >= 4 && "djv:" == lower(value.substr(0, 4));
        }

        std::optional<DeepLink> parseDeepLink(const std::string& link, std::string* error)
        {
            auto fail = [error](const std::string& message)
            {
                if (error)
                {
                    *error = message;
                }
                return std::optional<DeepLink>();
            };
            if (!isDeepLink(link))
            {
                return fail(ftk::Format("\"{0}\" is not a djv: link.").arg(link));
            }

            // djv://session/<name>?server=<address>. Browsers and Windows
            // may add or drop the slashes, and a trailing one.
            std::string rest = link.substr(4);
            const size_t hash = rest.find('#');
            if (hash != std::string::npos)
            {
                rest.resize(hash);
            }
            std::string query;
            const size_t question = rest.find('?');
            if (question != std::string::npos)
            {
                query = rest.substr(question + 1);
                rest.resize(question);
            }
            std::vector<std::string> parts;
            for (const auto& part : ftk::split(rest, '/'))
            {
                if (!part.empty())
                {
                    parts.push_back(part);
                }
            }
            if (parts.size() != 2 || lower(parts[0]) != "session")
            {
                return fail(ftk::Format(
                    "\"{0}\" is not a review link: those are djv://session/<session>?server=<address>.").
                    arg(link));
            }

            DeepLink out;
            out.session = decode(parts[1]);
            if (!isValidSyncSession(out.session))
            {
                return fail(ftk::Format("The review link's session \"{0}\" is not a session name.").
                    arg(out.session));
            }
            std::optional<std::string> autoConnect;
            for (const auto& pair : ftk::split(query, '&'))
            {
                const size_t equals = pair.find('=');
                const std::string key = lower(decode(pair.substr(0, equals)));
                const std::string value = std::string::npos == equals ?
                    std::string() :
                    decode(pair.substr(equals + 1));
                if ("server" == key)
                {
                    out.server = value;
                }
                else if ("autoconnect" == key)
                {
                    autoConnect = value;
                }
            }
            if (autoConnect.has_value())
            {
                const std::string value = lower(autoConnect.value());
                if ("true" == value || "1" == value || "yes" == value || "on" == value)
                {
                    out.autoConnect = true;
                }
                else if ("false" == value || "0" == value || "no" == value || "off" == value)
                {
                    out.autoConnect = false;
                }
                else
                {
                    // Rather than guess: joining when the person meant only
                    // to look, or the other way round, is worse than a
                    // message saying why nothing happened.
                    return fail(ftk::Format("The review link's autoConnect \"{0}\" is neither true nor false.").
                        arg(autoConnect.value()));
                }
            }
            if (out.server.empty())
            {
                return fail("The review link does not say which sync server the session is on.");
            }
            if (0 != lower(out.server).compare(0, 7, "http://"))
            {
                return fail(ftk::Format("The review link's server \"{0}\" is not an http:// address.").
                    arg(out.server));
            }
            return out;
        }

        bool isSameSyncServer(const std::string& a, const std::string& b)
        {
            return normalServer(a) == normalServer(b);
        }

#if defined(_WIN32)
        namespace
        {
            //! The Windows user's security identifier, as text, or empty.
            std::wstring userSid()
            {
                std::wstring out;
                HANDLE token = nullptr;
                if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                {
                    return out;
                }
                DWORD size = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &size);
                std::vector<BYTE> buffer(size);
                if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
                {
                    LPWSTR text = nullptr;
                    if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text))
                    {
                        out = text;
                        LocalFree(text);
                    }
                }
                CloseHandle(token);
                return out;
            }

            const DWORD maxLink = 64 * 1024;
        }

        std::string deepLinkPipeName()
        {
            std::wstring id = userSid();
            if (id.empty())
            {
                wchar_t name[256] = {};
                DWORD size = 256;
                if (GetUserNameW(name, &size))
                {
                    id = name;
                }
            }
            return "\\\\.\\pipe\\djv-link-" + ftk::fromWide(id);
        }

        bool sendDeepLink(const std::string& link, const std::string& pipeName)
        {
            const std::wstring name = ftk::toWide(pipeName.empty() ? deepLinkPipeName() : pipeName);
            HANDLE pipe = INVALID_HANDLE_VALUE;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY)
                {
                    break;
                }
                // Another link on its way in; there is a turn after it.
                WaitNamedPipeW(name.c_str(), 2000);
            }
            if (INVALID_HANDLE_VALUE == pipe)
            {
                return false;
            }
            // This process was started by the browser the link was clicked
            // in, so it may bring a window to the front; it lets the DJV
            // that takes the link do so in its place.
            AllowSetForegroundWindow(ASFW_ANY);
            DWORD written = 0;
            BOOL ok = WriteFile(
                pipe,
                link.data(),
                static_cast<DWORD>(std::min<size_t>(link.size(), maxLink)),
                &written,
                nullptr);
            // Until the other DJV has read it: closing first could leave it
            // nothing to read.
            ok = ok && FlushFileBuffers(pipe);
            CloseHandle(pipe);
            return ok && written == link.size();

        }

        std::filesystem::path getDeepLinkHandler()
        {
            std::filesystem::path out;
            DWORD size = 0;
            const wchar_t* key = L"djv\\shell\\open\\command";
            if (RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
            {
                return out;
            }
            std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
            size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
            if (RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS)
            {
                return out;
            }
            if (const auto command = parseShellCommand(ftk::fromWide(buffer.data())))
            {
                out = ftk::toFileSystem(command->program);
            }
            return out;
        }

        bool registerDeepLinkHandler(
            const std::filesystem::path& executable,
            bool force,
            std::string* error)
        {
            if (!force)
            {
                const std::filesystem::path current = getDeepLinkHandler();
                std::error_code ec;
                if (!current.empty() && std::filesystem::exists(current, ec))
                {
                    return false;
                }
            }

            // In the user's own classes, which need no administrator, and
            // win over the installer's for this user.
            const std::wstring exe = executable.wstring();
            const std::wstring base = L"Software\\Classes\\djv";
            auto set = [&error](const std::wstring& path, const wchar_t* name, const std::wstring& value)
            {
                HKEY key = nullptr;
                LSTATUS status = RegCreateKeyExW(
                    HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
                if (ERROR_SUCCESS == status)
                {
                    status = RegSetValueExW(
                        key,
                        name,
                        0,
                        REG_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
                    RegCloseKey(key);
                }
                if (status != ERROR_SUCCESS && error)
                {
                    *error = ftk::Format("Cannot write HKEY_CURRENT_USER\\{0} (error {1}).").
                        arg(ftk::fromWide(path)).
                        arg(static_cast<int>(status));
                }
                return ERROR_SUCCESS == status;
            };
            return
                set(base, nullptr, L"URL:DJV review link") &&
                set(base, L"URL Protocol", L"") &&
                set(base + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0") &&
                set(base + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
        }
#else // _WIN32
        std::string deepLinkPipeName()
        {
            return std::string();
        }

        bool sendDeepLink(const std::string&, const std::string&)
        {
            return false;
        }

        std::filesystem::path getDeepLinkHandler()
        {
            return std::filesystem::path();
        }

        bool registerDeepLinkHandler(const std::filesystem::path&, bool, std::string* error)
        {
            if (error)
            {
                *error = "Review links are only registered on Windows.";
            }
            return false;
        }
#endif // _WIN32

        struct DeepLinkListener::Private
        {
            std::string pipeName;
            std::function<void(const std::string&)> callback;
            std::shared_ptr<ftk::Timer> timer;

            std::mutex mutex;
            std::deque<std::string> links;
            std::atomic<bool> running = false;
            bool listening = false;
            std::thread thread;
#if defined(_WIN32)
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            HANDLE firstPipe = INVALID_HANDLE_VALUE;
#endif // _WIN32
        };

#if defined(_WIN32)
        namespace
        {
            HANDLE createPipe(const std::wstring& name, PSECURITY_DESCRIPTOR descriptor, bool first)
            {
                SECURITY_ATTRIBUTES attributes = {};
                attributes.nLength = sizeof(attributes);
                attributes.lpSecurityDescriptor = descriptor;
                return CreateNamedPipeW(
                    name.c_str(),
                    PIPE_ACCESS_INBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    PIPE_UNLIMITED_INSTANCES,
                    0,
                    maxLink,
                    0,
                    descriptor ? &attributes : nullptr);
            }
        }
#endif // _WIN32

        void DeepLinkListener::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::string& pipeName)
        {
            FTK_P();
            p.pipeName = pipeName.empty() ? deepLinkPipeName() : pipeName;
#if defined(_WIN32)
            // Only this Windows user may hand this DJV a link: another
            // user's program could otherwise point it at a session.
            const std::wstring sid = userSid();
            if (!sid.empty())
            {
                const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")(A;;GA;;;SY)";
                ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl.c_str(), SDDL_REVISION_1, &p.descriptor, nullptr);
            }
            // The first instance of the pipe decides who listens: a DJV that
            // cannot make it is not the first one running.
            const std::wstring name = ftk::toWide(p.pipeName);
            p.firstPipe = createPipe(name, p.descriptor, true);
            if (INVALID_HANDLE_VALUE == p.firstPipe)
            {
                return;
            }
            p.listening = true;
            p.running = true;
            p.thread = std::thread(
                [this, name]
                {
                    FTK_P();
                    HANDLE pipe = p.firstPipe;
                    while (p.running && pipe != INVALID_HANDLE_VALUE)
                    {
                        // A DJV that connected before this thread came to
                        // wait is connected all the same; one that has
                        // already let go may still have left its link.
                        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ?
                            TRUE :
                            (GetLastError() == ERROR_PIPE_CONNECTED || GetLastError() == ERROR_NO_DATA);
                        // The next instance before this one goes, so that
                        // the pipe is never missing for the moment between:
                        // a DJV started then would think none was running.
                        const HANDLE next = p.running ?
                            createPipe(name, p.descriptor, false) :
                            INVALID_HANDLE_VALUE;
                        if (connected && p.running)
                        {
                            std::string link;
                            std::vector<char> buffer(4096);
                            while (link.size() < maxLink)
                            {
                                DWORD read = 0;
                                const BOOL ok = ReadFile(
                                    pipe,
                                    buffer.data(),
                                    static_cast<DWORD>(buffer.size()),
                                    &read,
                                    nullptr);
                                link.append(buffer.data(), read);
                                if (ok || GetLastError() != ERROR_MORE_DATA)
                                {
                                    break;
                                }
                            }
                            if (!link.empty() && p.running)
                            {
                                std::unique_lock<std::mutex> lock(p.mutex);
                                p.links.push_back(link);
                            }
                        }
                        DisconnectNamedPipe(pipe);
                        CloseHandle(pipe);
                        pipe = next;
                    }
                    if (pipe != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(pipe);
                    }
                });
#endif // _WIN32

            p.timer = ftk::Timer::create(context);
            p.timer->setRepeating(true);
            p.timer->start(
                std::chrono::milliseconds(100),
                [this]
                {
                    FTK_P();
                    std::deque<std::string> links;
                    {
                        std::unique_lock<std::mutex> lock(p.mutex);
                        links.swap(p.links);
                    }
                    for (const auto& link : links)
                    {
                        if (p.callback)
                        {
                            p.callback(link);
                        }
                    }
                });
        }

        DeepLinkListener::DeepLinkListener() :
            _p(new Private)
        {}

        DeepLinkListener::~DeepLinkListener()
        {
            FTK_P();
            if (p.timer)
            {
                p.timer->stop();
            }
#if defined(_WIN32)
            if (p.thread.joinable())
            {
                // The thread waits for someone to connect: be that someone.
                p.running = false;
                const std::wstring name = ftk::toWide(p.pipeName);
                for (int attempt = 0; attempt < 10 && p.thread.joinable(); ++attempt)
                {
                    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (pipe != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(pipe);
                        break;
                    }
                    if (GetLastError() == ERROR_PIPE_BUSY)
                    {
                        WaitNamedPipeW(name.c_str(), 100);
                    }
                    else
                    {
                        Sleep(10);
                    }
                }
                p.thread.join();
            }
            if (p.descriptor)
            {
                LocalFree(p.descriptor);
            }
#endif // _WIN32
        }

        std::shared_ptr<DeepLinkListener> DeepLinkListener::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::string& pipeName)
        {
            auto out = std::shared_ptr<DeepLinkListener>(new DeepLinkListener);
            out->_init(context, pipeName);
            return out;
        }

        bool DeepLinkListener::isListening() const
        {
            return _p->listening;
        }

        void DeepLinkListener::setCallback(const std::function<void(const std::string&)>& value)
        {
            _p->callback = value;
        }
    }
}
