// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <ftk/Core/Util.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ftk
{
    class Context;
}

namespace djv
{
    namespace models
    {
        //! \name Review links
        //!
        //! The sync server's portal lists the review sessions, each with a
        //! "review" link: djv://session/<session>?server=<server>. Windows
        //! starts DJV with the link as its only argument, because DJV
        //! registers itself for djv: links. A DJV already running takes the
        //! link from the new one, which then exits, so that one click does
        //! not mean one more window.
        ///@{

        //! A review link, taken apart.
        struct DJV_MODELS_API_TYPE DeepLink
        {
            //! The sync session to join.
            std::string session;

            //! The sync server the portal that made the link is on.
            std::string server;

            bool operator == (const DeepLink&) const = default;
        };

        //! Whether the text is a djv: link rather than a file.
        DJV_MODELS_API bool isDeepLink(const std::string&);

        //! Take a review link apart. Returns nothing, with the reason, for
        //! a link that is not one: another kind, a session name that cannot
        //! be one, or a server that is not an http:// address.
        DJV_MODELS_API std::optional<DeepLink> parseDeepLink(
            const std::string&,
            std::string* error = nullptr);

        //! Whether two sync server addresses are the same server: the case
        //! of the scheme and host, a trailing slash and the default port do
        //! not count.
        DJV_MODELS_API bool isSameSyncServer(const std::string&, const std::string&);

        //! The name of the pipe the running DJV takes links on, the same for
        //! every DJV of this Windows user and different for every other one.
        DJV_MODELS_API std::string deepLinkPipeName();

        //! Hand a link to the DJV running for this Windows user, letting it
        //! come to the front. Returns false when none is running, or on
        //! platforms other than Windows.
        DJV_MODELS_API bool sendDeepLink(
            const std::string& link,
            const std::string& pipeName = std::string());

        //! The program Windows starts for djv: links, or empty when none is
        //! registered.
        DJV_MODELS_API std::filesystem::path getDeepLinkHandler();

        //! Register the executable for djv: links, for this Windows user.
        //! Unless forced, only when no program is registered or the one that
        //! is no longer exists: an installed DJV, or another copy someone
        //! chose, is left as it is. Returns whether the registration was
        //! written, with the reason when it could not be.
        DJV_MODELS_API bool registerDeepLinkHandler(
            const std::filesystem::path& executable,
            bool force = false,
            std::string* error = nullptr);

        //! Takes the links other DJVs hand on (sendDeepLink()), on a thread
        //! of its own, and gives them to the callback on the main thread.
        //! Only the first DJV of a Windows user listens; for the others,
        //! and on other platforms, it does nothing.
        class DJV_MODELS_API_TYPE DeepLinkListener : public std::enable_shared_from_this<DeepLinkListener>
        {
            FTK_NON_COPYABLE(DeepLinkListener);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::string& pipeName);

            DeepLinkListener();

        public:
            DJV_MODELS_API ~DeepLinkListener();

            //! Create a new listener, on the pipe given or the user's.
            DJV_MODELS_API static std::shared_ptr<DeepLinkListener> create(
                const std::shared_ptr<ftk::Context>&,
                const std::string& pipeName = std::string());

            //! Whether this one is listening.
            DJV_MODELS_API bool isListening() const;

            //! Set what is done with each link that arrives.
            DJV_MODELS_API void setCallback(const std::function<void(const std::string&)>&);

        private:
            FTK_PRIVATE();
        };

        ///@}
    }
}
