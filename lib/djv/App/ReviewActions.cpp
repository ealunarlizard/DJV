// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/ReviewActions.h>

#include <djv/App/App.h>
#include <djv/App/MainWindow.h>
#include <djv/Models/AnnotationsModel.h>
#include <djv/Models/DrawModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/MarkersModel.h>
#include <djv/Models/SettingsModel.h>
#include <djv/Models/LanModel.h>
#include <djv/Models/SyncModel.h>

#include <tlRender/Timeline/Player.h>

#include <ftk/Core/Path.h>

namespace djv
{
    namespace app
    {
        std::string defaultSyncSession(const std::shared_ptr<App>& app)
        {
            // The session the review is in, then one named after its file,
            // with what a session name cannot hold made safe.
            if (const auto& sync = app->getSyncModel()->getSync())
            {
                return sync->session;
            }
            std::string out = ftk::fromFileSystem(app->getReviewPath().stem());
            for (auto& c : out)
            {
                if (!models::isValidSyncSession(std::string(1, c)))
                {
                    c = '-';
                }
            }
            if (!models::isValidSyncSession(out))
            {
                out = "review-" + models::generateId().substr(0, 8);
            }
            return out;
        }

        struct ReviewActions::Private
        {
            bool hasFiles = false;
            bool syncConnected = false;
            bool hasSyncMedia = false;
            bool hasPlayer = false;
            bool hasMarkers = false;
            bool hasMarkerItems = false;
            //! Whether the in/out points narrow the timeline: adding a range
            //! marker is only meaningful when they do.
            bool narrowed = false;

            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > filesObserver;
            std::shared_ptr<ftk::Observer<models::DrawTool> > toolObserver;
            std::shared_ptr<ftk::Observer<bool> > enabledObserver;
            std::shared_ptr<ftk::Observer<bool> > hasUndoObserver;
            std::shared_ptr<ftk::Observer<bool> > hasRedoObserver;
            std::shared_ptr<ftk::ListObserver<int> > markersObserver;
            std::shared_ptr<ftk::ListObserver<models::ReviewMarker> > markerItemsObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerObserver;
            std::shared_ptr<ftk::Observer<OTIO_NS::TimeRange> > inOutObserver;
            std::shared_ptr<ftk::Observer<models::SyncStatus> > syncObserver;
            std::shared_ptr<ftk::Observer<std::optional<models::SyncMedia> > > syncMediaObserver;
        };

