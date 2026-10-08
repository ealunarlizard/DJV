// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/LanModel.h>

#include <djv/Models/NetworkDrive.h>
#include <djv/Models/SettingsModel.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/LogSystem.h>
#include <ftk/Core/Timer.h>

#include <mutex>
#include <optional>
#include <thread>

namespace djv
{
    namespace models
    {
        namespace
        {
            const std::chrono::milliseconds tickTimeout(100);

            //! What the threads hand to the main thread. Held by the threads
            //! as well as the model, so a thread still waiting on the network
            //! when DJV quits has somewhere to put its answer.
            struct LanShared
            {
                std::mutex mutex;
                std::optional<SyncServerInfo> info;
                std::optional<NetworkDriveResult> drive;
            };
        }

        struct LanModel::Private
        {
            std::weak_ptr<ftk::Context> context;
            std::shared_ptr<ftk::IObservable<SyncSettings> > settings;
            std::function<void(const SyncSettings&)> setSettings;
            std::string version;

            std::shared_ptr<ftk::Observable<SyncServerInfo> > serverInfo;
            std::shared_ptr<ftk::Observable<LanDriveStatus> > drive;
            std::shared_ptr<ftk::Observable<std::string> > mediaRoot;

            std::shared_ptr<LanShared> shared;
            bool helloPending = false;
            bool mapPending = false;
            std::shared_ptr<ftk::Timer> timer;
            std::shared_ptr<ftk::Observer<SyncSettings> > settingsObserver;

            void log(const std::string& text, ftk::LogType type = ftk::LogType::Message)
            {
                if (auto c = context.lock())
                {
                    c->getLogSystem()->print("djv::models::LanModel", text, type);
                }
            }
        };

        void LanModel::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<ftk::IObservable<SyncSettings> >& settings,
            const std::function<void(const SyncSettings&)>& setSettings,
            const std::string& version)
        {
            FTK_P();
            p.context = context;
            p.settings = settings;
            p.setSettings = setSettings;
            p.version = version;
            p.serverInfo = ftk::Observable<SyncServerInfo>::create();
            p.drive = ftk::Observable<LanDriveStatus>::create();
            p.mediaRoot = ftk::Observable<std::string>::create();
            p.shared = std::make_shared<LanShared>();
            p.timer = ftk::Timer::create(context);
            p.timer->setRepeating(true);

            p.settingsObserver = ftk::Observer<SyncSettings>::create(
                settings,
                [this](const SyncSettings&)
                {
                    _driveUpdate();
                    _mediaRootUpdate();
                });
        }

        LanModel::LanModel() :
            _p(new Private)
        {}

        LanModel::~LanModel()
        {}

