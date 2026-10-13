// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/SyncModel.h>

#include <djv/Models/MarkersModel.h>
#include <djv/Models/NetworkDrive.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/LogSystem.h>
#include <ftk/Core/Path.h>
#include <ftk/Core/Timer.h>

// The client brings in the Windows headers, so it stays in this file: the
// macros they define would otherwise reach everything that includes the
// model.
#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif // NOMINMAX
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#endif // _WIN32
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace djv
{
    namespace models
    {
        bool isValidSyncSession(const std::string& value)
        {
            if (value.empty() || value.size() > 128)
            {
                return false;
            }
            // Not "." or "..": the server keeps a session in a file named
            // after it.
            if ("." == value || ".." == value)
            {
                return false;
            }
            return std::all_of(
                value.begin(),
                value.end(),
                [](char c)
                {
                    return
                        (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        '.' == c || '_' == c || '-' == c;
                });
        }

        bool isValidSyncClientId(const std::string& value)
        {
            if (value.empty() || value.size() > 64 || "." == value || ".." == value)
            {
                return false;
            }
            return std::all_of(
                value.begin(),
                value.end(),
                [](char c)
                {
                    return
                        (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        '.' == c || '_' == c || '-' == c;
                });
        }

        bool isValidSyncUser(const std::string& value)
        {
            // The same rules: a user name goes into the same URLs and logs.
            return isValidSyncClientId(value);
        }

        std::string syncSignedOutMessage()
        {
            return "Not signed in to the sync server; sign in in the Sync Server settings.";
        }

        namespace
        {
            std::string jsonText(const nlohmann::json& object, const char* key)
            {
                if (!object.is_object())
                {
                    return std::string();
                }
                const auto i = object.find(key);
                return i != object.end() && i->is_string() ? i->get<std::string>() : std::string();
            }

            bool jsonFlag(const nlohmann::json& object, const char* key)
            {
                if (!object.is_object())
                {
                    return false;
                }
                const auto i = object.find(key);
                return i != object.end() && i->is_boolean() && i->get<bool>();
            }

            nlohmann::json jsonObject(const nlohmann::json& object, const char* key)
            {
                if (object.is_object())
                {
                    const auto i = object.find(key);
                    if (i != object.end() && i->is_object())
                    {
                        return *i;
                    }
                }
                return nlohmann::json::object();
            }
        }

        SyncServerInfo parseSyncServerInfo(const nlohmann::json& json)
        {
            SyncServerInfo out;
            const nlohmann::json storage = jsonObject(json, "storage");
            const nlohmann::json user = jsonObject(json, "user");
            out.name = jsonText(json, "name");
            out.serverVersion = jsonText(json, "serverVersion");
            out.serverId = jsonText(json, "serverId");
            out.djvVersion = jsonText(json, "djvVersion");
            out.authRequired = jsonFlag(json, "authRequired");
            out.username = jsonText(user, "username");
            out.userFullName = jsonText(user, "name");
            out.storageUnc = jsonText(storage, "unc");
            out.shareUser = jsonText(storage, "user");
            out.sharePassword = jsonText(storage, "password");
            if (out.authRequired && !jsonFlag(json, "signedIn"))
            {
                out.state = SyncServerState::SignedOut;
                out.message = syncSignedOutMessage();
            }
            else
            {
                out.state = SyncServerState::Ok;
            }
            return out;
        }

        SyncMedia syncMediaFromPath(
            const std::string& path,
            const std::filesystem::path& mediaRoot)
        {
            SyncMedia out;
            const std::filesystem::path fsPath = ftk::toFileSystem(path);
            // On a mapped drive, the share it maps: a PC that mapped the
            // share to another letter, or not at all, still finds the file.
            out.pathAbsolute = reviewGenericPath(toUncPath(path));
            out.name = ftk::fromFileSystem(fsPath.filename());
            out.sharedBy = reviewAuthor();
            if (!path.empty() && !mediaRoot.empty())
            {
                // Through the file system where it can be, so that a mapped
                // drive and the share it maps, or two spellings of one
                // folder, still meet; by the letters where it cannot.
                std::error_code ec;
                std::filesystem::path rel = std::filesystem::relative(fsPath, mediaRoot, ec);
                if (ec || rel.empty())
                {
                    rel = fsPath.lexically_normal().lexically_relative(mediaRoot.lexically_normal());
                }
                // Only a file inside the folder: one beside it, reached
                // through "..", is somewhere the other machines' folders do
                // not lead.
                if (!rel.empty() &&
                    rel.is_relative() &&
                    *rel.begin() != std::filesystem::path("..") &&
                    rel != std::filesystem::path("."))
                {
                    out.path = ftk::fromFileSystemGeneric(rel);
                }
            }
            return out;
        }

        std::filesystem::path resolveSyncMedia(
            const SyncMedia& media,
            const std::filesystem::path& mediaRoot,
            bool& exists)
        {
            // Without a folder of its own, this machine has nothing to join
            // the relative path to.
            return resolveReviewPath(
                mediaRoot.empty() ? std::string() : media.path,
                media.pathAbsolute,
                mediaRoot,
                std::filesystem::path(),
                ftk::PathOptions(),
                exists);
        }

        std::filesystem::path resolveSyncMedia(
            const SyncMediaFile& file,
            const std::filesystem::path& mediaRoot,
            bool& exists)
        {
            SyncMedia media;
            media.path = file.path;
            media.pathAbsolute = file.pathAbsolute;
            return resolveSyncMedia(media, mediaRoot, exists);
        }

        void to_json(nlohmann::json& json, const SyncMedia& in)
        {
            json = nlohmann::json::object();
            json["path"] = in.path;
            json["pathAbsolute"] = in.pathAbsolute;
            json["name"] = in.name;
            json["sharedBy"] = in.sharedBy;
            if (in.compare.has_value())
            {
                json["compare"] = {
                    { "path", in.compare->path },
                    { "pathAbsolute", in.compare->pathAbsolute },
                    { "name", in.compare->name }
                };
            }
        }

        void from_json(const nlohmann::json& json, SyncMedia& out)
        {
            auto text = [](const nlohmann::json& json, const char* key)
            {
                const auto i = json.find(key);
                return i != json.end() && i->is_string() ? i->get<std::string>() : std::string();
            };
            out.path = text(json, "path");
            out.pathAbsolute = text(json, "pathAbsolute");
            out.name = text(json, "name");
            out.sharedBy = text(json, "sharedBy");
            out.compare.reset();
            const auto i = json.find("compare");
            if (i != json.end() && i->is_object())
            {
                SyncMediaFile file;
                file.path = text(*i, "path");
                file.pathAbsolute = text(*i, "pathAbsolute");
                file.name = text(*i, "name");
                if (!file.path.empty() || !file.pathAbsolute.empty())
                {
                    out.compare = file;
                }
            }
        }

        namespace
        {
            //! The media in a snapshot or an event, when there is any.
            std::optional<SyncMedia> readMedia(const nlohmann::json& json)
            {
                std::optional<SyncMedia> out;
                const auto i = json.find("media");
                if (i != json.end() && i->is_object())
                {
                    const SyncMedia media = i->get<SyncMedia>();
                    if (!media.path.empty() || !media.pathAbsolute.empty())
                    {
                        out = media;
                    }
                }
                return out;
            }
        }

        std::vector<std::string> parseSyncEvents(
            SyncEventStream& stream,
            const char* data,
            size_t size)
        {
            std::vector<std::string> out;
            for (size_t i = 0; i < size; ++i)
            {
                const char c = data[i];
                if (c != '\n')
                {
                    stream.line.push_back(c);
                    continue;
                }
                // The stream may use CRLF.
                if (!stream.line.empty() && '\r' == stream.line.back())
                {
                    stream.line.pop_back();
                }
                if (stream.line.empty())
                {
                    // A blank line ends the event.
                    if (stream.hasData)
                    {
                        out.push_back(stream.data);
                    }
                    stream.data.clear();
                    stream.hasData = false;
                }
                else if (0 == stream.line.compare(0, 5, "data:"))
                {
                    std::string value = stream.line.substr(5);
                    if (!value.empty() && ' ' == value.front())
                    {
                        value.erase(0, 1);
                    }
                    if (stream.hasData)
                    {
                        stream.data.push_back('\n');
                    }
                    stream.data += value;
                    stream.hasData = true;
                }
                // Anything else -- a comment, an "id:" or "event:" field --
                // carries nothing the client needs: the revision is in the
                // data.
                stream.line.clear();
            }
            return out;
        }

        struct SyncMerger::Private
        {
            std::weak_ptr<MarkersModel> markersModel;
            std::string clientId;
            int64_t revision = 0;
            bool hasSnapshot = false;

            //! The markers as the server was last told or told us, by
            //! identifier. The difference between this and the markers is
            //! what is left to send.
            std::map<std::string, ReviewMarker> known;

            //! The changes sent and not yet seen coming back, by marker.
            std::map<std::string, int> pending;

            //! The session's media, and the media this client has open.
            //! The session's is only ever what the server said: the last
            //! media it receives stands, as for a marker, so a change from
            //! here shows once it comes back.
            std::optional<SyncMedia> media;
            std::optional<SyncMedia> localMedia;
        };

        void SyncMerger::_init(
            const std::shared_ptr<MarkersModel>& markersModel,
            const std::string& clientId)
        {
            FTK_P();
            p.markersModel = markersModel;
            p.clientId = clientId;
        }

        SyncMerger::SyncMerger() :
            _p(new Private)
        {}

        SyncMerger::~SyncMerger()
        {}

        std::shared_ptr<SyncMerger> SyncMerger::create(
            const std::shared_ptr<MarkersModel>& markersModel,
            const std::string& clientId)
        {
            auto out = std::shared_ptr<SyncMerger>(new SyncMerger);
            out->_init(markersModel, clientId);
            return out;
        }

        const std::string& SyncMerger::getClientId() const
        {
            return _p->clientId;
        }

        int64_t SyncMerger::getRevision() const
        {
            return _p->revision;
        }

        bool SyncMerger::hasSnapshot() const
        {
            return _p->hasSnapshot;
        }

        std::vector<SyncOp> SyncMerger::snapshot(const nlohmann::json& json)
        {
            FTK_P();
            std::vector<SyncOp> out;
            auto markersModel = p.markersModel.lock();
            if (!markersModel)
            {
                return out;
            }

            p.revision = json.value("rev", static_cast<int64_t>(0));

            // The known state is rebuilt from the server: whatever it was
            // before, the server's word is the latest there is.
            p.known.clear();
            std::map<std::string, ReviewMarker> server;
            if (json.contains("markers") && json.at("markers").is_array())
            {
                for (const auto& item : json.at("markers"))
                {
                    try
                    {
                        const ReviewMarker marker = item.get<ReviewMarker>();
                        if (!marker.id.empty())
                        {
                            server[marker.id] = marker;
                        }
                    }
                    catch (const std::exception&)
                    {}
                }
            }
            std::map<std::string, bool> deleted;
            if (json.contains("deleted") && json.at("deleted").is_array())
            {
                for (const auto& item : json.at("deleted"))
                {
                    if (item.is_string())
                    {
                        deleted[item.get<std::string>()] = true;
                    }
                }
            }

            const auto local = markersModel->getMarkers();
            std::map<std::string, ReviewMarker> localById;
            for (const auto& marker : local)
            {
                localById[marker.id] = marker;
            }

            // A marker with a change on its way keeps the local version: the
            // change will reach the server after this state was read. Every
            // other marker is the server's. The known state is updated before
            // the model, so the change the model reports finds nothing new
            // to send.
            for (const auto& i : server)
            {
                if (p.pending.find(i.first) != p.pending.end())
                {
                    continue;
                }
                p.known[i.first] = i.second;
                const auto j = localById.find(i.first);
                if (j == localById.end() || j->second != i.second)
                {
                    markersModel->upsert(i.second);
                }
            }
            for (const auto& i : deleted)
            {
                if (p.pending.find(i.first) != p.pending.end())
                {
                    continue;
                }
                if (localById.find(i.first) != localById.end())
                {
                    markersModel->remove(i.first);
                }
            }

            // What is left differs from the server: the markers only this
            // client has, and those with a change on its way.
            p.hasSnapshot = true;
            out = _diff();

            // A session without media is given the file this client has
            // open, so whoever joins later gets the file with the markers.
            p.media = readMedia(json);
            if (!p.media.has_value() && p.localMedia.has_value())
            {
                SyncOp op;
                op.type = SyncOp::Type::PutMedia;
                op.media = p.localMedia.value();
                out.push_back(op);
            }
            return out;
        }

        bool SyncMerger::event(const nlohmann::json& json)
        {
            FTK_P();
            const std::string type = json.value("type", std::string());
            if ("reset" == type)
            {
                return false;
            }
            auto markersModel = p.markersModel.lock();
            if (!markersModel || !p.hasSnapshot)
            {
                return true;
            }
            const int64_t revision = json.value("rev", static_cast<int64_t>(0));
            if (revision <= p.revision)
            {
                // Already applied, with the state or an earlier stream.
                return true;
            }
            p.revision = revision;

            // Our own media change coming back is applied like anyone
            // else's: nothing here showed it before the server took it.
            if ("media" == type)
            {
                p.media = readMedia(json);
                return true;
            }

            std::string id;
            ReviewMarker marker;
            if ("upsert" == type)
            {
                try
                {
                    marker = json.at("marker").get<ReviewMarker>();
                    id = marker.id;
                }
                catch (const std::exception&)
                {
                    return true;
                }
            }
            else if ("delete" == type)
            {
                id = json.value("id", std::string());
            }
            if (id.empty())
            {
                return true;
            }

            // Our own change coming back: the server has it now.
            if (json.value("clientId", std::string()) == p.clientId)
            {
                const auto i = p.pending.find(id);
                if (i != p.pending.end() && --i->second <= 0)
                {
                    p.pending.erase(i);
                }
                return true;
            }

            // Someone else's change to a marker we have a change on the way
            // for: ours reaches the server after it, so ours wins.
            if (p.pending.find(id) != p.pending.end())
            {
                return true;
            }

            if ("upsert" == type)
            {
                p.known[id] = marker;
                markersModel->upsert(marker);
            }
            else
            {
                p.known.erase(id);
                markersModel->remove(id);
            }
            return true;
        }

        std::vector<SyncOp> SyncMerger::localChanged()
        {
            FTK_P();
            if (!p.hasSnapshot)
            {
                return {};
            }
            return _diff();
        }

        void SyncMerger::dropped(const SyncOp& op)
        {
            FTK_P();
            const auto i = p.pending.find(op.id);
            if (i != p.pending.end() && --i->second <= 0)
            {
                p.pending.erase(i);
            }
        }

        const std::optional<SyncMedia>& SyncMerger::getMedia() const
        {
            return _p->media;
        }

        const std::optional<SyncMedia>& SyncMerger::getLocalMedia() const
        {
            return _p->localMedia;
        }

        void SyncMerger::setLocalMedia(const std::optional<SyncMedia>& value)
        {
            _p->localMedia = value;
        }

        std::optional<SyncOp> SyncMerger::shareMedia() const
        {
            FTK_P();
            std::optional<SyncOp> out;
            if (p.localMedia.has_value())
            {
                SyncOp op;
                op.type = SyncOp::Type::PutMedia;
                op.media = p.localMedia.value();
                out = op;
            }
            return out;
        }

        std::vector<SyncOp> SyncMerger::_diff()
        {
            FTK_P();
            std::vector<SyncOp> out;
            auto markersModel = p.markersModel.lock();
            if (!markersModel)
            {
                return out;
            }
            const auto& markers = markersModel->getMarkers();
            std::map<std::string, bool> present;
            for (const auto& marker : markers)
            {
                present[marker.id] = true;
                const auto i = p.known.find(marker.id);
                if (i == p.known.end() || i->second != marker)
                {
                    SyncOp op;
                    op.type = SyncOp::Type::Put;
                    op.id = marker.id;
                    op.marker = marker;
                    out.push_back(op);
                    p.known[marker.id] = marker;
                    ++p.pending[marker.id];
                }
            }
            for (auto i = p.known.begin(); i != p.known.end();)
            {
                if (present.find(i->first) == present.end())
                {
                    SyncOp op;
                    op.type = SyncOp::Type::Delete;
                    op.id = i->first;
                    out.push_back(op);
                    ++p.pending[i->first];
                    i = p.known.erase(i);
                }
                else
                {
                    ++i;
                }
            }
            return out;
        }

        namespace
        {
            //! How often the main thread takes what the network threads
            //! received. Short enough that a remote note shows as it is
            //! typed elsewhere, long enough to cost nothing.
            const std::chrono::milliseconds tickTimeout(100);

            //! How long to wait before reconnecting or resending, growing to
            //! the maximum while the server stays away.
            const std::chrono::seconds retryMin(1);
            const std::chrono::seconds retryMax(10);

            //! How long to wait for a connection. Leaving a session waits for
            //! a connection being made, since one cannot be stopped part way,
            //! so this is kept short: the server is on the network next door.
            const std::chrono::seconds connectTimeout(2);

            //! A server, split into what the client connects to and the path
            //! the API is under, so a server behind a proxy at
            //! "http://host/djv" works as well as one on its own port.
            struct ServerURL
            {
                std::string schemeHostPort;
                std::string prefix;
            };

            ServerURL splitServer(const std::string& value)
            {
                ServerURL out;
                std::string url = value;
                while (!url.empty() && '/' == url.back())
                {
                    url.pop_back();
                }
                const size_t scheme = url.find("://");
                const size_t pathStart = url.find(
                    '/',
                    std::string::npos == scheme ? 0 : scheme + 3);
                if (pathStart != std::string::npos)
                {
                    out.schemeHostPort = url.substr(0, pathStart);
                    out.prefix = url.substr(pathStart);
                }
                else
                {
                    out.schemeHostPort = url;
                }
                if (std::string::npos == scheme)
                {
                    out.schemeHostPort = "http://" + out.schemeHostPort;
                }
                return out;
            }

            //! A message from the network threads to the main thread.
            struct Inbound
            {
                enum class Type
                {
                    Snapshot,
                    Event,
                    Status
                };

                Type           type = Type::Event;
                nlohmann::json json;
                SyncStatus     status;
            };

            std::string errorMessage(const httplib::Result& result)
            {
                if (!result)
                {
                    return httplib::to_string(result.error());
                }
                return ftk::Format("HTTP {0}").arg(result->status);
            }

            //! The message in a refusal, when the server gave one.
            std::string serverError(const httplib::Result& result)
            {
                if (result)
                {
                    try
                    {
                        return jsonText(nlohmann::json::parse(result->body), "error");
                    }
                    catch (const std::exception&)
                    {}
                }
                return std::string();
            }

            //! The headers every request carries: the session token, and
            //! which DJV is asking.
            httplib::Headers syncHeaders(const std::string& token, const SyncIdentity& identity)
            {
                httplib::Headers out;
                if (!token.empty())
                {
                    out.emplace("Authorization", "Bearer " + token);
                }
                if (!identity.clientId.empty())
                {
                    out.emplace("X-DJV-Client", identity.clientId);
                }
                if (!identity.version.empty())
                {
                    out.emplace("X-DJV-Version", identity.version);
                }
                return out;
            }

            //! What the main thread shares with the network threads.
            struct SyncShared
            {
                std::mutex mutex;
                std::condition_variable cv;
                bool running = false;
                ServerURL url;
                std::string session;
                httplib::Headers headers;
                //! This connection, for telling our own changes coming back.
                std::string clientId;
                std::vector<Inbound> inbox;
                std::deque<SyncOp> outbox;
                //! The clients, so they can be stopped from the main thread
                //! while they wait for the server.
                httplib::Client* streamClient = nullptr;
                httplib::Client* sendClient = nullptr;
                //! Ask the stream thread to read the state again.
                bool resync = false;
            };
        }

        namespace
        {
            //! The server's address, checked: "http://" only.
            std::optional<ServerURL> checkServer(const std::string& server, std::string& message)
            {
                if (server.empty())
                {
                    message = "No sync server is set.";
                    return std::nullopt;
                }
                const ServerURL url = splitServer(server);
                if (0 != url.schemeHostPort.compare(0, 7, "http://"))
                {
                    message = ftk::Format("\"{0}\" is not an http:// address.").arg(server);
                    return std::nullopt;
                }
                return url;
            }
        }

        SyncServerInfo fetchSyncServerInfo(
            const std::string& server,
            const std::string& token,
            const SyncIdentity& identity)
        {
            SyncServerInfo out;
            out.server = server;
            out.clientId = identity.clientId;
            const auto url = checkServer(server, out.message);
            if (!url.has_value())
            {
                out.state = SyncServerState::Error;
                return out;
            }
            httplib::Client client(url->schemeHostPort);
            client.set_connection_timeout(connectTimeout);
            client.set_read_timeout(std::chrono::seconds(5));
            const auto result = client.Get(url->prefix + "/api/hello", syncHeaders(token, identity));
            if (!result)
            {
                out.state = SyncServerState::Unreachable;
                out.message = ftk::Format("Cannot reach {0} ({1}).").arg(server).arg(errorMessage(result));
                return out;
            }
            if (result->status != 200)
            {
                // An older sync server has no hello: it is reached, it
                // just says nothing about a share.
                out.state = 404 == result->status ? SyncServerState::Ok : SyncServerState::Unreachable;
                out.message = 404 == result->status ?
                    std::string("The server is an older version that does not say where the media is.") :
                    std::string(ftk::Format("The server answered {0}; is this a DJV sync server?").
                        arg(errorMessage(result)));
                return out;
            }
            try
            {
                out = parseSyncServerInfo(nlohmann::json::parse(result->body));
                out.server = server;
                out.clientId = identity.clientId;
            }
            catch (const std::exception& e)
            {
                out.state = SyncServerState::Error;
                out.message = ftk::Format("Unexpected reply: {0}").arg(e.what());
            }
            return out;
        }

        SyncLoginResult syncLogin(
            const std::string& server,
            const std::string& username,
            const std::string& password,
            const SyncIdentity& identity)
        {
            SyncLoginResult out;
            const auto url = checkServer(server, out.message);
            if (!url.has_value())
            {
                return out;
            }
            if (!isValidSyncUser(username))
            {
                out.message = username.empty() ?
                    std::string("No user name is set.") :
                    std::string(ftk::Format("\"{0}\" is not a user name: letters, digits, '.', '_' and '-' only.").
                        arg(username));
                return out;
            }
            if (password.empty())
            {
                out.message = "No password is set.";
                return out;
            }
            httplib::Client client(url->schemeHostPort);
            client.set_connection_timeout(connectTimeout);
            client.set_read_timeout(std::chrono::seconds(10));
            nlohmann::json body;
            body["username"] = username;
            body["password"] = password;
            const auto result = client.Post(
                url->prefix + "/api/login",
                syncHeaders(std::string(), identity),
                body.dump(),
                "application/json");
            if (!result)
            {
                out.unreachable = true;
                out.message = ftk::Format("Cannot reach {0} ({1}).").arg(server).arg(errorMessage(result));
                return out;
            }
            if (result->status != 200)
            {
                const std::string error = serverError(result);
                if (404 == result->status)
                {
                    out.message = "The server is an older version without users; update it.";
                }
                else if (!error.empty())
                {
                    out.message = error;
                }
                else
                {
                    out.message = ftk::Format("The server answered {0}.").arg(errorMessage(result));
                }
                return out;
            }
            try
            {
                const nlohmann::json json = nlohmann::json::parse(result->body);
                const nlohmann::json user = jsonObject(json, "user");
                out.token = jsonText(json, "token");
                out.username = jsonText(user, "username");
                out.userFullName = jsonText(user, "name");
                out.ok = !out.token.empty();
                if (!out.ok)
                {
                    out.message = "The server signed in without giving a session token.";
                }
            }
            catch (const std::exception& e)
            {
                out.message = ftk::Format("Unexpected reply: {0}").arg(e.what());
            }
            return out;
        }

        void syncLogout(
            const std::string& server,
            const std::string& token,
            const SyncIdentity& identity)
        {
            std::string message;
            const auto url = checkServer(server, message);
            if (!url.has_value() || token.empty())
            {
                return;
            }
            httplib::Client client(url->schemeHostPort);
            client.set_connection_timeout(connectTimeout);
            client.set_read_timeout(std::chrono::seconds(5));
            client.Post(url->prefix + "/api/logout", syncHeaders(token, identity), std::string(), "application/json");
        }

        struct SyncModel::Private
        {
            std::weak_ptr<ftk::Context> context;
            std::weak_ptr<MarkersModel> markersModel;
            std::shared_ptr<ftk::Observable<SyncStatus> > status;
            std::shared_ptr<ftk::Observable<std::optional<SyncMedia> > > media;
            std::optional<SyncMedia> localMedia;
            std::optional<ReviewSync> sync;
            std::shared_ptr<SyncMerger> merger;
            std::shared_ptr<ftk::Timer> timer;
            std::shared_ptr<ftk::ListObserver<ReviewMarker> > markersObserver;

            std::shared_ptr<SyncShared> shared;
            std::thread streamThread;
            std::thread sendThread;
        };

        namespace
        {
            void streamThread(std::shared_ptr<SyncShared> shared)
            {
                std::chrono::seconds retry = retryMin;
                auto push = [&shared](Inbound&& msg)
                {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->inbox.push_back(std::move(msg));
                };
                auto status = [&push, &shared](SyncState state, const std::string& message)
                {
                    Inbound msg;
                    msg.type = Inbound::Type::Status;
                    msg.status.state = state;
                    msg.status.message = message;
                    push(std::move(msg));
                };
                auto wait = [&shared](std::chrono::seconds value)
                {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->cv.wait_for(lock, value, [&shared] { return !shared->running; });
                    return shared->running;
                };

                ServerURL url;
                std::string session;
                httplib::Headers headers;
                {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    url = shared->url;
                    session = shared->session;
                    headers = shared->headers;
                }
                const std::string base = url.prefix + "/api/sessions/" + session;

                while (true)
                {
                    {
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        if (!shared->running)
                        {
                            break;
                        }
                        shared->resync = false;
                    }

                    httplib::Client client(url.schemeHostPort);
                    client.set_connection_timeout(connectTimeout);
                    // The server writes a comment every 15 seconds, so a
                    // stream quiet for longer than this has gone.
                    client.set_read_timeout(std::chrono::seconds(45));
                    client.set_write_timeout(std::chrono::seconds(5));
                    {
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        if (!shared->running)
                        {
                            break;
                        }
                        shared->streamClient = &client;
                    }

                    // The state first, then the changes after it. Asking the
                    // stream to start at the state's revision leaves no gap
                    // between the two.
                    int64_t revision = 0;
                    bool ok = false;
                    auto result = client.Get(base, headers);
                    if (result && 200 == result->status)
                    {
                        try
                        {
                            Inbound msg;
                            msg.type = Inbound::Type::Snapshot;
                            msg.json = nlohmann::json::parse(result->body);
                            revision = msg.json.value("rev", static_cast<int64_t>(0));
                            push(std::move(msg));
                            ok = true;
                        }
                        catch (const std::exception& e)
                        {
                            status(SyncState::Error, ftk::Format("Unexpected reply: {0}").arg(e.what()));
                        }
                    }
                    else if (result && 401 == result->status)
                    {
                        // Kept trying: once this DJV signs in, App
                        // connects again with the new session token.
                        status(SyncState::Error, syncSignedOutMessage());
                    }
                    else if (result && 403 == result->status)
                    {
                        const std::string error = serverError(result);
                        status(SyncState::Error, error.empty() ? std::string("The server refused this user.") : error);
                    }
                    else if (result)
                    {
                        // Something answered, but not as a sync server
                        // would: a wrong address, or a proxy in the way.
                        status(SyncState::Connecting, ftk::Format(
                            "The server answered {0}; is this a DJV sync server? Retrying.").
                            arg(errorMessage(result)));
                    }
                    else
                    {
                        status(SyncState::Connecting, ftk::Format("Cannot reach the server ({0}); retrying.").
                            arg(errorMessage(result)));
                    }

                    bool removed = false;
                    if (ok)
                    {
                        retry = retryMin;
                        status(SyncState::Connected, std::string());
                        SyncEventStream stream;
                        bool resync = false;

                        client.Get(
                            ftk::Format("{0}/events?since={1}").arg(base).arg(revision),
                            headers,
                            [&](const char* data, size_t size)
                            {
                                for (auto& event : parseSyncEvents(stream, data, size))
                                {
                                    try
                                    {
                                        Inbound msg;
                                        msg.type = Inbound::Type::Event;
                                        msg.json = nlohmann::json::parse(event);
                                        const std::string type = msg.json.value("type", std::string());
                                        if ("reset" == type)
                                        {
                                            resync = true;
                                        }
                                        else if ("removed" == type)
                                        {
                                            removed = true;
                                            continue;
                                        }
                                        push(std::move(msg));
                                    }
                                    catch (const std::exception&)
                                    {}
                                }
                                std::unique_lock<std::mutex> lock(shared->mutex);
                                return shared->running && !shared->resync && !resync && !removed;
                            });
                    }
                    if (removed)
                    {
                        // The session was deleted on the server, or this
                        // user taken out of it. Both threads stop: writing
                        // to it again would make a new, empty session of
                        // the same name.
                        status(SyncState::Error, ftk::Format(
                            "The session \"{0}\" was deleted on the server, or you are no longer one of "
                            "its people. Leave it, or join another.").arg(session));
                        {
                            std::unique_lock<std::mutex> lock(shared->mutex);
                            shared->running = false;
                            shared->streamClient = nullptr;
                        }
                        shared->cv.notify_all();
                        break;
                    }

                    bool again = false;
                    {
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        shared->streamClient = nullptr;
                        if (!shared->running)
                        {
                            break;
                        }
                        again = shared->resync;
                    }
                    if (again)
                    {
                        continue;
                    }
                    if (ok)
                    {
                        status(SyncState::Connecting, "The connection was lost; reconnecting.");
                    }
                    if (!wait(retry))
                    {
                        break;
                    }
                    retry = std::min(retry * 2, std::chrono::seconds(retryMax));
                }
            }

            void sendThread(std::shared_ptr<SyncShared> shared)
            {
                ServerURL url;
                std::string session;
                httplib::Headers headers;
                std::string clientId;
                {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    url = shared->url;
                    session = shared->session;
                    headers = shared->headers;
                    clientId = shared->clientId;
                }
                const std::string base = url.prefix + "/api/sessions/" + session;
                httplib::Client client(url.schemeHostPort);
                client.set_connection_timeout(connectTimeout);
                client.set_read_timeout(std::chrono::seconds(10));
                client.set_write_timeout(std::chrono::seconds(10));
                {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->sendClient = &client;
                }
                std::chrono::seconds retry = retryMin;
                while (true)
                {
                    SyncOp op;
                    {
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        shared->cv.wait(
                            lock,
                            [&shared] { return !shared->running || !shared->outbox.empty(); });
                        if (!shared->running)
                        {
                            break;
                        }
                        op = shared->outbox.front();
                    }

                    // The identifier came from a marker; it is ours or the
                    // server's, but it goes into a path all the same.
                    const std::string markerPath = base + "/markers/" + httplib::encode_uri_component(op.id);
                    const std::string mediaPath = base + "/media";
                    const std::string clientQuery = "?clientId=" + httplib::encode_uri_component(clientId);
                    httplib::Result result;
                    switch (op.type)
                    {
                    case SyncOp::Type::Put:
                    {
                        nlohmann::json body;
                        body["clientId"] = clientId;
                        body["marker"] = op.marker;
                        result = client.Put(markerPath, headers, body.dump(), "application/json");
                        break;
                    }
                    case SyncOp::Type::Delete:
                        result = client.Delete(markerPath + clientQuery, headers);
                        break;
                    case SyncOp::Type::PutMedia:
                    {
                        nlohmann::json body;
                        body["clientId"] = clientId;
                        body["media"] = op.media;
                        result = client.Put(mediaPath, headers, body.dump(), "application/json");
                        break;
                    }
                    }

                    if (result && result->status >= 200 && result->status < 300)
                    {
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        if (!shared->outbox.empty())
                        {
                            shared->outbox.pop_front();
                        }
                        retry = retryMin;
                    }
                    else if (result && result->status >= 400 && result->status < 500 &&
                        result->status != 401 && result->status != 403 && result->status != 408 &&
                        result->status != 429)
                    {
                        // The server will not take it however often it is
                        // asked. Drop it, and say so.
                        Inbound msg;
                        msg.type = Inbound::Type::Event;
                        msg.json["type"] = "dropped";
                        msg.json["id"] = op.id;
                        switch (op.type)
                        {
                        case SyncOp::Type::Put: msg.json["op"] = "put"; break;
                        case SyncOp::Type::Delete: msg.json["op"] = "delete"; break;
                        case SyncOp::Type::PutMedia: msg.json["op"] = "media"; break;
                        }
                        msg.json["message"] = errorMessage(result);
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        if (!shared->outbox.empty())
                        {
                            shared->outbox.pop_front();
                        }
                        shared->inbox.push_back(std::move(msg));
                    }
                    else
                    {
                        // Not reached, or not now: the change keeps its place
                        // at the front of the queue, so the order holds.
                        std::unique_lock<std::mutex> lock(shared->mutex);
                        shared->cv.wait_for(lock, retry, [&shared] { return !shared->running; });
                        if (!shared->running)
                        {
                            break;
                        }
                        retry = std::min(retry * 2, std::chrono::seconds(retryMax));
                    }
                }
                std::unique_lock<std::mutex> lock(shared->mutex);
                shared->sendClient = nullptr;
            }
        }

        void SyncModel::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<MarkersModel>& markersModel)
        {
            FTK_P();
            p.context = context;
            p.markersModel = markersModel;
            p.status = ftk::Observable<SyncStatus>::create();
            p.media = ftk::Observable<std::optional<SyncMedia> >::create();
            p.timer = ftk::Timer::create(context);
            p.timer->setRepeating(true);
        }

        SyncModel::SyncModel() :
            _p(new Private)
        {}

        SyncModel::~SyncModel()
        {
            disconnect();
        }

        std::shared_ptr<SyncModel> SyncModel::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<MarkersModel>& markersModel)
        {
            auto out = std::shared_ptr<SyncModel>(new SyncModel);
            out->_init(context, markersModel);
            return out;
        }

        const std::optional<ReviewSync>& SyncModel::getSync() const
        {
            return _p->sync;
        }

        std::shared_ptr<ftk::IObservable<SyncStatus> > SyncModel::observeStatus() const
        {
            return _p->status;
        }

        void SyncModel::connect(
            const ReviewSync& sync,
            const std::string& token,
            const SyncIdentity& identity)
        {
            FTK_P();
            disconnect();

            if (sync.server.empty())
            {
                throw std::invalid_argument("No sync server given.");
            }
            if (!isValidSyncSession(sync.session))
            {
                throw std::invalid_argument(ftk::Format(
                    "The session name \"{0}\" cannot be used: letters, digits, "
                    "'.', '_' and '-' only.").arg(sync.session));
            }
            const ServerURL url = splitServer(sync.server);
            if (0 == url.schemeHostPort.compare(0, 8, "https://"))
            {
                throw std::invalid_argument(
                    "This build cannot connect with HTTPS; use an http:// "
                    "address, e.g. through a tunnel or a proxy on this machine.");
            }
            if (0 != url.schemeHostPort.compare(0, 7, "http://"))
            {
                throw std::invalid_argument(ftk::Format(
                    "\"{0}\" is not an http:// address.").arg(sync.server));
            }

            auto markersModel = p.markersModel.lock();
            if (!markersModel)
            {
                return;
            }

            p.sync = sync;
            p.merger = SyncMerger::create(markersModel, generateId());
            p.merger->setLocalMedia(p.localMedia);

            p.shared = std::make_shared<SyncShared>();
            p.shared->running = true;
            p.shared->url = url;
            p.shared->session = sync.session;
            p.shared->clientId = p.merger->getClientId();
            p.shared->headers = syncHeaders(token, identity);

            SyncStatus status;
            status.state = SyncState::Connecting;
            status.server = sync.server;
            status.session = sync.session;
            _setStatus(status);

            // Local changes are compared against what the server was told
            // once the session state is in; until then the merger ignores
            // them, and the state's merge picks them up.
            p.markersObserver = ftk::ListObserver<ReviewMarker>::create(
                markersModel->observeMarkers(),
                [this](const std::vector<ReviewMarker>&)
                {
                    FTK_P();
                    if (!p.merger || !p.shared)
                    {
                        return;
                    }
                    const auto ops = p.merger->localChanged();
                    if (!ops.empty())
                    {
                        {
                            std::unique_lock<std::mutex> lock(p.shared->mutex);
                            p.shared->outbox.insert(p.shared->outbox.end(), ops.begin(), ops.end());
                        }
                        p.shared->cv.notify_all();
                    }
                },
                ftk::ObserverAction::Suppress);

            p.streamThread = std::thread(streamThread, p.shared);
            p.sendThread = std::thread(sendThread, p.shared);

            p.timer->start(
                tickTimeout,
                [this]
                {
                    _tick();
                });

            if (auto context = p.context.lock())
            {
                context->getLogSystem()->print(
                    "djv::models::SyncModel",
                    ftk::Format("Connecting to session \"{0}\" on {1}").
                        arg(sync.session).
                        arg(sync.server));
            }
        }

        void SyncModel::disconnect()
        {
            FTK_P();
            p.timer->stop();
            p.markersObserver.reset();
            if (p.shared)
            {
                {
                    std::unique_lock<std::mutex> lock(p.shared->mutex);
                    p.shared->running = false;
                    if (p.shared->streamClient)
                    {
                        p.shared->streamClient->stop();
                    }
                    if (p.shared->sendClient)
                    {
                        p.shared->sendClient->stop();
                    }
                }
                p.shared->cv.notify_all();
            }
            if (p.streamThread.joinable())
            {
                p.streamThread.join();
            }
            if (p.sendThread.joinable())
            {
                p.sendThread.join();
            }
            if (p.shared)
            {
                std::unique_lock<std::mutex> lock(p.shared->mutex);
                if (!p.shared->outbox.empty())
                {
                    if (auto context = p.context.lock())
                    {
                        context->getLogSystem()->print(
                            "djv::models::SyncModel",
                            ftk::Format("Left the session with {0} change(s) not sent; "
                                "they are kept in the review.").arg(p.shared->outbox.size()),
                            ftk::LogType::Warning);
                    }
                }
            }
            p.shared.reset();
            p.merger.reset();
            p.media->setIfChanged(std::nullopt);
            const bool wasConnected = p.sync.has_value();
            p.sync.reset();
            if (wasConnected)
            {
                _setStatus(SyncStatus());
            }
        }

        void SyncModel::_tick()
        {
            FTK_P();
            if (!p.shared || !p.merger)
            {
                return;
            }
            std::vector<Inbound> inbox;
            {
                std::unique_lock<std::mutex> lock(p.shared->mutex);
                inbox.swap(p.shared->inbox);
            }
            std::vector<SyncOp> ops;
            for (const auto& msg : inbox)
            {
                switch (msg.type)
                {
                case Inbound::Type::Snapshot:
                {
                    const auto snapshotOps = p.merger->snapshot(msg.json);
                    ops.insert(ops.end(), snapshotOps.begin(), snapshotOps.end());
                    break;
                }
                case Inbound::Type::Event:
                    if ("dropped" == msg.json.value("type", std::string()))
                    {
                        const std::string what = msg.json.value("op", std::string());
                        const std::string message = msg.json.value("message", std::string());
                        std::string text;
                        if ("media" == what)
                        {
                            // Nothing waits on a media change: the session
                            // keeps the media it had.
                            text = ftk::Format("The server did not take the session's media: {0}").
                                arg(message);
                        }
                        else
                        {
                            SyncOp op;
                            op.id = msg.json.value("id", std::string());
                            op.type = "delete" == what ? SyncOp::Type::Delete : SyncOp::Type::Put;
                            p.merger->dropped(op);
                            text = ftk::Format("The server did not take a change to marker {0}: {1}").
                                arg(op.id).
                                arg(message);
                        }
                        if (auto context = p.context.lock())
                        {
                            context->getLogSystem()->print(
                                "djv::models::SyncModel",
                                text,
                                ftk::LogType::Error);
                        }
                    }
                    else if (!p.merger->event(msg.json))
                    {
                        // The server no longer has the changes since our
                        // revision: read the state again.
                        std::unique_lock<std::mutex> lock(p.shared->mutex);
                        p.shared->resync = true;
                    }
                    break;
                case Inbound::Type::Status:
                {
                    SyncStatus status = msg.status;
                    status.server = p.sync.has_value() ? p.sync->server : std::string();
                    status.session = p.sync.has_value() ? p.sync->session : std::string();
                    _setStatus(status);
                    break;
                }
                }
            }
            if (!ops.empty())
            {
                {
                    std::unique_lock<std::mutex> lock(p.shared->mutex);
                    p.shared->outbox.insert(p.shared->outbox.end(), ops.begin(), ops.end());
                }
                p.shared->cv.notify_all();
            }
            p.media->setIfChanged(p.merger->getMedia());
        }

        std::shared_ptr<ftk::IObservable<std::optional<SyncMedia> > > SyncModel::observeMedia() const
        {
            return _p->media;
        }

        void SyncModel::setLocalMedia(const std::optional<SyncMedia>& value)
        {
            FTK_P();
            p.localMedia = value;
            if (p.merger)
            {
                p.merger->setLocalMedia(value);
            }
        }

        bool SyncModel::shareMedia()
        {
            FTK_P();
            if (!p.merger || !p.shared)
            {
                return false;
            }
            const auto op = p.merger->shareMedia();
            if (!op.has_value())
            {
                return false;
            }
            {
                std::unique_lock<std::mutex> lock(p.shared->mutex);
                p.shared->outbox.push_back(op.value());
            }
            p.shared->cv.notify_all();
            return true;
        }

        void SyncModel::_setStatus(const SyncStatus& value)
        {
            FTK_P();
            if (p.status->setIfChanged(value))
            {
                if (auto context = p.context.lock())
                {
                    if (SyncState::Error == value.state ||
                        (SyncState::Connecting == value.state && !value.message.empty()))
                    {
                        context->getLogSystem()->print(
                            "djv::models::SyncModel",
                            value.message,
                            ftk::LogType::Warning);
                    }
                    else if (SyncState::Connected == value.state)
                    {
                        context->getLogSystem()->print(
                            "djv::models::SyncModel",
                            ftk::Format("Connected to session \"{0}\" on {1}").
                                arg(value.session).
                                arg(value.server));
                    }
                }
            }
        }
    }
}
