// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/AppPrivate.h>
#include <djv/App/MainWindow.h>
#include <djv/Models/Credentials.h>
#include <djv/Models/DeepLink.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/LanModel.h>
#include <djv/Models/MarkersModel.h>
#include <djv/Models/SyncModel.h>
#include <djv/Models/ToolsModel.h>

#include <ftk/Core/Format.h>
#include <ftk/Core/OS.h>
#include <ftk/Core/Path.h>
#include <ftk/Core/Timer.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace djv
{
    namespace app
    {
        const std::shared_ptr<models::SyncModel>& App::getSyncModel() const
        {
            return _p->syncModel;
        }

        const std::shared_ptr<models::LanModel>& App::getLanModel() const
        {
            return _p->lanModel;
        }

        void App::connectSync(const models::ReviewSync& value)
        {
            FTK_P();
            const auto prev = p.syncModel->getSync();
            if (_connectSync(value) && prev != p.syncModel->getSync())
            {
                // The session is part of the review.
                _markModified();
            }
        }

        void App::disconnectSync()
        {
            FTK_P();
            if (p.syncModel->getSync().has_value())
            {
                p.syncModel->disconnect();
                _markModified();
            }
        }

        bool App::_connectSync(const models::ReviewSync& value)
        {
            FTK_P();
            try
            {
                p.syncToken = p.lanModel->observeAuth()->get().token;
                p.syncModel->connect(
                    value,
                    p.syncToken,
                    p.lanModel->getIdentity());
            }
            catch (const std::exception& e)
            {
                _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                return false;
            }
            return true;
        }

        void App::shareSyncMedia()
        {
            FTK_P();
            if (!p.syncModel->getSync().has_value())
            {
                _context->log(
                    "djv::app::App",
                    "Connect to a sync session before sharing a file with it.",
                    ftk::LogType::Error);
            }
            else if (!p.syncModel->shareMedia())
            {
                _context->log(
                    "djv::app::App",
                    "There is no file open to share with the sync session.",
                    ftk::LogType::Error);
            }
        }

        void App::loadSyncMedia()
        {
            FTK_P();
            const auto media = p.syncModel->observeMedia()->get();
            if (!media.has_value())
            {
                _context->log(
                    "djv::app::App",
                    "The sync session has no media to open.",
                    ftk::LogType::Error);
                return;
            }
            bool exists = false;
            const std::filesystem::path path = resolveSyncMedia(exists);
            if (!exists)
            {
                // Say where it was looked for: the drive or the folder in the
                // settings is the likely culprit, and the path shows which.
                const std::string mediaRoot = p.lanModel->observeMediaRoot()->get();
                const models::LanDriveStatus& drive = p.lanModel->observeDrive()->get();
                std::string hint;
                if (models::LanDriveState::Mapping == drive.state)
                {
                    hint = "The media drive is still being mapped; try again in a moment.";
                }
                else if (models::LanDriveState::Error == drive.state)
                {
                    hint = drive.message;
                }
                else if (mediaRoot.empty())
                {
                    hint = "Set the drive letter, or the media folder, in the Sync Server settings.";
                }
                else
                {
                    hint = ftk::Format("Check the drive letter or the media folder in the Sync Server settings (\"{0}\").").
                        arg(mediaRoot).
                        str();
                }
                _context->log(
                    "djv::app::App",
                    ftk::Format("The sync session's media \"{0}\" cannot be found at \"{1}\". {2}").
                        arg(media->name).
                        arg(ftk::fromFileSystem(path)).
                        arg(hint),
                    ftk::LogType::Error);
                return;
            }

            // A second video is compared with the first, as "-compare" on
            // the command line does: B is opened first, so that A, opened
            // after it, is the current file.
            std::filesystem::path bPath;
            if (media->compare.has_value())
            {
                bool bExists = false;
                bPath = models::resolveSyncMedia(
                    media->compare.value(),
                    ftk::toFileSystem(p.lanModel->observeMediaRoot()->get()),
                    bExists);
                if (!bExists)
                {
                    _context->log(
                        "djv::app::App",
                        ftk::Format("The sync session's second video \"{0}\" cannot be found at \"{1}\"; "
                            "opening the first alone.").
                            arg(media->compare->name).
                            arg(ftk::fromFileSystem(bPath)),
                        ftk::LogType::Error);
                    bPath.clear();
                }
            }
            if (!bPath.empty())
            {
                _context->log(
                    "djv::app::Sync",
                    ftk::Format("Opening the session's second video, as B: {0}").arg(ftk::fromFileSystem(bPath)));
                open(ftk::Path(ftk::fromFileSystem(bPath)));
            }
            _context->log(
                "djv::app::Sync",
                ftk::Format("Opening the session's video: {0}").arg(ftk::fromFileSystem(path)));
            open(ftk::Path(ftk::fromFileSystem(path)));
            if (!bPath.empty())
            {
                const auto& files = p.filesModel->getFiles();
                const std::string b = ftk::Path(ftk::fromFileSystem(bPath)).get();
                for (size_t i = 0; i < files.size(); ++i)
                {
                    if (files[i] && files[i]->path.get() == b)
                    {
                        // Side by side unless the person already compares
                        // some other way.
                        tl::CompareOptions options = p.filesModel->getCompareOptions();
                        if (tl::Compare::None == options.compare || tl::Compare::B == options.compare)
                        {
                            options.compare = tl::Compare::Horizontal;
                            p.filesModel->setCompareOptions(options);
                        }
                        p.filesModel->setB(static_cast<int>(i), true);
                        break;
                    }
                }
            }
        }

        std::filesystem::path App::resolveSyncMedia(bool& exists) const
        {
            FTK_P();
            exists = false;
            const auto& media = p.syncModel->observeMedia()->get();
            if (!media.has_value())
            {
                return std::filesystem::path();
            }
            return models::resolveSyncMedia(
                media.value(),
                ftk::toFileSystem(p.lanModel->observeMediaRoot()->get()),
                exists);
        }

        void App::_syncLocalMediaUpdate()
        {
            FTK_P();
            std::optional<models::SyncMedia> media;
            if (const auto& a = p.filesModel->getA())
            {
                const std::filesystem::path root = ftk::toFileSystem(p.lanModel->observeMediaRoot()->get());
                media = models::syncMediaFromPath(a->path.get(), root);
                // The file compared with it goes along, so that the session
                // compares the same two.
                const auto& active = p.filesModel->getActive();
                if (active.size() > 1 && active[1] && active[1] != a)
                {
                    const models::SyncMedia b = models::syncMediaFromPath(active[1]->path.get(), root);
                    media->compare = models::SyncMediaFile{ b.path, b.pathAbsolute, b.name };
                }
            }
            p.syncModel->setLocalMedia(media);
        }

        namespace
        {
            //! Where every step of a review link is logged, so that the
            //! System Log tool, and the log file, tell the whole story of
            //! one, and the steps are easy to pick out among the rest.
            const std::string linkLog = "djv::app::ReviewLink";

            std::string onOff(bool value)
            {
                return value ? "true" : "false";
            }
        }

        void App::openDeepLink(const std::string& value, const std::string& source)
        {
            FTK_P();
            _context->log(
                linkLog,
                ftk::Format("Received the review link {0} from {1}").
                    arg(value).
                    arg(source.empty() ? std::string("the command Review/SyncOpenLink") : source));
            std::string error;
            const auto link = models::parseDeepLink(value, &error);
            if (!link.has_value())
            {
                _context->log(linkLog, error, ftk::LogType::Error);
                return;
            }
            _context->log(
                linkLog,
                ftk::Format("The link asks for the session \"{0}\" on the server {1}, autoConnect {2}").
                    arg(link->session).
                    arg(link->server).
                    arg(onOff(link->autoConnect)));
            if (p.mainWindow)
            {
                p.mainWindow->raise();
            }
            if (p.deepLink.has_value())
            {
                _context->log(
                    linkLog,
                    ftk::Format("The link for the session \"{0}\", still waiting, is dropped for this one").
                        arg(p.deepLink->link.session),
                    ftk::LogType::Warning);
            }
            // A server check still under way is for the link dropped.
            p.deepLinkProbe.reset();
            if (p.deepLinkProbeTimer)
            {
                p.deepLinkProbeTimer->stop();
            }
            p.deepLink = DeepLinkPending{ link.value(), false };
            _deepLinkContinue();
        }

        std::shared_ptr<ftk::IObservable<models::ReviewSync> > App::observeSyncProposal() const
        {
            return _p->syncProposal;
        }

        void App::_deepLinkContinue()
        {
            FTK_P();
            if (!p.deepLink.has_value())
            {
                return;
            }

            // Signed in first: the session is joined with the token, and the
            // server's identity, which the check below needs, comes with the
            // sign-in.
            const models::SyncSettings& settings = p.settingsModel->getSync();
            const models::LanAuth& auth = p.lanModel->observeAuth()->get();
            switch (auth.state)
            {
            case models::LanAuthState::SignedIn:
                break;
            case models::LanAuthState::SigningIn:
                // lanAuthObserver comes back here once it is done.
                _context->log(
                    linkLog,
                    ftk::Format("Waiting for the sign-in to {0} to finish").arg(settings.server));
                return;
            default:
            {
                if (settings.server.empty())
                {
                    _context->log(
                        linkLog,
                        "No sync server is set in the Sync Server settings: set it and sign in, "
                        "and the link is followed.",
                        ftk::LogType::Error);
                    p.toolsModel->showSection("Settings", "SyncServer");
                }
                else if (!settings.username.empty() &&
                    !models::loadSyncPassword(settings.server, settings.username).empty() &&
                    models::LanAuthState::Failed != auth.state)
                {
                    _context->log(
                        linkLog,
                        ftk::Format("Signing in to {0} as {1}, with the password kept on this PC").
                            arg(settings.server).
                            arg(settings.username));
                    p.lanModel->start();
                }
                else
                {
                    std::string why;
                    if (models::LanAuthState::Failed == auth.state)
                    {
                        why = ftk::Format("signing in failed ({0})").arg(auth.message).str();
                    }
                    else if (settings.username.empty())
                    {
                        why = "no user name is set";
                    }
                    else
                    {
                        why = "no password is kept on this PC";
                    }
                    _context->log(
                        linkLog,
                        ftk::Format("Sign in to {0} to open the session \"{1}\": {2}. "
                            "The link is followed once you have.").
                            arg(settings.server).
                            arg(p.deepLink->link.session).
                            arg(why),
                        ftk::LogType::Warning);
                    p.toolsModel->showSection("Settings", "SyncServer");
                }
                return;
            }
            }

            // A web page must not be able to send this DJV, and the password
            // it keeps, to another server, so the session is always joined
            // on the server of the settings. The link's server only says
            // which one it is meant for: the same address, or another
            // address the server answers to with the same identity.
            if (!p.deepLink->serverChecked)
            {
                if (models::isSameSyncServer(p.deepLink->link.server, settings.server))
                {
                    _context->log(
                        linkLog,
                        ftk::Format("The link's server is this DJV's server, {0}").arg(settings.server));
                    p.deepLink->serverChecked = true;
                }
                else
                {
                    _deepLinkProbe();
                    return;
                }
            }

            const models::DeepLink link = p.deepLink->link;
            p.deepLink.reset();
            models::ReviewSync sync;
            sync.server = settings.server;
            sync.session = link.session;
            const auto current = p.syncModel->getSync();
            if (!link.autoConnect)
            {
                if (current.has_value() && current.value() != sync)
                {
                    _context->log(
                        linkLog,
                        ftk::Format("DJV is in the session \"{0}\": disconnect from it in the Review tool's "
                            "Sync section, then connect to \"{1}\".").
                            arg(current->session).
                            arg(link.session),
                        ftk::LogType::Warning);
                }
                else
                {
                    _context->log(
                        linkLog,
                        ftk::Format("Filled in the session \"{0}\" (autoConnect false): click Connect in the "
                            "Review tool's Sync section to join it").
                            arg(link.session));
                }
                p.syncProposal->setAlways(sync);
                p.toolsModel->showSection("Review", "Sync");
                return;
            }
            if (current.has_value() && current.value() == sync)
            {
                _context->log(
                    linkLog,
                    ftk::Format("Already in the session \"{0}\"").arg(link.session));
                return;
            }

            // From an empty review: markers already open would otherwise be
            // sent into the session (SyncMerger::snapshot()). The person is
            // asked about unsaved changes first, as when closing a review.
            confirmClose(
                [this, sync]
                {
                    FTK_P();
                    if (!p.filesModel->getFiles().empty() || !p.markersModel->getMarkers().empty())
                    {
                        _context->log(linkLog, "Closing the open review, to join the session from an empty one");
                    }
                    _closeReview();
                    p.syncAutoLoaded.reset();
                    _context->log(
                        linkLog,
                        ftk::Format("Joining the session \"{0}\" on {1}").
                            arg(sync.session).
                            arg(sync.server));
                    connectSync(sync);
                },
                [this, sync]
                {
                    _context->log(
                        linkLog,
                        ftk::Format("The open review was kept, so the session \"{0}\" was not joined").
                            arg(sync.session),
                        ftk::LogType::Warning);
                });
        }

        void App::_deepLinkProbe()
        {
            FTK_P();
            const std::string server = p.deepLink->link.server;
            _context->log(
                linkLog,
                ftk::Format("The link names {0}, and this DJV uses {1}: asking {2} whether it is the same server").
                    arg(server).
                    arg(p.settingsModel->getSync().server).
                    arg(server));
            // On a thread of its own, as the sign-in is: the server may be
            // slow or not there at all. No token is sent, nor a password.
            auto probe = std::make_shared<DeepLinkProbe>();
            p.deepLinkProbe = probe;
            std::thread(
                [probe, server, identity = p.lanModel->getIdentity()]
                {
                    const models::SyncServerInfo info = models::fetchSyncServerInfo(server, std::string(), identity);
                    std::unique_lock<std::mutex> lock(probe->mutex);
                    probe->info = info;
                }).detach();
            if (!p.deepLinkProbeTimer)
            {
                p.deepLinkProbeTimer = ftk::Timer::create(_context);
                p.deepLinkProbeTimer->setRepeating(true);
            }
            p.deepLinkProbeTimer->start(
                std::chrono::milliseconds(100),
                [this]
                {
                    FTK_P();
                    if (!p.deepLinkProbe)
                    {
                        p.deepLinkProbeTimer->stop();
                        return;
                    }
                    std::optional<models::SyncServerInfo> info;
                    {
                        std::unique_lock<std::mutex> lock(p.deepLinkProbe->mutex);
                        info = p.deepLinkProbe->info;
                    }
                    if (info.has_value())
                    {
                        p.deepLinkProbeTimer->stop();
                        p.deepLinkProbe.reset();
                        _deepLinkProbed(info.value());
                    }
                });
        }

        void App::_deepLinkProbed(const models::SyncServerInfo& info)
        {
            FTK_P();
            if (!p.deepLink.has_value())
            {
                return;
            }
            const std::string linkServer = p.deepLink->link.server;
            const std::string server = p.settingsModel->getSync().server;
            const models::SyncServerInfo& own = p.lanModel->observeServerInfo()->get();
            std::string refusal;
            if (info.state != models::SyncServerState::Ok && info.state != models::SyncServerState::SignedOut)
            {
                // The reason is a sentence of its own; here it is inside one.
                std::string reason = info.message.empty() ? std::string("no answer") : info.message;
                while (!reason.empty() && '.' == reason.back())
                {
                    reason.pop_back();
                }
                refusal = ftk::Format("{0}, the server the link names, cannot be reached from this PC ({1})").
                    arg(linkServer).
                    arg(reason).
                    str();
            }
            else if (info.serverId.empty() || own.serverId.empty())
            {
                refusal = ftk::Format("whether {0} is the same server as {1} cannot be told: the sync server "
                    "is older than 2.1.1").
                    arg(linkServer).
                    arg(server).
                    str();
            }
            else if (info.serverId != own.serverId)
            {
                refusal = ftk::Format("{0} is another sync server than this DJV's, {1}").
                    arg(linkServer).
                    arg(server).
                    str();
            }
            if (!refusal.empty())
            {
                _context->log(
                    linkLog,
                    ftk::Format("The session \"{0}\" was not opened: {1}. Open the portal at {2}, "
                        "or change the server in the Sync Server settings.").
                        arg(p.deepLink->link.session).
                        arg(refusal).
                        arg(server),
                    ftk::LogType::Error);
                p.deepLink.reset();
                p.toolsModel->showSection("Settings", "SyncServer");
                return;
            }
            _context->log(
                linkLog,
                ftk::Format("{0} is the same server as {1} (server {2}); DJV uses {3} for the session").
                    arg(linkServer).
                    arg(server).
                    arg(info.serverId).
                    arg(server));
            p.deepLink->serverChecked = true;
            _deepLinkContinue();
        }

        void App::_deepLinkInit()
        {
            FTK_P();
            // DJV answers djv: links once it has run, wherever it was
            // installed or built; an installed one registered itself
            // already, and is left as it is.
            // The console build answers for the application beside it, as in
            // repairShellAssociations(): a link opens a window.
            std::filesystem::path exe = ftk::getExePath();
            if (exe.extension() == ".com")
            {
                std::filesystem::path gui = exe;
                gui.replace_extension(".exe");
                std::error_code ec;
                if (std::filesystem::exists(gui, ec))
                {
                    exe = gui;
                }
            }
            std::string error;
            if (!exe.empty() && models::registerDeepLinkHandler(exe, false, &error))
            {
                _context->log(linkLog, ftk::Format("Registered this DJV for review links: {0}").
                    arg(ftk::fromFileSystem(exe)));
            }
            else if (!error.empty())
            {
                _context->log(linkLog, error, ftk::LogType::Warning);
            }
#if defined(_WIN32)
            // Which DJV a click opens: another copy than this one is the
            // first thing to rule out when a link seems to do nothing.
            const std::filesystem::path handler = models::getDeepLinkHandler();
            _context->log(
                linkLog,
                handler.empty() ?
                    std::string("No program is registered for review links on this PC") :
                    ftk::Format("Review links open {0}").arg(ftk::fromFileSystem(handler)).str(),
                handler.empty() ? ftk::LogType::Warning : ftk::LogType::Message);
#endif // _WIN32
            p.deepLinkListener = models::DeepLinkListener::create(_context);
            p.deepLinkListener->setCallback(
                [this](const std::string& value)
                {
                    openDeepLink(value, "another DJV, started for it");
                });
#if defined(_WIN32)
            _context->log(
                linkLog,
                p.deepLinkListener->isListening() ?
                    std::string("Taking the review links of DJVs started for them") :
                    std::string("Another DJV running for this Windows user takes the review links"));
#endif // _WIN32
        }

        void App::_syncMediaUpdate(const std::optional<models::SyncMedia>& value)
        {
            FTK_P();
            if (!value.has_value())
            {
                // Out of the session, or a session without media: a file
                // that could not be found before is worth looking for again
                // next time.
                p.syncAutoLoaded.reset();
                return;
            }
            // Only into an empty player: a file already open is the one the
            // person chose, so the session's media is offered (the Review
            // tool's Load button) rather than put in its place.
            if (!p.filesModel->getFiles().empty())
            {
                if (p.syncMediaNotOpened != value && p.syncAutoLoaded != value)
                {
                    p.syncMediaNotOpened = value;
                    _context->log(
                        "djv::app::Sync",
                        ftk::Format("The session's media \"{0}\" is not opened, since files are open: "
                            "Load in the Review tool's Sync section opens it.").
                            arg(value->name));
                }
                return;
            }
            if (p.syncAutoLoaded == value)
            {
                return;
            }
            p.syncAutoLoaded = value;
            _context->log(
                "djv::app::App",
                ftk::Format("Opening the sync session's media \"{0}\"").arg(value->name));
            loadSyncMedia();
        }
    }
}