        void ReviewActions::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow)
        {
            IActions::_init(context, app, "Review");
            FTK_P();

            // Register the commands.
            auto appWeak = std::weak_ptr<App>(app);
            // A path opens or saves that file rather than asking, for
            // automation.
            _addCommand(
                "Open",
                "Open a review, replacing the current session; e.g., "
                "{ \"path\": \"session.djvr\" }, or nothing to choose one.",
                [appWeak](const nlohmann::json& args)
                {
                    if (auto app = appWeak.lock())
                    {
                        if (args.is_object() && args.contains("path"))
                        {
                            app->openReview(ftk::toFileSystem(
                                args.at("path").get<std::string>()));
                        }
                        else
                        {
                            app->openReviewDialog();
                        }
                    }
                });

            _addCommand(
                "Save",
                "Save the current session as a review; e.g., "
                "{ \"path\": \"session.djvr\" }, or nothing to save to the "
                "review's own file.",
                [appWeak](const nlohmann::json& args)
                {
                    if (auto app = appWeak.lock())
                    {
                        if (args.is_object() && args.contains("path"))
                        {
                            app->saveReview(ftk::toFileSystem(
                                args.at("path").get<std::string>()));
                        }
                        else
                        {
                            app->saveReview();
                        }
                    }
                });

            _addCommand(
                "SaveAs",
                "Save the current session as a new review.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->saveReviewAs();
                    }
                });

            _addCommand(
                "Import",
                "Import an OTIO timeline as a review: it becomes the "
                "review's first file, and its markers become the feedback.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->importReviewDialog();
                    }
                });

            _addCommand(
                "Export",
                "Write the review's markers to an OTIO file.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->exportReviewMarkers();
                    }
                });

            _addCommand(
                "Close",
                "Close the review and reset to the startup state.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->closeReview();
                    }
                });

            // Selecting a tool turns drawing on, and turning the active tool
            // off gives the left mouse button back to the frame shuttle --
            // the same as the review tool's own buttons.
            _addCheckCommand(
                "Draw",
                "Draw strokes on the frame.",
                [appWeak](const nlohmann::json& args)
                {
                    const bool value = args.at("value").get<bool>();
                    if (auto app = appWeak.lock())
                    {
                        auto drawModel = app->getDrawModel();
                        if (value)
                        {
                            drawModel->setTool(models::DrawTool::Pen);
                            drawModel->setEnabled(true);
                        }
                        else if (models::DrawTool::Pen == drawModel->getTool())
                        {
                            drawModel->setEnabled(false);
                        }
                    }
                });

            _addCheckCommand(
                "Erase",
                "Erase the strokes you touch.",
                [appWeak](const nlohmann::json& args)
                {
                    const bool value = args.at("value").get<bool>();
                    if (auto app = appWeak.lock())
                    {
                        auto drawModel = app->getDrawModel();
                        if (value)
                        {
                            drawModel->setTool(models::DrawTool::Eraser);
                            drawModel->setEnabled(true);
                        }
                        else if (models::DrawTool::Eraser == drawModel->getTool())
                        {
                            drawModel->setEnabled(false);
                        }
                    }
                });

            _addCommand(
                "Undo",
                "Undo drawing.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getAnnotationsModel()->undo();
                    }
                });

            _addCommand(
                "Redo",
                "Redo drawing.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getAnnotationsModel()->redo();
                    }
                });

            _addCommand(
                "ClearDrawing",
                "Remove every stroke on the current frame.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        auto player = app->observePlayer()->get();
                        if (player)
                        {
                            // Every active file, so a compared frame's "B"
                            // strokes go with the "A" strokes.
                            std::vector<std::string> ids;
                            for (const auto& i : app->getFilesModel()->getActive())
                            {
                                ids.push_back(i->id);
                            }
                            app->getAnnotationsModel()->clearFrame(
                                ids,
                                player->getCurrentTime());
                        }
                    }
                });

            auto mainWindowWeak = std::weak_ptr<MainWindow>(mainWindow);
            // The identifier stays "AddNote" so saved shortcut bindings
            // survive the marker unification.
            _addCommand(
                "AddNote",
                "Open the review tool and add a marker about the current "
                "frame, edited in place; or, given the text, add it as it "
                "is, e.g., { \"text\": \"Grade is too cold.\" }.",
                [appWeak, mainWindowWeak](const nlohmann::json& args)
                {
                    if (args.is_object() && args.contains("text"))
                    {
                        if (auto app = appWeak.lock())
                        {
                            if (auto player = app->observePlayer()->get())
                            {
                                const OTIO_NS::RationalTime time = player->getCurrentTime();
                                app->getMarkersModel()->add(
                                    OTIO_NS::TimeRange(time, OTIO_NS::RationalTime(1.0, time.rate())),
                                    std::string(),
                                    args.at("text").get<std::string>());
                            }
                        }
                    }
                    else if (auto mainWindow = mainWindowWeak.lock())
                    {
                        mainWindow->addReviewNote();
                    }
                });

            // Sync. The server comes from the settings unless given, and
            // the session from the review's file name unless given, so the
            // people sharing a review file land in the same session.
            _addCommand(
                "SyncConnect",
                "Share the review's markers through a sync server session; "
                "e.g., { \"server\": \"http://localhost:8787\", "
                "\"session\": \"sh010\" }.",
                [appWeak](const nlohmann::json& args)
                {
                    if (auto app = appWeak.lock())
                    {
                        models::ReviewSync sync;
                        sync.server = app->getSettingsModel()->getSync().server;
                        sync.session = defaultSyncSession(app);
                        if (args.is_object())
                        {
                            if (args.contains("server"))
                            {
                                args.at("server").get_to(sync.server);
                            }
                            if (args.contains("session"))
                            {
                                args.at("session").get_to(sync.session);
                            }
                        }
                        app->connectSync(sync);
                    }
                });

            _addCommand(
                "SyncDisconnect",
                "Stop sharing the review's markers. The markers stay.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->disconnectSync();
                    }
                });

            // The session's media: the file its markers are about, opened
            // by whoever joins with nothing open.
            _addCommand(
                "SyncShareMedia",
                "Make the current file the sync session's media, replacing "
                "what it had.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->shareSyncMedia();
                    }
                });

            // The studio network: ask the server about this PC again, and map
            // the media drive, as at start.
            _addCommand(
                "SyncTestServer",
                "Ask the sync server whether this PC is registered and where "
                "the media share is, then map the media drive.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getLanModel()->start();
                    }
                });

            _addCommand(
                "SyncMapDrive",
                "Map the media drive to the share the sync server last named, "
                "without asking the server.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->getLanModel()->mapDrive();
                    }
                });

            _addCommand(
                "SyncLoadMedia",
                "Open the sync session's media, found through the network "
                "media folder in the Sync Server settings.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->loadSyncMedia();
                    }
                });

            _addCommand(
                "AddRange",
                "Open the review tool and add a marker for the timeline "
                "in/out points, edited in place.",
                [mainWindowWeak](const nlohmann::json&)
                {
                    if (auto mainWindow = mainWindowWeak.lock())
                    {
                        mainWindow->addReviewRange();
                    }
                });

            // Jump between the frames that carry a marker or a drawing. In a
            // review these are the only frames that matter, and stepping to
            // them by hand over a long timeline is the slow part.
            _addCommand(
                "PrevFrame",
                "Go to the previous frame with a marker or a drawing.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->seekReviewMarker(false);
                    }
                });

            _addCommand(
                "NextFrame",
                "Go to the next frame with a marker or a drawing.",
                [appWeak](const nlohmann::json&)
                {
                    if (auto app = appWeak.lock())
                    {
                        app->seekReviewMarker(true);
                    }
                });

            // Create the actions.
            _actions["Open"] = ftk::Action::create(
                "Open",
                _command("Open"));
            _actions["Save"] = ftk::Action::create(
                "Save",
                _command("Save"));
            _actions["SaveAs"] = ftk::Action::create(
                "Save As...",
                _command("SaveAs"));
            _actions["Import"] = ftk::Action::create(
                "Import...",
                _command("Import"));
            _actions["Export"] = ftk::Action::create(
                "Export...",
                _command("Export"));
            _actions["Close"] = ftk::Action::create(
                "Close",
                _command("Close"));
            _actions["Draw"] = ftk::Action::create(
                "Draw",
                "DrawTool",
                _checkCommand("Draw"));
            _actions["Erase"] = ftk::Action::create(
                "Erase",
                "Eraser",
                _checkCommand("Erase"));
            _actions["Undo"] = ftk::Action::create(
                "Undo Drawing",
                "Undo",
                _command("Undo"));
            _actions["Redo"] = ftk::Action::create(
                "Redo Drawing",
                "Redo",
                _command("Redo"));
            _actions["ClearDrawing"] = ftk::Action::create(
                "Clear Drawing",
                "Remove",
                _command("ClearDrawing"));
            _actions["AddNote"] = ftk::Action::create(
                "Add Marker",
                _command("AddNote"));
            _actions["AddRange"] = ftk::Action::create(
                "Add Range",
                _command("AddRange"));
            _actions["SyncConnect"] = ftk::Action::create(
                "Connect Sync",
                _command("SyncConnect"));
            _actions["SyncDisconnect"] = ftk::Action::create(
                "Disconnect Sync",
                _command("SyncDisconnect"));
            _actions["SyncShareMedia"] = ftk::Action::create(
                "Share Current File with Sync",
                _command("SyncShareMedia"));
            _actions["SyncLoadMedia"] = ftk::Action::create(
                "Load Sync Media",
                _command("SyncLoadMedia"));
            _actions["SyncTestServer"] = ftk::Action::create(
                "Check Sync Server",
                _command("SyncTestServer"));
            _actions["SyncMapDrive"] = ftk::Action::create(
                "Map Media Drive",
                _command("SyncMapDrive"));
            _actions["PrevFrame"] = ftk::Action::create(
                "Previous Marker",
                "ReviewPrev",
                _command("PrevFrame"));
            _actions["NextFrame"] = ftk::Action::create(
                "Next Marker",
                "ReviewNext",
                _command("NextFrame"));

            // The tooltips the review tool's buttons show, richer than the
            // command documentation that fills these in by default: the
            // buttons bind to them, and the shortcut suffix follows the
            // bindings.
            _tooltips["Draw"] =
                "Draw strokes.\n"
                "\n"
                "Click again to stop drawing.";
            _tooltips["Erase"] =
                "Erase the strokes you touch.\n"
                "\n"
                "Click again to stop.";
            _tooltips["ClearDrawing"] = "Remove every stroke on this frame.";
            _tooltips["AddNote"] =
                "Add a marker about the current frame, written in place.";
            _tooltips["AddRange"] =
                "Add a marker for the timeline in/out points, written in "
                "place.";

            // Register the shortcuts.
            // Alt rather than Shift on the command modifier: Shift+Ctrl+O is
            // "Open with audio", and Shift+Ctrl+S is free but keeping the pair
            // symmetrical is worth more than reusing it.
            _addShortcut(
                "Open",
                "Open review",
                ftk::KeyShortcut(
                    ftk::Key::O,
                    static_cast<int>(ftk::KeyModifier::Alt) |
                    static_cast<int>(ftk::commandKeyModifier)));
            _addShortcut(
                "Save",
                "Save review",
                ftk::KeyShortcut(
                    ftk::Key::S,
                    static_cast<int>(ftk::KeyModifier::Alt) |
                    static_cast<int>(ftk::commandKeyModifier)));
            _addShortcut("SaveAs", "Save review as");
            _addShortcut("Import", "Import a timeline");
            _addShortcut("Export", "Export markers");
            _addShortcut("Close", "Close review");
            // No default keys yet: which keys serve drawing best is still
            // being worked out with the users (#838). The actions are in the
            // shortcuts editor, so any key can be bound today.
            _addShortcut("Draw", "Draw strokes");
            _addShortcut("Erase", "Erase strokes");
            _addShortcut(
                "Undo",
                "Undo drawing",
                ftk::KeyShortcut(ftk::Key::Z, static_cast<int>(ftk::commandKeyModifier)));
            // Ctrl+Y as well where that is the convention; macOS has only the
            // one.
