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

        //! DJV's place on a studio network: who this PC is on the sync
        //! server, what the server says about it, and the drive the media
        //! share is mapped to (see docs/lan-setup.html).
        //!
        //! Asking the server and mapping the drive can each take seconds, so
        //! both run on threads of their own; what they find is taken up on
        //! the main thread, from a timer. The share the server names is kept
        //! in the settings, so that the drive is mapped even when the server
        //! cannot be reached.
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
            //! test: the model reads them, and writes back the share the
            //! server names.
            DJV_MODELS_API static std::shared_ptr<LanModel> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<ftk::IObservable<SyncSettings> >&,
                const std::function<void(const SyncSettings&)>& setSettings,
                const std::string& version);

            //! Get who this PC is on the sync server.
            DJV_MODELS_API SyncIdentity getIdentity() const;

            //! Observe what the server last said.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<SyncServerInfo> > observeServerInfo() const;

            //! Observe the media drive.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<LanDriveStatus> > observeDrive() const;

            //! Observe the folder the session media is found in: the mapped
            //! drive when there is one, otherwise the media folder of the
            //! settings. Empty when neither.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<std::string> > observeMediaRoot() const;

            //! Ask the server about itself and this PC, then map the drive
            //! to the share it names, or to the one last named when it
            //! cannot be reached.
            DJV_MODELS_API void start();

            //! Map the drive to the share last named, without asking the
            //! server.
            DJV_MODELS_API void mapDrive();

        private:
            void _mapDrive(const std::string& unc);
            void _tick();
            void _driveUpdate();
            void _mediaRootUpdate();

            FTK_PRIVATE();
        };
    }
}
