// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <string>

namespace djv
{
    namespace models
    {
        //! \name Credentials
        //!
        //! The passwords DJV keeps, in the Windows Credential Manager: the
        //! user's password for the sync server, so that DJV signs in by
        //! itself when it starts, and the media share's account, so that
        //! Windows can map the share even when the sync server, which
        //! hands it out, cannot be reached. Windows encrypts them for the
        //! Windows user, and they are never written to DJV's settings.
        //!
        //! Elsewhere nothing is kept: DJV asks for the password again each
        //! time it runs.
        ///@{

        //! Whether passwords can be kept on this platform.
        DJV_MODELS_API bool isCredentialStoreSupported();

        //! The Credential Manager entry the sync password is kept under:
        //! "DJV sync/<server>/<user>".
        DJV_MODELS_API std::string syncCredentialTarget(
            const std::string& server,
            const std::string& username);

        //! Keep the password of a user of a sync server. Returns false, with
        //! the reason, when it could not be kept.
        DJV_MODELS_API bool saveSyncPassword(
            const std::string& server,
            const std::string& username,
            const std::string& password,
            std::string* error = nullptr);

        //! The password kept for a user of a sync server, or empty.
        DJV_MODELS_API std::string loadSyncPassword(
            const std::string& server,
            const std::string& username);

        //! Forget the password kept for a user of a sync server.
        DJV_MODELS_API void forgetSyncPassword(
            const std::string& server,
            const std::string& username);

        //! Keep the account a file server's shares are reached with, as
        //! "cmdkey /add:<host> /user:<user> /pass:<password>" would. Returns
        //! false, with the reason, when it could not be kept.
        DJV_MODELS_API bool saveShareCredential(
            const std::string& host,
            const std::string& user,
            const std::string& password,
            std::string* error = nullptr);

        ///@}
    }
}
