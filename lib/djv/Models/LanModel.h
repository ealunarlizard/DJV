// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/SyncModel.h>

#include <ftk/Core/Observable.h>
#include <ftk/Core/Util.h>

#include <functional>

namespace ftk
{
    class Context;
}

namespace djv
{
    namespace models
    {
        class SettingsModel;
        struct SyncSettings;

        //! Whether DJV is signed in to the sync server.
        enum class LanAuthState
        {
            SignedOut,
            SigningIn,
            SignedIn,
            //! The server refused the user name or the password.
            Failed
        };

        //! Who DJV is signed in to the sync server as.
        struct DJV_MODELS_API_TYPE LanAuth
        {
            LanAuthState state = LanAuthState::SignedOut;
            std::string username;
            std::string userFullName;

            //! The session token, sent with every request (see
            //! SyncModel::connect()). Held in memory only.
            std::string token;

            //! What went wrong, or why DJV is not signed in.
            std::string message;

            bool operator == (const LanAuth&) const = default;
        };

        //! The state of the drive the studio's media share is mapped to.
        enum class LanDriveState
        {
            //! No drive letter is set.
            None,
            //! Drive letters are a Windows feature.
            Unsupported,
            Mapping,
            Mapped,
            Error
        };

        //! The drive the studio's media share is mapped to.
        struct DJV_MODELS_API_TYPE LanDriveStatus
        {
            LanDriveState state = LanDriveState::None;
            std::string letter;
            std::string unc;
            std::string message;

            bool operator == (const LanDriveStatus&) const = default;
        };

        //! DJV's place on a studio network: who is signed in to the sync
        //! server, what the server says, and the drive the media share is
        //! mapped to (see docs/lan-setup.html).
        //!
        //! Signing in, asking the server and mapping the drive can each take
        //! seconds, so they run on threads of their own; what they find is
        //! taken up on the main thread, from a timer. The password is kept
        //! in the Windows Credential Manager, so that DJV signs in by itself
        //! when it starts. The share the server names is kept in the
        //! settings, and its account in the Credential Manager, so that the
        //! drive is mapped even when the server cannot be reached.
        class DJV_MODELS_API_TYPE LanModel : public std::enable_shared_from_this<LanModel>
        {
            FTK_NON_COPYABLE(LanModel);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<ftk::IObservable<SyncSettings> >&,
                const std::function<void(const SyncSettings&)>& setSettings,
                const std::string& version);

            LanModel();

        public:
            DJV_MODELS_API ~LanModel();

            //! Create a new model on the settings. The version is this DJV's,
            //! sent to the server and compared with the one the studio runs.
            DJV_MODELS_API static std::shared_ptr<LanModel> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<SettingsModel>&,
                const std::string& version);

            //! Create a new model on sync settings held elsewhere, e.g. by a
            //! test: the model reads them, and writes back the user who
            //! signed in and the share the server names.
            DJV_MODELS_API static std::shared_ptr<LanModel> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<ftk::IObservable<SyncSettings> >&,
                const std::function<void(const SyncSettings&)>& setSettings,
                const std::string& version);

            //! Get which DJV this is, for the server.
            DJV_MODELS_API SyncIdentity getIdentity() const;

            //! Observe who DJV is signed in as.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<LanAuth> > observeAuth() const;

            //! Observe what the server last said.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<SyncServerInfo> > observeServerInfo() const;

            //! Observe the media drive.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<LanDriveStatus> > observeDrive() const;

            //! Observe the folder the session media is found in: the mapped
            //! drive when there is one, otherwise the media folder of the
            //! settings. Empty when neither.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<std::string> > observeMediaRoot() const;

            //! Sign in with the password kept for the user of the settings,
            //! ask the server about itself, then map the drive to the share
            //! it names, with the account it gives; or to the share last
            //! named, when it cannot be reached.
            DJV_MODELS_API void start();

            //! Sign in as a user. On success the user is kept in the
            //! settings and the password in the Credential Manager, and the
            //! drive is mapped as start() does.
            DJV_MODELS_API void signIn(const std::string& username, const std::string& password);

            //! Sign out, and forget the password kept for the user. The
            //! drive stays mapped.
            DJV_MODELS_API void signOut();

            //! Map the drive to the share last named, without asking the
            //! server.
            DJV_MODELS_API void mapDrive();

        private:
            void _begin(const std::string& password, bool save);
            void _mapDrive(const std::string& unc, const std::string& user, const std::string& password);
            void _tick();
            void _driveUpdate();
            void _mediaRootUpdate();

            FTK_PRIVATE();
        };
    }
}
