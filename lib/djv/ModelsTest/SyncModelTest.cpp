// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/ModelsTest/SyncModelTest.h>

#include <djv/ModelsTest/ModelsTestUtil.h>

#include <djv/Models/Credentials.h>
#include <djv/Models/DeepLink.h>
#include <djv/Models/LanModel.h>
#include <djv/Models/MarkersModel.h>
#include <djv/Models/NetworkDrive.h>
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
            _lan();
            _merge();
            _connect();
            _deepLink();
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
            models::ReviewSettings review;
            review.showNotes = false;
            review.noteDuration = 4.F;
            nlohmann::json json = review;
            FTK_CHECK(json.get<models::ReviewSettings>() == review);

            models::SyncSettings sync;
            FTK_CHECK(models::isValidSyncClientId(sync.clientId));
            FTK_CHECK("http://192.168.1.40:8877" == sync.server);
            FTK_CHECK(sync.username.empty());
            sync.server = "http://10.0.0.5:8787";
            sync.username = "anna";
            sync.clientId = "edit-bay-1";
            sync.driveLetter = "S";
            sync.shareUnc = "\\\\10.0.0.5\\media";
            sync.mediaRoot = "//nas/media";
            sync.connectOnStart = false;
            json = sync;
            FTK_CHECK(json.get<models::SyncSettings>() == sync);

            // What is missing, or not what it should be, keeps its default
            // and does not lose the rest.
            json.erase("DriveLetter");
            json["ConnectOnStart"] = "yes";
            // A token kept by an earlier DJV is not read.
            json["Token"] = "old-secret";
            const auto partial = json.get<models::SyncSettings>();
            FTK_CHECK("edit-bay-1" == partial.clientId);
            FTK_CHECK("anna" == partial.username);
            FTK_CHECK(partial.driveLetter.empty());
            FTK_CHECK(partial.connectOnStart);

            // The sync settings were kept with the review settings before
            // they had a section of their own; they move across.
            auto settings = createTestSettings(_context);
            nlohmann::json older;
            older["ShowNotes"] = true;
            older["NoteDuration"] = 2.5F;
            older["SyncServer"] = "http://review:9000";
            older["SyncToken"] = "old-secret";
            older["SyncMediaRoot"] = "//nas/old";
            settings->set("/Review.1", older);
            auto moved = models::readSyncSettings(settings);
            FTK_CHECK("http://review:9000" == moved.server);
            FTK_CHECK(moved.username.empty());
            FTK_CHECK("//nas/old" == moved.mediaRoot);
            FTK_CHECK(models::isValidSyncClientId(moved.clientId));

            // Once the section is there, it is what is read.
            settings->setT("/Sync.1", sync);
            FTK_CHECK(models::readSyncSettings(settings) == sync);

            // Resetting the settings keeps this PC's place on the network,
            // and only that.
            const auto reset = models::resetSyncSettings(sync);
            FTK_CHECK("edit-bay-1" == reset.clientId);
            FTK_CHECK("anna" == reset.username);
            FTK_CHECK("S" == reset.driveLetter);
            FTK_CHECK(sync.server == reset.server);
            FTK_CHECK(sync.shareUnc == reset.shareUnc);
            FTK_CHECK(reset.mediaRoot.empty());
            FTK_CHECK(reset.connectOnStart);
            _print("  review and sync settings -> ok");
        }

        void SyncModelTest::_lan()
        {
            FTK_CHECK(models::isValidSyncClientId("edit-bay-1"));
            FTK_CHECK(models::isValidSyncClientId("Grading.Suite_2"));
            FTK_CHECK(!models::isValidSyncClientId(""));
            FTK_CHECK(!models::isValidSyncClientId(".."));
            FTK_CHECK(!models::isValidSyncClientId("edit bay"));
            FTK_CHECK(!models::isValidSyncClientId(std::string(65, 'a')));
            FTK_CHECK(models::isValidSyncUser("anna.k"));
            FTK_CHECK(!models::isValidSyncUser("anna k"));

            FTK_CHECK("S" == models::normalizeDriveLetter("s"));
            FTK_CHECK("S" == models::normalizeDriveLetter("S:"));
            FTK_CHECK("S" == models::normalizeDriveLetter("s:\\"));
            FTK_CHECK(models::normalizeDriveLetter("").empty());
            FTK_CHECK(models::normalizeDriveLetter("SS").empty());
            FTK_CHECK(models::normalizeDriveLetter("1:").empty());

            FTK_CHECK(models::isSameUncPath("\\\\10.0.0.5\\Media\\", "//10.0.0.5/media"));
            FTK_CHECK(!models::isSameUncPath("\\\\10.0.0.5\\media", "\\\\10.0.0.6\\media"));
            FTK_CHECK(!models::isSameUncPath("", ""));

            // The share's account is the server's own unless it says
            // otherwise, so that a PC in a domain does not look it up there.
            FTK_CHECK("192.168.1.40" == models::getUncHost("\\\\192.168.1.40\\media"));
            FTK_CHECK("nas" == models::getUncHost("//nas/media/shots"));
            FTK_CHECK(models::getUncHost("C:\\media").empty());
            FTK_CHECK("192.168.1.40\\djv" == models::shareAccountName("djv", "\\\\192.168.1.40\\media"));
            FTK_CHECK("STUDIO\\djv" == models::shareAccountName("STUDIO\\djv", "\\\\192.168.1.40\\media"));
            FTK_CHECK("djv@studio.local" == models::shareAccountName("djv@studio.local", "\\\\nas\\media"));
            FTK_CHECK(models::shareAccountName("", "\\\\nas\\media").empty());

            // A path not on a mapped drive is left as it is.
            FTK_CHECK("relative/a.mov" == models::toUncPath("relative/a.mov"));
#if defined(_WIN32)
            // A letter that is free is not a network drive.
            const auto free = models::getFreeDriveLetters();
            if (!free.empty())
            {
                FTK_CHECK(models::getDriveConnection(free.back()).empty());
                const std::string path = free.back() + ":\\shots\\a.mov";
                FTK_CHECK(path == models::toUncPath(path));
            }
            // A letter in use by a disk is not taken over.
            const auto refused = models::mapNetworkDrive("C", "\\\\10.255.255.1\\media");
            FTK_CHECK(!refused.ok);
            FTK_CHECK(!refused.message.empty());
#else // _WIN32
            FTK_CHECK(!models::isNetworkDriveSupported());
            FTK_CHECK(!models::mapNetworkDrive("S", "//server/media").ok);
#endif // _WIN32

            // What the server says, read: to a signed-in user, where the
            // media is and how to sign in to it.
            nlohmann::json hello;
            hello["name"] = "Dev review server";
            hello["serverVersion"] = "2.0.0";
            hello["djvVersion"] = "3.7.5";
            hello["authRequired"] = true;
            hello["signedIn"] = true;
            hello["user"] = { { "username", "anna" }, { "name", "Anna" }, { "admin", false } };
            hello["storage"] = { { "unc", "\\\\192.168.1.40\\media" }, { "user", "djv" }, { "password", "share-password" } };
            auto info = models::parseSyncServerInfo(hello);
            FTK_CHECK(models::SyncServerState::Ok == info.state);
            FTK_CHECK("Dev review server" == info.name);
            FTK_CHECK("2.0.0" == info.serverVersion);
            FTK_CHECK("\\\\192.168.1.40\\media" == info.storageUnc);
            FTK_CHECK("djv" == info.shareUser);
            FTK_CHECK("share-password" == info.sharePassword);
            FTK_CHECK("3.7.5" == info.djvVersion);
            FTK_CHECK("anna" == info.username);
            FTK_CHECK("Anna" == info.userFullName);
            FTK_CHECK(info.serverId.empty());
            hello["serverId"] = "0f6e1c1e-7d1a-4f4e-9a55-3b0d9b8d2c11";
            FTK_CHECK("0f6e1c1e-7d1a-4f4e-9a55-3b0d9b8d2c11" == models::parseSyncServerInfo(hello).serverId);
            // Not signed in: nothing about the share, and what to do.
            nlohmann::json anonymous;
            anonymous["name"] = "Dev review server";
            anonymous["authRequired"] = true;
            anonymous["signedIn"] = false;
            info = models::parseSyncServerInfo(anonymous);
            FTK_CHECK(models::SyncServerState::SignedOut == info.state);
            FTK_CHECK(info.storageUnc.empty());
            FTK_CHECK(models::syncSignedOutMessage() == info.message);
            // A server from before users asks nobody to sign in.
            FTK_CHECK(models::SyncServerState::Ok == models::parseSyncServerInfo(nlohmann::json()).state);

            // A server that is not there.
            info = models::fetchSyncServerInfo("http://127.0.0.1:9", std::string(), models::SyncIdentity{ "edit-bay-1", "3.7.5" });
            FTK_CHECK(models::SyncServerState::Unreachable == info.state);
            FTK_CHECK(models::SyncServerState::Error == models::fetchSyncServerInfo("", "", {}).state);
            FTK_CHECK(models::SyncServerState::Error == models::fetchSyncServerInfo("ftp://x", "", {}).state);

            // Signing in to a server that is not there, or with nothing to
            // sign in with.
            auto login = models::syncLogin("http://127.0.0.1:9", "anna", "anna-password", {});
            FTK_CHECK(!login.ok);
            FTK_CHECK(login.unreachable);
            login = models::syncLogin("http://127.0.0.1:9", "", "anna-password", {});
            FTK_CHECK(!login.ok);
            FTK_CHECK(!login.unreachable);
            FTK_CHECK(!models::syncLogin("http://127.0.0.1:9", "anna", "", {}).ok);
            FTK_CHECK(!models::syncLogin("ftp://x", "anna", "anna-password", {}).ok);

            // The password kept, read back, and forgotten.
            const std::string target = "http://djv-test-" + models::generateId().substr(0, 8);
            FTK_CHECK(models::loadSyncPassword(target, "anna").empty());
            if (models::isCredentialStoreSupported())
            {
                std::string error;
                FTK_CHECK(models::saveSyncPassword(target, "anna", "p\xc3\xa4ssword-1", &error));
                FTK_CHECK("p\xc3\xa4ssword-1" == models::loadSyncPassword(target, "anna"));
                FTK_CHECK(models::loadSyncPassword(target, "ben").empty());
                models::forgetSyncPassword(target, "anna");
                FTK_CHECK(models::loadSyncPassword(target, "anna").empty());
            }
            else
            {
                FTK_CHECK(!models::saveSyncPassword(target, "anna", "x"));
            }

            // The model: without a drive letter there is nothing to map, and
            // the media is found in the media folder of the settings.
            models::SyncSettings sync;
            sync.server = "http://127.0.0.1:9";
            sync.mediaRoot = "//nas/media";
            sync.driveLetter.clear();
            auto syncSettings = ftk::Observable<models::SyncSettings>::create(sync);
            auto lan = models::LanModel::create(
                _context,
                syncSettings,
                [syncSettings](const models::SyncSettings& value) { syncSettings->setIfChanged(value); },
                "3.7.5");
            FTK_CHECK("3.7.5" == lan->getIdentity().version);
            FTK_CHECK(sync.clientId == lan->getIdentity().clientId);
            FTK_CHECK("//nas/media" == lan->observeMediaRoot()->get());
            lan->mapDrive();
            FTK_CHECK(models::LanDriveState::None == lan->observeDrive()->get().state);

            // A server that cannot be reached is reported, and the share last
            // named is what would be mapped. With no password kept for the
            // user, DJV is not signed in, and says so.
            sync.shareUnc = "\\\\10.255.255.1\\media";
            sync.username = "djv-test-nobody-" + models::generateId().substr(0, 8);
            syncSettings->setIfChanged(sync);
            lan->start();
            FTK_CHECK(models::SyncServerState::Checking == lan->observeServerInfo()->get().state);
            FTK_CHECK(models::LanAuthState::SigningIn == lan->observeAuth()->get().state);
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (models::SyncServerState::Checking == lan->observeServerInfo()->get().state &&
                std::chrono::steady_clock::now() < end)
            {
                _context->tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            FTK_CHECK(models::SyncServerState::Unreachable == lan->observeServerInfo()->get().state);
            FTK_CHECK(models::LanAuthState::SignedOut == lan->observeAuth()->get().state);
            FTK_CHECK(!lan->observeAuth()->get().message.empty());
            FTK_CHECK(lan->observeAuth()->get().token.empty());
            FTK_CHECK(models::LanDriveState::None == lan->observeDrive()->get().state);

            // Signing in to a server that cannot be reached: the user is
            // kept, and DJV is not signed in, but nothing says the password
            // was wrong.
            lan->signIn("anna", "anna-password");
            FTK_CHECK("anna" == syncSettings->get().username);
            const auto end2 = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (models::LanAuthState::SigningIn == lan->observeAuth()->get().state &&
                std::chrono::steady_clock::now() < end2)
            {
                _context->tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            FTK_CHECK(models::LanAuthState::SignedOut == lan->observeAuth()->get().state);
            FTK_CHECK(models::loadSyncPassword(sync.server, "anna").empty());
            _print("  lan -> ok");
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
            FTK_CHECK(!read.compare.has_value());

            // A second video to compare with the first, as the portal makes
            // a session of two; one without a path is none.
            media.compare = models::SyncMediaFile{ "shows/abc/sh010_v2.mov", "//nas/media/shows/abc/sh010_v2.mov", "sh010_v2.mov" };
            const nlohmann::json withCompare = media;
            FTK_CHECK(withCompare.contains("compare"));
            FTK_CHECK(withCompare.get<models::SyncMedia>() == media);
            nlohmann::json emptyCompare = withCompare;
            emptyCompare["compare"] = { { "name", "nothing.mov" } };
            FTK_CHECK(!emptyCompare.get<models::SyncMedia>().compare.has_value());
            media.compare.reset();

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

        void SyncModelTest::_deepLink()
        {
            // The portal's review links.
            auto link = models::parseDeepLink("djv://session/20261012-sh010-ab12?server=http%3A%2F%2F192.168.1.40%3A8877");
            FTK_CHECK(link.has_value());
            FTK_CHECK("20261012-sh010-ab12" == link->session);
            FTK_CHECK("http://192.168.1.40:8877" == link->server);
            // As browsers and Windows may pass them on: other slashes, the
            // scheme in capitals, a trailing slash, more in the query.
            link = models::parseDeepLink("DJV:session/sh010/?x=1&server=http://localhost:8877/#top");
            FTK_CHECK(link.has_value());
            FTK_CHECK("sh010" == link->session);
            FTK_CHECK("http://localhost:8877/" == link->server);
            // The link of the first try of the portal, which has no
            // autoConnect: it joins, as one that says true does.
            link = models::parseDeepLink("djv://session/review-d87fe0c3?server=http%3A%2F%2Flocalhost%3A8877");
            FTK_CHECK(link.has_value());
            FTK_CHECK("review-d87fe0c3" == link->session);
            FTK_CHECK("http://localhost:8877" == link->server);
            FTK_CHECK(link->autoConnect);
            for (const std::string& yes : { "true", "TRUE", "1", "yes", "on" })
            {
                link = models::parseDeepLink("djv://session/sh010?server=http://a&autoConnect=" + yes);
                FTK_CHECK(link.has_value() && link->autoConnect);
            }
            for (const std::string& no : { "false", "False", "0", "no", "off" })
            {
                link = models::parseDeepLink("djv://session/sh010?autoconnect=" + no + "&server=http://a");
                FTK_CHECK(link.has_value() && !link->autoConnect);
            }
            std::string error;
            FTK_CHECK(!models::parseDeepLink("djv://session/sh010?server=http://a&autoConnect=maybe", &error).has_value());
            FTK_CHECK(error.find("maybe") != std::string::npos);
            FTK_CHECK(!models::parseDeepLink("djv://session/sh010?server=http://a&autoConnect", &error).has_value());
            for (const std::string& bad : {
                "http://example.com/session/sh010",
                "djv://open/sh010?server=http://a",
                "djv://session/?server=http://a",
                "djv://session/a%2F..%2Fb?server=http://a",
                "djv://session/sh010",
                "djv://session/sh010?server=https://a",
                "djv://session/sh010?server=file:///c:/x" })
            {
                error.clear();
                FTK_CHECK(!models::parseDeepLink(bad, &error).has_value());
                FTK_CHECK(!error.empty());
            }
            FTK_CHECK(models::isDeepLink("djv://x"));
            FTK_CHECK(!models::isDeepLink("C:/media/djv.mov"));

            // The link must be for the server in the settings, however it is
            // written.
            FTK_CHECK(models::isSameSyncServer("http://192.168.1.40:8877", "HTTP://192.168.1.40:8877/"));
            FTK_CHECK(models::isSameSyncServer("http://Server", "http://server:80"));
            FTK_CHECK(!models::isSameSyncServer("http://192.168.1.40:8877", "http://192.168.1.41:8877"));
            FTK_CHECK(!models::isSameSyncServer("http://192.168.1.40:8877", "http://192.168.1.40:8878"));
            FTK_CHECK(!models::isSameSyncServer("http://localhost:8877", "http://127.0.0.1:8877"));

#if defined(_WIN32)
            // A second DJV hands the link to the first, on a pipe of its own
            // here so as not to meet a DJV that is running.
            const std::string pipe = "\\\\.\\pipe\\djv-link-test-" + models::generateId();
            FTK_CHECK(!models::sendDeepLink("djv://session/nobody?server=http://a", pipe));
            std::vector<std::string> received;
            {
                auto first = models::DeepLinkListener::create(_context, pipe);
                FTK_CHECK(first->isListening());
                first->setCallback([&received](const std::string& value) { received.push_back(value); });
                // Only the first one listens.
                auto second = models::DeepLinkListener::create(_context, pipe);
                FTK_CHECK(!second->isListening());

                const std::string sent = "djv://session/sh010?server=http%3A%2F%2F192.168.1.40%3A8877";
                FTK_CHECK(models::sendDeepLink(sent, pipe));
                FTK_CHECK(models::sendDeepLink(sent + "-again", pipe));
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (received.size() < 2 && std::chrono::steady_clock::now() < end)
                {
                    _context->tick();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                FTK_CHECK(2 == received.size());
                FTK_CHECK(sent == received[0]);
                FTK_CHECK(sent + "-again" == received[1]);
            }
            // Gone with the listener.
            FTK_CHECK(!models::sendDeepLink("djv://session/sh010?server=http://a", pipe));
            _print(ftk::Format("  review links: handled by \"{0}\"").
                arg(ftk::fromFileSystem(models::getDeepLinkHandler())));
#endif // _WIN32
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

            // The server says who it is, and asks this DJV to sign in: as
            // the user DJV_SYNC_TEST_USER, with DJV_SYNC_TEST_PASSWORD,
            // which the admin adds on the server's admin page.
            const models::SyncIdentity identity{ "djv-test-" + models::generateId().substr(0, 8), "test" };
            auto info = models::fetchSyncServerInfo(server, std::string(), identity);
            FTK_CHECK(
                models::SyncServerState::Ok == info.state ||
                models::SyncServerState::SignedOut == info.state);
            // The server under another of its addresses is the same server,
            // which is how a review link from a portal opened at another
            // address is told apart from one for another server.
            if (!info.serverId.empty())
            {
                std::string other = server;
                for (const auto& [a, b] : std::vector<std::pair<std::string, std::string> >{
                    { "127.0.0.1", "localhost" }, { "localhost", "127.0.0.1" } })
                {
                    const size_t i = other.find(a);
                    if (i != std::string::npos)
                    {
                        other.replace(i, a.size(), b);
                        break;
                    }
                }
                if (other != server)
                {
                    const auto otherInfo = models::fetchSyncServerInfo(other, std::string(), identity);
                    FTK_CHECK(otherInfo.serverId == info.serverId);
                    _print(ftk::Format("  live: {0} and {1} are the same server, {2}").
                        arg(server).
                        arg(other).
                        arg(info.serverId));
                }
            }
            else
            {
                _print("  live: the server gives no identity (older than 2.1.1)");
            }
            std::string token;
            if (info.authRequired)
            {
                std::string user;
                std::string password;
                ftk::getEnv("DJV_SYNC_TEST_USER", user);
                ftk::getEnv("DJV_SYNC_TEST_PASSWORD", password);
                FTK_CHECK(!models::syncLogin(server, user.empty() ? "nobody" : user, "not-the-password", identity).ok);
                if (user.empty() || password.empty())
                {
                    _print("  live: the server wants users to sign in, and DJV_SYNC_TEST_USER and "
                        "DJV_SYNC_TEST_PASSWORD are not set; the rest is skipped");
                    return;
                }
                const auto login = models::syncLogin(server, user, password, identity);
                FTK_CHECK(login.ok);
                FTK_CHECK(user == login.username);
                token = login.token;
                info = models::fetchSyncServerInfo(server, token, identity);
                FTK_CHECK(models::SyncServerState::Ok == info.state);
                FTK_CHECK(user == info.username);
                _print(ftk::Format("  live: signed in as {0}; the share is \"{1}\", mapped as {2}").
                    arg(user).
                    arg(info.storageUnc).
                    arg(info.shareUser));

                // Without the token, the session is refused, and the reason
                // is the one App acts on.
                auto modelX = models::MarkersModel::create();
                auto syncX = models::SyncModel::create(_context, modelX);
                syncX->connect(models::ReviewSync{ server, session }, std::string(), identity);
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (syncX->observeStatus()->get().state != models::SyncState::Error &&
                    std::chrono::steady_clock::now() < end)
                {
                    _context->tick();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                FTK_CHECK(models::syncSignedOutMessage() == syncX->observeStatus()->get().message);
                syncX->disconnect();
            }

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
            syncA->connect(models::ReviewSync{ server, session }, token, identity);
            FTK_CHECK(waitFor([&] { return connected(syncA); }));

            // The second gets it when joining.
            auto modelB = models::MarkersModel::create();
            auto syncB = models::SyncModel::create(_context, modelB);
            syncB->connect(models::ReviewSync{ server, session }, token, identity);
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

            // And so does a second video to compare it with.
            media.compare = models::SyncMediaFile{ "shows/sh010_v2.mov", std::string(), "sh010_v2.mov" };
            syncA->setLocalMedia(media);
            FTK_CHECK(syncA->shareMedia());
            FTK_CHECK(waitFor([&] { return syncB->observeMedia()->get() == media; }));

            syncA->disconnect();
            syncB->disconnect();
            _print(ftk::Format("  live against {0} -> ok").arg(server));
        }
    }
}