        std::shared_ptr<LanModel> LanModel::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<ftk::IObservable<SyncSettings> >& settings,
            const std::function<void(const SyncSettings&)>& setSettings,
            const std::string& version)
        {
            auto out = std::shared_ptr<LanModel>(new LanModel);
            out->_init(context, settings, setSettings, version);
            return out;
        }

        std::shared_ptr<LanModel> LanModel::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<SettingsModel>& settingsModel,
            const std::string& version)
        {
            std::weak_ptr<SettingsModel> weak = settingsModel;
            return create(
                context,
                settingsModel->observeSync(),
                [weak](const SyncSettings& value)
                {
                    if (auto settingsModel = weak.lock())
                    {
                        settingsModel->setSync(value);
                    }
                },
                version);
        }

        SyncIdentity LanModel::getIdentity() const
        {
            FTK_P();
            SyncIdentity out;
            out.clientId = p.settings->get().clientId;
            out.version = p.version;
            return out;
        }

        std::shared_ptr<ftk::IObservable<SyncServerInfo> > LanModel::observeServerInfo() const
        {
            return _p->serverInfo;
        }

        std::shared_ptr<ftk::IObservable<LanDriveStatus> > LanModel::observeDrive() const
        {
            return _p->drive;
        }

        std::shared_ptr<ftk::IObservable<std::string> > LanModel::observeMediaRoot() const
        {
            return _p->mediaRoot;
        }

        void LanModel::start()
        {
            FTK_P();
            if (p.helloPending)
            {
                return;
            }
            const SyncSettings& settings = p.settings->get();
            SyncServerInfo checking;
            checking.state = SyncServerState::Checking;
            checking.server = settings.server;
            checking.clientId = settings.clientId;
            p.serverInfo->setIfChanged(checking);

            p.helloPending = true;
            std::thread(
                [shared = p.shared, server = settings.server, token = settings.token, identity = getIdentity()]
                {
                    const SyncServerInfo info = fetchSyncServerInfo(server, token, identity);
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->info = info;
                }).detach();
            p.timer->start(tickTimeout, [this] { _tick(); });
        }

        void LanModel::mapDrive()
        {
            _mapDrive(_p->settings->get().shareUnc);
        }

        void LanModel::_mapDrive(const std::string& unc)
        {
            FTK_P();
            if (p.mapPending)
            {
                return;
            }
            const std::string letter = normalizeDriveLetter(p.settings->get().driveLetter);
            LanDriveStatus status;
            status.letter = letter;
            status.unc = unc;
            if (letter.empty())
            {
                status.state = LanDriveState::None;
                p.drive->setIfChanged(status);
                _mediaRootUpdate();
                return;
            }
            if (!isNetworkDriveSupported())
            {
                status.state = LanDriveState::Unsupported;
                status.message = "Drive letters are a Windows feature; set the media folder instead.";
                p.drive->setIfChanged(status);
                _mediaRootUpdate();
                return;
            }
            if (unc.empty())
            {
                status.state = LanDriveState::Error;
                status.message = "The server has not said where the media share is; ask the admin to set it.";
                p.drive->setIfChanged(status);
                _mediaRootUpdate();
                return;
            }
            status.state = LanDriveState::Mapping;
            status.message = ftk::Format("Mapping {0}: to {1}...").arg(letter).arg(unc);
            p.drive->setIfChanged(status);
            _mediaRootUpdate();

            p.mapPending = true;
            std::thread(
                [shared = p.shared, letter, unc]
                {
                    const NetworkDriveResult result = mapNetworkDrive(letter, unc);
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->drive = result;
                }).detach();
            p.timer->start(tickTimeout, [this] { _tick(); });
        }

        void LanModel::_tick()
        {
            FTK_P();
            std::optional<SyncServerInfo> info;
            std::optional<NetworkDriveResult> drive;
            {
                std::unique_lock<std::mutex> lock(p.shared->mutex);
                info.swap(p.shared->info);
                drive.swap(p.shared->drive);
            }

            if (info.has_value())
            {
                p.helloPending = false;
                SyncServerInfo value = info.value();
                SyncSettings settings = p.settings->get();
                std::string unc = settings.shareUnc;
                switch (value.state)
                {
                case SyncServerState::Ok:
                case SyncServerState::Unregistered:
                    // Every PC runs the same DJV; one that does not is told,
                    // but not stopped: the notes it writes are the same.
                    if (!value.djvVersion.empty() && value.djvVersion != p.version)
                    {
                        const std::string warning = ftk::Format(
                            "The studio runs DJV {0}; this PC runs {1}.").
                            arg(value.djvVersion).
                            arg(p.version);
                        value.message = value.message.empty() ? warning : value.message + " " + warning;
                    }
                    p.log(ftk::Format("Sync server \"{0}\" at {1}: {2}{3}").
                        arg(value.name).
                        arg(value.server).
                        arg(SyncServerState::Ok == value.state ? "registered" : "not registered").
                        arg(value.message.empty() ? std::string() : " -- " + value.message),
                        value.message.empty() ? ftk::LogType::Message : ftk::LogType::Warning);
                    // The share the server names is kept, for a start when
                    // the server cannot be reached.
                    if (!value.storageUnc.empty())
                    {
                        unc = value.storageUnc;
                        if (unc != settings.shareUnc)
                        {
                            settings.shareUnc = unc;
                            p.setSettings(settings);
                        }
                    }
                    break;
                default:
                    p.log(value.message, ftk::LogType::Warning);
                    if (!unc.empty())
                    {
                        p.log(ftk::Format("Mapping the media share last named by the server, {0}.").arg(unc));
                    }
                    break;
                }
                p.serverInfo->setIfChanged(value);
                _mapDrive(unc);
            }

            if (drive.has_value())
            {
                p.mapPending = false;
                LanDriveStatus status = p.drive->get();
                status.state = drive->ok ? LanDriveState::Mapped : LanDriveState::Error;
                status.message = drive->ok ?
                    std::string(ftk::Format("{0}: is {1}").arg(status.letter).arg(status.unc)) :
                    drive->message;
                p.log(
                    drive->ok ?
                        std::string(ftk::Format("{0}: {1} {2}").
                            arg(status.letter).
                            arg(drive->alreadyMapped ? "was already mapped to" : "mapped to").
                            arg(status.unc)) :
                        drive->message,
                    drive->ok ? ftk::LogType::Message : ftk::LogType::Error);
                p.drive->setIfChanged(status);
                _mediaRootUpdate();
            }

            if (!p.helloPending && !p.mapPending)
            {
                p.timer->stop();
            }
        }

        void LanModel::_driveUpdate()
        {
            FTK_P();
            if (p.mapPending)
            {
                return;
            }
            // A letter changed in the settings, or one mapped by an earlier
            // run of DJV in this logon: what Windows says it is now, which
            // costs nothing to ask, unlike mapping.
            const SyncSettings& settings = p.settings->get();
            const std::string letter = normalizeDriveLetter(settings.driveLetter);
            LanDriveStatus status = p.drive->get();
            if (letter.empty())
            {
                status = LanDriveStatus();
            }
            else if (letter != status.letter || LanDriveState::None == status.state)
            {
                status = LanDriveStatus();
                status.letter = letter;
                status.unc = settings.shareUnc;
                const std::string current = getDriveConnection(letter);
                if (!current.empty() && isSameUncPath(current, settings.shareUnc))
                {
                    status.state = LanDriveState::Mapped;
                    status.message = ftk::Format("{0}: is {1}").arg(letter).arg(current);
                }
                else if (!isNetworkDriveSupported())
                {
                    status.state = LanDriveState::Unsupported;
                }
            }
            p.drive->setIfChanged(status);
        }

        void LanModel::_mediaRootUpdate()
        {
            FTK_P();
            const SyncSettings& settings = p.settings->get();
            const LanDriveStatus& drive = p.drive->get();
            std::string root = settings.mediaRoot;
            if (LanDriveState::Mapped == drive.state && !drive.letter.empty())
            {
                root = drive.letter + ":/";
            }
            p.mediaRoot->setIfChanged(root);
        }
    }
}