#if defined(__APPLE__)
            const ftk::KeyShortcut redoSecondary;
#else // __APPLE__
            const ftk::KeyShortcut redoSecondary(
                ftk::Key::Y,
                static_cast<int>(ftk::KeyModifier::Control));
#endif // __APPLE__
            _addShortcut(
                "Redo",
                "Redo drawing",
                ftk::KeyShortcut(
                    ftk::Key::Z,
                    static_cast<int>(ftk::KeyModifier::Shift) |
                    static_cast<int>(ftk::commandKeyModifier)),
                redoSecondary);
            _addShortcut("ClearDrawing", "Clear drawing");
            _addShortcut("AddNote", "Add a marker");
            _addShortcut("AddRange", "Add a range");
            _addShortcut("SyncConnect", "Connect sync");
            _addShortcut("SyncDisconnect", "Disconnect sync");
            _addShortcut("SyncShareMedia", "Share the current file with sync");
            _addShortcut("SyncLoadMedia", "Load the sync media");
            _addShortcut("SyncTestServer", "Check the sync server");
            _addShortcut("SyncMapDrive", "Map the media drive");
            // Shift and Control on the arrows are already taken by the X10 and
            // X100 frame steps.
            _addShortcut(
                "PrevFrame",
                "Previous marker",
                ftk::KeyShortcut(
                    ftk::Key::Left,
                    static_cast<int>(ftk::KeyModifier::Alt)));
            _addShortcut(
                "NextFrame",
                "Next marker",
                ftk::KeyShortcut(
                    ftk::Key::Right,
                    static_cast<int>(ftk::KeyModifier::Alt)));

            _shortcutsUpdate(app->getSettingsModel()->getShortcuts());

            p.filesObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                app->getFilesModel()->observeFiles(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >& value)
                {
                    // There is nothing to save, and nothing to close, until a
                    // file is open. Opening a review stays available.
                    _actions["Save"]->setEnabled(!value.empty());
                    _actions["SaveAs"]->setEnabled(!value.empty());
                    _actions["Close"]->setEnabled(!value.empty());
                    _p->hasFiles = !value.empty();
                    _syncUpdate();
                });

            auto drawModel = app->getDrawModel();
            p.toolObserver = ftk::Observer<models::DrawTool>::create(
                drawModel->observeTool(),
                [this, appWeak](models::DrawTool)
                {
                    if (auto app = appWeak.lock())
                    {
                        _drawStateUpdate(app);
                    }
                });
            p.enabledObserver = ftk::Observer<bool>::create(
                drawModel->observeEnabled(),
                [this, appWeak](bool)
                {
                    if (auto app = appWeak.lock())
                    {
                        _drawStateUpdate(app);
                    }
                });

            p.hasUndoObserver = ftk::Observer<bool>::create(
                app->getAnnotationsModel()->observeHasUndo(),
                [this](bool value)
                {
                    _actions["Undo"]->setEnabled(value);
                });
            p.hasRedoObserver = ftk::Observer<bool>::create(
                app->getAnnotationsModel()->observeHasRedo(),
                [this](bool value)
                {
                    _actions["Redo"]->setEnabled(value);
                });

            p.markerItemsObserver = ftk::ListObserver<models::ReviewMarker>::create(
                app->getMarkersModel()->observeMarkers(),
                [this](const std::vector<models::ReviewMarker>& value)
                {
                    FTK_P();
                    p.hasMarkerItems = !value.empty();
                    _actions["Export"]->setEnabled(
                        p.hasPlayer && p.hasMarkerItems);
                });

            p.syncObserver = ftk::Observer<models::SyncStatus>::create(
                app->getSyncModel()->observeStatus(),
                [this](const models::SyncStatus& value)
                {
                    _p->syncConnected = value.state != models::SyncState::Disconnected;
                    _syncUpdate();
                });
            p.syncMediaObserver = ftk::Observer<std::optional<models::SyncMedia> >::create(
                app->getSyncModel()->observeMedia(),
                [this](const std::optional<models::SyncMedia>& value)
                {
                    _p->hasSyncMedia = value.has_value();
                    _syncUpdate();
                });

            p.markersObserver = ftk::ListObserver<int>::create(
                app->observeReviewMarkers(),
                [this](const std::vector<int>& value)
                {
                    _p->hasMarkers = !value.empty();
                    _markersUpdate();
                });

            p.playerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayer(),
                [this](const std::shared_ptr<tl::Player>& value)
                {
                    FTK_P();
                    p.hasPlayer = value.get();
                    _actions["Draw"]->setEnabled(p.hasPlayer);
                    _actions["Erase"]->setEnabled(p.hasPlayer);
                    _actions["ClearDrawing"]->setEnabled(p.hasPlayer);
                    _actions["AddNote"]->setEnabled(p.hasPlayer);
                    _actions["Export"]->setEnabled(
                        p.hasPlayer && p.hasMarkerItems);
                    if (value)
                    {
                        p.inOutObserver = ftk::Observer<OTIO_NS::TimeRange>::create(
                            value->observeInOutRange(),
                            [this, value](const OTIO_NS::TimeRange& range)
                            {
                                FTK_P();
                                p.narrowed = !tl::compareExact(
                                    range, value->getTimeRange());
                                _actions["AddRange"]->setEnabled(p.narrowed);
                            });
                    }
                    else
                    {
                        p.inOutObserver.reset();
                        p.narrowed = false;
                        _actions["AddRange"]->setEnabled(false);
                    }
                    _markersUpdate();
                });
        }

        ReviewActions::ReviewActions() :
            _p(new Private)
        {}

        ReviewActions::~ReviewActions()
        {}

        std::shared_ptr<ReviewActions> ReviewActions::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow)
        {
            auto out = std::shared_ptr<ReviewActions>(new ReviewActions);
            out->_init(context, app, mainWindow);
            return out;
        }

        void ReviewActions::_drawStateUpdate(const std::shared_ptr<App>& app)
        {
            auto drawModel = app->getDrawModel();
            const bool enabled = drawModel->isEnabled();
            const models::DrawTool tool = drawModel->getTool();
            _actions["Draw"]->setChecked(
                enabled && models::DrawTool::Pen == tool);
            _actions["Erase"]->setChecked(
                enabled && models::DrawTool::Eraser == tool);
        }

        void ReviewActions::_markersUpdate()
        {
            FTK_P();
            // There is nowhere to jump until a frame carries a marker or a
            // drawing.
            const bool enabled = p.hasPlayer && p.hasMarkers;
            _actions["PrevFrame"]->setEnabled(enabled);
            _actions["NextFrame"]->setEnabled(enabled);
        }

        void ReviewActions::_syncUpdate()
        {
            FTK_P();
            _actions["SyncDisconnect"]->setEnabled(p.syncConnected);
            _actions["SyncShareMedia"]->setEnabled(p.syncConnected && p.hasFiles);
            _actions["SyncLoadMedia"]->setEnabled(p.syncConnected && p.hasSyncMedia);
        }
    }
}
