// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/ModelsTest/SyncModelTest.h>

#include <djv/Models/MarkersModel.h>
#include <djv/Models/SettingsModel.h>
#include <djv/Models/SyncModel.h>

#include <ftk/Core/Assert.h>
#include <ftk/Core/Context.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/OS.h>

#include <ftk/Core/Path.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace djv
{
    namespace models_tests
    {
        namespace
        {
            models::ReviewMarker makeMarker(
                const std::string& id,
                const std::string& text,
                double frame = 10.0)
            {
                models::ReviewMarker out;
                out.id = id;
                out.text = text;
                out.range = OTIO_NS::TimeRange(
                    OTIO_NS::RationalTime(frame, 24.0),
                    OTIO_NS::RationalTime(1.0, 24.0));
                return out;
            }

            const models::ReviewMarker* findMarker(
                const std::shared_ptr<models::MarkersModel>& model,
                const std::string& id)
            {
                const auto& markers = model->getMarkers();
                const auto i = std::find_if(
                    markers.begin(),
                    markers.end(),
                    [&id](const models::ReviewMarker& marker) { return marker.id == id; });
                return i != markers.end() ? &*i : nullptr;
            }

            nlohmann::json upsertEvent(
                int64_t rev,
                const std::string& clientId,
                const models::ReviewMarker& marker)
            {
                nlohmann::json out;
                out["type"] = "upsert";
                out["rev"] = rev;
                out["clientId"] = clientId;
                out["marker"] = marker;
                return out;
            }

            nlohmann::json deleteEvent(
                int64_t rev,
                const std::string& clientId,
                const std::string& id)
            {
                nlohmann::json out;
                out["type"] = "delete";
                out["rev"] = rev;
                out["clientId"] = clientId;
                out["id"] = id;
                return out;
            }
        }

        SyncModelTest::SyncModelTest(const std::shared_ptr<ftk::Context>& context) :
            ITest(context, "models_tests::SyncModelTest")
        {}

        std::shared_ptr<SyncModelTest> SyncModelTest::create(
            const std::shared_ptr<ftk::Context>& context)
        {
            return std::shared_ptr<SyncModelTest>(new SyncModelTest(context));
        }

        void SyncModelTest::run()
        {
            _sessions();
            _events();
            _markers();
            _review();
            _settings();
            _media();
            _merge();
            _connect();
            _live();
        }

        void SyncModelTest::_sessions()
        {
            FTK_CHECK(models::isValidSyncSession("sh010"));
            FTK_CHECK(models::isValidSyncSession("Show_A-sh010.v2"));
            FTK_CHECK(!models::isValidSyncSession(""));
            FTK_CHECK(!models::isValidSyncSession("."));
            FTK_CHECK(!models::isValidSyncSession(".."));
            FTK_CHECK(!models::isValidSyncSession("a/b"));
            FTK_CHECK(!models::isValidSyncSession("a b"));
            FTK_CHECK(!models::isValidSyncSession(std::string(129, 'a')));
            _print("  session names -> ok");
        }

        void SyncModelTest::_events()
        {
            // The bytes arrive however the network cuts them: inside a line,
            // between the CR and the LF, with comments between the events.
            models::SyncEventStream stream;
            std::vector<std::string> events;
            for (const std::string chunk :
                {
                    std::string(": djv sync\n\nid: 1\nda"),
                    std::string("ta: {\"rev\":1}\r"),
                    std::string("\n\n: ping\n\ndata: line one\ndata:line two\n"),
                    std::string("\n"),
                    std::string("data: unfinished")
                })
            {
                const auto out = models::parseSyncEvents(stream, chunk.data(), chunk.size());
                events.insert(events.end(), out.begin(), out.end());
            }
            FTK_CHECK(2 == events.size());
            FTK_CHECK("{\"rev\":1}" == events[0]);
            FTK_CHECK("line one\nline two" == events[1]);
            _print("  event stream -> ok");
        }

        void SyncModelTest::_markers()
        {
            auto model = models::MarkersModel::create();
            const std::string id = model->add(
                OTIO_NS::TimeRange(
                    OTIO_NS::RationalTime(10.0, 24.0),
                    OTIO_NS::RationalTime(1.0, 24.0)),
                std::string(),
                "First.");
            FTK_CHECK(model->getMarkers()[0].modified.empty());

            // A change stamps the time; one that changes nothing does not.
            model->update(id, "Second.");
            FTK_CHECK(!model->getMarkers()[0].modified.empty());
            auto marker = model->getMarkers()[0];
            marker.modified = "earlier";
            model->upsert(marker);
            model->update(id, "Second.");
            FTK_CHECK("earlier" == model->getMarkers()[0].modified);

            // Upsert takes a marker as it is, replacing by identifier or
            // adding in time order.
            auto other = makeMarker("other", "From elsewhere.", 5.0);
            other.author = "someone";
            other.created = "2026-01-01T00:00:00Z";
            model->upsert(other);
            FTK_CHECK(2 == model->getMarkers().size());
            FTK_CHECK("other" == model->getMarkers()[0].id);
            FTK_CHECK("someone" == model->getMarkers()[0].author);
            other.text = "Changed elsewhere.";
            model->upsert(other);
            FTK_CHECK(2 == model->getMarkers().size());
            FTK_CHECK("Changed elsewhere." == findMarker(model, "other")->text);
            _print("  markers upsert/modified -> ok");
        }

        void SyncModelTest::_review()
        {
            models::Review review;
            auto marker = makeMarker("m1", "Note.");
            marker.modified = "2026-10-07T12:00:00Z";
            review.markers.push_back(marker);
            review.sync = models::ReviewSync{ "http://localhost:8787", "sh010" };

            nlohmann::json json = review;
            FTK_CHECK(json.contains("sync"));
            FTK_CHECK("sh010" == json["sync"]["session"]);
            FTK_CHECK("2026-10-07T12:00:00Z" == json["markers"][0]["modified"]);

            auto out = json.get<models::Review>();
            FTK_CHECK(out.sync.has_value());
            FTK_CHECK(out.sync.value() == review.sync.value());
            FTK_CHECK(out.markers[0] == marker);

            // A review no longer shared drops the section rather than send
            // the next reader to the session.
            out.sync.reset();
            json = out;
            FTK_CHECK(!json.contains("sync"));

            // A section that cannot be read is kept as it was found.
            nlohmann::json bad = nlohmann::json::object();
            bad["djvReview"] = 1;
            bad["sync"] = { { "server", 1 } };
            auto badReview = bad.get<models::Review>();
            FTK_CHECK(!badReview.sync.has_value());
            json = badReview;
            FTK_CHECK(json["sync"] == bad["sync"]);
            _print("  review sync section -> ok");
        }

        void SyncModelTest::_settings()
        {
            models::ReviewSettings settings;
            settings.showNotes = false;
            settings.noteDuration = 4.F;
            settings.syncServer = "http://review:9000";
            settings.syncToken = "secret";
            settings.syncMediaRoot = "//nas/media";
            nlohmann::json json = settings;
            FTK_CHECK(json.get<models::ReviewSettings>() == settings);

            // Settings saved before the media folder existed still read,
            // keeping the server and the token.
            json.erase("SyncMediaRoot");
            const auto older = json.get<models::ReviewSettings>();
            FTK_CHECK("http://review:9000" == older.syncServer);
            FTK_CHECK("secret" == older.syncToken);
            FTK_CHECK(older.syncMediaRoot.empty());
            _print("  review settings -> ok");
        }

        void SyncModelTest::_media()
        {
            // The media as JSON, and as a server that knows less, or more,
            // might send it.
            models::SyncMedia media;
            media.path = "shows/abc/sh010.mov";
            media.pathAbsolute = "//nas/media/shows/abc/sh010.mov";
            media.name = "sh010.mov";
            media.sharedBy = "someone";
            const nlohmann::json json = media;
            FTK_CHECK(json.get<models::SyncMedia>() == media);
            nlohmann::json partial;
            partial["path"] = "a.mov";
            partial["future"] = true;
            partial["name"] = 12;
            const auto read = partial.get<models::SyncMedia>();
            FTK_CHECK("a.mov" == read.path);
            FTK_CHECK(read.pathAbsolute.empty());
            FTK_CHECK(read.name.empty());

            // A file inside the network media folder is named relative to
            // it, and found there through another machine's folder; one
            // outside it only by its absolute path.
            const std::filesystem::path temp = std::filesystem::temp_directory_path() /
                ("djv-sync-media-" + models::generateId());
            const std::filesystem::path rootA = temp / "a";
            const std::filesystem::path rootB = temp / "b";
            std::filesystem::create_directories(rootA / "shows");
            std::filesystem::create_directories(rootB / "shows");
            std::filesystem::create_directories(temp / "elsewhere");
            for (const auto& file : {
                rootA / "shows" / "sh010.mov",
                rootB / "shows" / "sh010.mov",
                temp / "elsewhere" / "sh020.mov" })
            {
                std::ofstream(file) << "x";
            }

            const std::string inside = ftk::fromFileSystem(rootA / "shows" / "sh010.mov");
            media = models::syncMediaFromPath(inside, rootA);
            FTK_CHECK("shows/sh010.mov" == media.path);
            FTK_CHECK(models::reviewGenericPath(inside) == media.pathAbsolute);
            FTK_CHECK("sh010.mov" == media.name);

            bool exists = false;
            auto resolved = models::resolveSyncMedia(media, rootB, exists);
            FTK_CHECK(exists);
            FTK_CHECK(resolved.lexically_normal() == (rootB / "shows" / "sh010.mov").lexically_normal());
            // Without a folder of its own, a machine tries the path as it was.
            resolved = models::resolveSyncMedia(media, std::filesystem::path(), exists);
            FTK_CHECK(exists);
            FTK_CHECK(resolved.lexically_normal() == (rootA / "shows" / "sh010.mov").lexically_normal());

            const std::string outside = ftk::fromFileSystem(temp / "elsewhere" / "sh020.mov");
            media = models::syncMediaFromPath(outside, rootA);
            FTK_CHECK(media.path.empty());
            resolved = models::resolveSyncMedia(media, rootB, exists);
            FTK_CHECK(exists);

            // Nowhere to be found: the path that was expected, for the
            // message to show.
            media.path = "shows/missing.mov";
            media.pathAbsolute.clear();
            resolved = models::resolveSyncMedia(media, rootB, exists);
            FTK_CHECK(!exists);
            FTK_CHECK(resolved.lexically_normal() == (rootB / "shows" / "missing.mov").lexically_normal());

            std::error_code ec;
            std::filesystem::remove_all(temp, ec);

            // The merger: a session without media is given the file open
            // here when its state arrives; one with media keeps it.
            auto model = models::MarkersModel::create();
            auto merger = models::SyncMerger::create(model, "me");
            FTK_CHECK(!merger->shareMedia().has_value());
            models::SyncMedia local;
            local.path = "shows/local.mov";
            local.name = "local.mov";
            merger->setLocalMedia(local);
            nlohmann::json snapshot;
            snapshot["rev"] = 1;
            snapshot["markers"] = nlohmann::json::array();
            snapshot["deleted"] = nlohmann::json::array();
            snapshot["media"] = nullptr;
            auto ops = merger->snapshot(snapshot);
            FTK_CHECK(1 == ops.size());
            FTK_CHECK(models::SyncOp::Type::PutMedia == ops[0].type);
            FTK_CHECK(local == ops[0].media);
            // The session's media is what the server says, once it says it.
            FTK_CHECK(!merger->getMedia().has_value());

            models::SyncMedia theirs;
            theirs.pathAbsolute = "//nas/media/theirs.mov";
            theirs.name = "theirs.mov";
            snapshot["media"] = theirs;
            ops = merger->snapshot(snapshot);
            FTK_CHECK(ops.empty());
            FTK_CHECK(merger->getMedia() == theirs);

            // Changes to it arrive as events, ours and everyone else's.
            nlohmann::json event;
            event["type"] = "media";
            event["rev"] = 2;
            event["clientId"] = "me";
            event["media"] = local;
            FTK_CHECK(merger->event(event));
            FTK_CHECK(merger->getMedia() == local);
            event["rev"] = 3;
            event["clientId"] = "them";
            event["media"] = nullptr;
            FTK_CHECK(merger->event(event));
            FTK_CHECK(!merger->getMedia().has_value());
            FTK_CHECK(model->getMarkers().empty());

            const auto share = merger->shareMedia();
            FTK_CHECK(share.has_value());
            FTK_CHECK(local == share->media);
            _print("  media -> ok");
        }

        void SyncModelTest::_merge()
        {
            auto model = models::MarkersModel::create();
            auto merger = models::SyncMerger::create(model, "me");

            // What this client has before it joins: a marker of its own, one
            // the session also has with other words, and one the session
            // has deleted.
            model->upsert(makeMarker("local", "Only here."));
            model->upsert(makeMarker("shared", "Old words."));
            model->upsert(makeMarker("gone", "Deleted elsewhere."));

            // Nothing is sent before the session state is in.
            FTK_CHECK(merger->localChanged().empty());
            FTK_CHECK(!merger->hasSnapshot());

            nlohmann::json snapshot;
            snapshot["rev"] = 7;
            snapshot["markers"] = { makeMarker("shared", "New words."), makeMarker("theirs", "From the session.") };
            snapshot["deleted"] = { "gone" };
            auto ops = merger->snapshot(snapshot);
            FTK_CHECK(merger->hasSnapshot());
            FTK_CHECK(7 == merger->getRevision());
            FTK_CHECK(3 == model->getMarkers().size());
            FTK_CHECK("New words." == findMarker(model, "shared")->text);
            FTK_CHECK(findMarker(model, "theirs"));
            FTK_CHECK(!findMarker(model, "gone"));
            // Only the marker the session lacks is sent.
            FTK_CHECK(1 == ops.size());
            FTK_CHECK(models::SyncOp::Type::Put == ops[0].type);
            FTK_CHECK("local" == ops[0].id);
            FTK_CHECK("Only here." == ops[0].marker["text"]);

            // Applying what the server said is not a change to send back.
            FTK_CHECK(merger->localChanged().empty());

            // Our own change coming back changes nothing; after it, a change
            // from someone else applies.
            FTK_CHECK(merger->event(upsertEvent(8, "me", makeMarker("local", "Only here."))));
            FTK_CHECK(merger->event(upsertEvent(9, "them", makeMarker("local", "Edited by them."))));
            FTK_CHECK("Edited by them." == findMarker(model, "local")->text);
            FTK_CHECK(9 == merger->getRevision());
            FTK_CHECK(merger->localChanged().empty());

            // A change made here is sent. Someone else's change to the same
            // marker that the server took first is skipped: ours comes after
            // it and wins.
            model->update("theirs", "Edited by me.");
            ops = merger->localChanged();
            FTK_CHECK(1 == ops.size());
            FTK_CHECK(models::SyncOp::Type::Put == ops[0].type);
            FTK_CHECK("theirs" == ops[0].id);
            FTK_CHECK(merger->event(upsertEvent(10, "them", makeMarker("theirs", "Edited by them first."))));
            FTK_CHECK("Edited by me." == findMarker(model, "theirs")->text);
            FTK_CHECK(merger->event(upsertEvent(11, "me", *findMarker(model, "theirs"))));
            FTK_CHECK("Edited by me." == findMarker(model, "theirs")->text);

            // An event already applied is ignored.
            FTK_CHECK(merger->event(upsertEvent(11, "them", makeMarker("theirs", "Stale."))));
            FTK_CHECK("Edited by me." == findMarker(model, "theirs")->text);

            // A deletion here is sent, and one from elsewhere applies.
            model->remove("shared");
            ops = merger->localChanged();
            FTK_CHECK(1 == ops.size());
            FTK_CHECK(models::SyncOp::Type::Delete == ops[0].type);
            FTK_CHECK("shared" == ops[0].id);
            FTK_CHECK(merger->event(deleteEvent(12, "me", "shared")));
            FTK_CHECK(merger->event(deleteEvent(13, "them", "local")));
            FTK_CHECK(!findMarker(model, "local"));
            FTK_CHECK(merger->localChanged().empty());

            // A change the server would not take is no longer waited for.
            model->update("theirs", "Refused.");
            ops = merger->localChanged();
            FTK_CHECK(1 == ops.size());
            merger->dropped(ops[0]);
            FTK_CHECK(merger->event(upsertEvent(14, "them", makeMarker("theirs", "Theirs again."))));
            FTK_CHECK("Theirs again." == findMarker(model, "theirs")->text);

            // The server no longer has the changes: read the state again.
            nlohmann::json reset;
            reset["type"] = "reset";
            reset["rev"] = 20;
            FTK_CHECK(!merger->event(reset));
            _print("  merge -> ok");
        }

        void SyncModelTest::_connect()
        {
            auto model = models::MarkersModel::create();
            auto sync = models::SyncModel::create(_context, model);
            std::vector<models::SyncState> states;
            auto observer = ftk::Observer<models::SyncStatus>::create(
                sync->observeStatus(),
                [&states](const models::SyncStatus& value) { states.push_back(value.state); });

            // What cannot work is refused before anything starts.
            bool thrown = false;
            try
            {
                sync->connect(models::ReviewSync{ "http://localhost:8787", "a b" }, std::string());
            }
            catch (const std::exception&)
            {
                thrown = true;
            }
            FTK_CHECK(thrown);
            thrown = false;
            try
            {
                sync->connect(models::ReviewSync{ "https://example.com", "sh010" }, std::string());
            }
            catch (const std::exception&)
            {
                thrown = true;
            }
            FTK_CHECK(thrown);
            FTK_CHECK(!sync->getSync().has_value());

            // A server that is not there: the model connects, keeps trying,
            // and leaves cleanly. The markers are untouched.
            model->upsert(makeMarker("m1", "Kept."));
            sync->connect(models::ReviewSync{ "http://127.0.0.1:9", "sh010" }, std::string());
            FTK_CHECK(sync->getSync().has_value());
            FTK_CHECK(models::SyncState::Connecting == sync->observeStatus()->get().state);
            sync->disconnect();

            // A server that does not answer at all, not even to refuse:
            // leaving does not wait long for it, since leaving is what
            // closing a review does.
            sync->connect(models::ReviewSync{ "http://10.255.255.1:8787", "sh010" }, std::string());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const auto leaveStart = std::chrono::steady_clock::now();
            sync->disconnect();
            const auto leave = std::chrono::steady_clock::now() - leaveStart;
            _print(ftk::Format("  leaving an unanswering server took {0} ms").
                arg(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(leave).count())));
            FTK_CHECK(leave < std::chrono::milliseconds(2500));
            FTK_CHECK(!sync->getSync().has_value());
            FTK_CHECK(models::SyncState::Disconnected == sync->observeStatus()->get().state);
            FTK_CHECK(1 == model->getMarkers().size());
            _print("  connect/disconnect -> ok");
        }

        void SyncModelTest::_live()
        {
            // Two people in one session, against a real server: given by
            // DJV_SYNC_TEST_SERVER (e.g. http://127.0.0.1:8787), and skipped
            // without one, since the server is a separate program.
            std::string server;
            if (!ftk::getEnv("DJV_SYNC_TEST_SERVER", server) || server.empty())
            {
                _print("  live: skipped, DJV_SYNC_TEST_SERVER is not set");
                return;
            }
            const std::string session = "test-" + models::generateId();

            // Run the timers until the condition holds, or give up.
            auto waitFor = [this](const std::function<bool()>& condition)
            {
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!condition() && std::chrono::steady_clock::now() < end)
                {
                    _context->tick();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return condition();
            };
            auto connected = [](const std::shared_ptr<models::SyncModel>& sync)
            {
                return models::SyncState::Connected == sync->observeStatus()->get().state;
            };

            // The first to join brings a marker into the empty session.
            auto modelA = models::MarkersModel::create();
            modelA->upsert(makeMarker("before", "Made before joining."));
            auto syncA = models::SyncModel::create(_context, modelA);
            syncA->connect(models::ReviewSync{ server, session }, std::string());
            FTK_CHECK(waitFor([&] { return connected(syncA); }));

            // The second gets it when joining.
            auto modelB = models::MarkersModel::create();
            auto syncB = models::SyncModel::create(_context, modelB);
            syncB->connect(models::ReviewSync{ server, session }, std::string());
            FTK_CHECK(waitFor([&] { return connected(syncB) && findMarker(modelB, "before"); }));

            // Changes go both ways, live: an addition, an edit, a removal.
            const std::string id = modelA->add(
                OTIO_NS::TimeRange(
                    OTIO_NS::RationalTime(12.0, 24.0),
                    OTIO_NS::RationalTime(1.0, 24.0)),
                std::string(),
                "From A.");
            FTK_CHECK(waitFor([&] { return findMarker(modelB, id); }));
            FTK_CHECK(*findMarker(modelB, id) == *findMarker(modelA, id));

            modelB->update(id, "Edited by B.");
            FTK_CHECK(waitFor([&]
                {
                    const auto marker = findMarker(modelA, id);
                    return marker && "Edited by B." == marker->text;
                }));

            modelA->remove("before");
            FTK_CHECK(waitFor([&] { return !findMarker(modelB, "before"); }));

            // Both edit the same marker at once: whichever the server took
            // last, both end with it.
            modelA->update(id, "A at the same time.");
            modelB->update(id, "B at the same time.");
            FTK_CHECK(waitFor([&]
                {
                    const auto a = findMarker(modelA, id);
                    const auto b = findMarker(modelB, id);
                    return a && b && a->text == b->text;
                }));
            FTK_CHECK(modelA->getMarkers() == modelB->getMarkers());

            // The media one shares reaches the other.
            models::SyncMedia media;
            media.path = "shows/sh010.mov";
            media.name = "sh010.mov";
            FTK_CHECK(!syncA->shareMedia());
            syncA->setLocalMedia(media);
            FTK_CHECK(syncA->shareMedia());
            FTK_CHECK(waitFor([&] { return syncB->observeMedia()->get() == media; }));
            FTK_CHECK(syncA->observeMedia()->get() == media);

            syncA->disconnect();
            syncB->disconnect();
            _print(ftk::Format("  live against {0} -> ok").arg(server));
        }
    }
}
