// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>
#include <djv/Models/Review.h>

#include <ftk/Core/Observable.h>
#include <ftk/Core/Util.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ftk
{
    class Context;
}

namespace djv
{
    namespace models
    {
        class MarkersModel;

        //! The state of the connection to a sync server.
        enum class SyncState
        {
            Disconnected,
            Connecting,
            Connected,
            Error
        };

        //! The connection to a sync server, as the interface shows it.
        struct DJV_MODELS_API_TYPE SyncStatus
        {
            SyncState   state = SyncState::Disconnected;
            std::string server;
            std::string session;

            //! What went wrong, or what is being waited for.
            std::string message;

            bool operator == (const SyncStatus&) const = default;
        };

        //! Whether a session name can be used: letters, digits, '.', '_'
        //! and '-', up to 128 of them. The name goes into a URL path and a
        //! file name on the server, so it is kept to what is safe in both.
        DJV_MODELS_API bool isValidSyncSession(const std::string&);

        //! Whether a client identifier can be used: letters, digits, '.',
        //! '_' and '-', up to 64 of them. The server checks the same.
        DJV_MODELS_API bool isValidSyncClientId(const std::string&);

        //! Whether a user name can be used on the sync server: the same
        //! letters, digits, '.', '_' and '-', up to 64 of them.
        DJV_MODELS_API bool isValidSyncUser(const std::string&);

        //! Which DJV is asking the sync server: the PC, by its name, and the
        //! DJV it runs. Sent with every request, for the admin page; who may
        //! do what is the signed-in user's token.
        struct DJV_MODELS_API_TYPE SyncIdentity
        {
            std::string clientId;
            std::string version;

            bool operator == (const SyncIdentity&) const = default;
        };

        //! What the sync server said about itself and this PC.
        enum class SyncServerState
        {
            Unknown,
            Checking,
            //! Reached, and signed in (or the server does not ask for it).
            Ok,
            //! Reached, but not signed in: no user or password is set.
            SignedOut,
            //! Reached, but the user name or the password was refused.
            LoginFailed,
            //! Not reached, or not a sync server.
            Unreachable,
            Error
        };

        //! The sync server's answer to hello.
        struct DJV_MODELS_API_TYPE SyncServerInfo
        {
            SyncServerState state = SyncServerState::Unknown;
            std::string server;
            std::string name;
            std::string serverVersion;

            //! The DJV version the studio runs; empty when not checked.
            std::string djvVersion;

            //! Whether the server asks its users to sign in. Servers before
            //! version 2 do not.
            bool authRequired = false;

            //! This PC's name, as sent to the server.
            std::string clientId;

            //! Who is signed in: the user name and the person's name.
            std::string username;
            std::string userFullName;

            //! The media share, e.g. "\\192.168.1.40\media", and the
            //! Windows account it is mapped with, given to signed-in users
            //! only. Empty when none. The password is held in memory only.
            std::string storageUnc;
            std::string shareUser;
            std::string sharePassword;

            //! What went wrong, or a warning such as a version mismatch.
            std::string message;

            bool operator == (const SyncServerInfo&) const = default;
        };

        //! Read the server's answer to hello. The state is Ok or
        //! SignedOut.
        DJV_MODELS_API SyncServerInfo parseSyncServerInfo(const nlohmann::json&);

        //! Ask a sync server about itself, as the user the session token
        //! belongs to (none when empty). Blocks for as long as the server
        //! takes, up to a few seconds, so it is called from a thread of its
        //! own (see LanModel).
        DJV_MODELS_API SyncServerInfo fetchSyncServerInfo(
            const std::string& server,
            const std::string& token,
            const SyncIdentity&);

        //! The answer to signing in.
        struct DJV_MODELS_API_TYPE SyncLoginResult
        {
            bool ok = false;

            //! The server could not be reached, as opposed to refusing.
            bool unreachable = false;

            //! The session token, sent with every request from then on.
            std::string token;
            std::string username;
            std::string userFullName;
            std::string message;

            bool operator == (const SyncLoginResult&) const = default;
        };

        //! Sign in to a sync server. Blocks like fetchSyncServerInfo().
        DJV_MODELS_API SyncLoginResult syncLogin(
            const std::string& server,
            const std::string& username,
            const std::string& password,
            const SyncIdentity&);

        //! Sign out: the server forgets the session token. Blocks like
        //! fetchSyncServerInfo().
        DJV_MODELS_API void syncLogout(
            const std::string& server,
            const std::string& token,
            const SyncIdentity&);

        //! The partial state of a server-sent event stream.
        struct DJV_MODELS_API_TYPE SyncEventStream
        {
            std::string line;
            std::string data;
            bool        hasData = false;
        };

        //! Feed the bytes of a server-sent event stream, returning the data
        //! of every event they complete. The data lines of an event are
        //! joined with newlines, and comments (the server's keep alive) are
        //! dropped. The bytes may stop anywhere, including inside a line.
        DJV_MODELS_API std::vector<std::string> parseSyncEvents(
            SyncEventStream&,
            const char*,
            size_t);

        //! The media a sync session's markers are about: a file on network
        //! storage that every reviewer can reach, though each machine may
        //! reach it under a different name (a UNC share on Windows, a mount
        //! under /Volumes or /mnt elsewhere). So the path is kept twice: once
        //! relative to the network media folder each client sets for itself,
        //! and once as the client that shared it saw it.
        struct DJV_MODELS_API_TYPE SyncMedia
        {
            //! Relative to the network media folder, with forward slashes.
            //! Empty when the file was not inside the folder of the client
            //! that shared it.
            std::string path;

            //! The path as the client that shared it saw it, with forward
            //! slashes; tried when the relative path does not lead to the
            //! file.
            std::string pathAbsolute;

            //! The file name, for showing.
            std::string name;

            //! Who shared it (see reviewAuthor()).
            std::string sharedBy;

            bool operator == (const SyncMedia&) const = default;
        };

        //! Describe a file as the media of a sync session, relative to the
        //! network media folder when it is inside it.
        DJV_MODELS_API SyncMedia syncMediaFromPath(
            const std::string& path,
            const std::filesystem::path& mediaRoot);

        //! Find the media of a sync session on this machine: the relative
        //! path in the network media folder first, then the absolute path.
        //! Reports whether the file is there; when it is not, the path
        //! returned is the one that was expected.
        DJV_MODELS_API std::filesystem::path resolveSyncMedia(
            const SyncMedia&,
            const std::filesystem::path& mediaRoot,
            bool& exists);

        DJV_MODELS_API void to_json(nlohmann::json&, const SyncMedia&);

        //! Read leniently: what is missing is left empty, since the media
        //! comes from a server that other versions of DJV write to.
        DJV_MODELS_API void from_json(const nlohmann::json&, SyncMedia&);

        //! A change to send to the server.
        struct DJV_MODELS_API_TYPE SyncOp
        {
            enum class Type
            {
                Put,
                Delete,
                PutMedia
            };

            Type           type = Type::Put;

            //! The marker, for Put and Delete.
            std::string    id;
            nlohmann::json marker;

            //! The media, for PutMedia.
            SyncMedia      media;
        };

        //! The words for a refusal from a server that wants its users to
        //! sign in.
        DJV_MODELS_API std::string syncSignedOutMessage();

        //! The merge between the markers and a sync session, without the
        //! network: it is told what the server said and what the markers
        //! now are, and says what to send.
        //!
        //! The server decides the order of the changes: the one it receives
        //! last wins, and every client applies the changes in the order the
        //! server numbered them. A remote change to a marker with a local
        //! change still on its way is skipped, since the local one arrives
        //! after it and will win everywhere. Deletions are kept by the server
        //! so that a client that was away learns of them.
        class DJV_MODELS_API_TYPE SyncMerger : public std::enable_shared_from_this<SyncMerger>
        {
            FTK_NON_COPYABLE(SyncMerger);

        protected:
            void _init(
                const std::shared_ptr<MarkersModel>&,
                const std::string& clientId);

            SyncMerger();

        public:
            DJV_MODELS_API ~SyncMerger();

            //! Create a new merger.
            DJV_MODELS_API static std::shared_ptr<SyncMerger> create(
                const std::shared_ptr<MarkersModel>&,
                const std::string& clientId);

            //! Get the client identifier the changes are sent with.
            DJV_MODELS_API const std::string& getClientId() const;

            //! Get the number of the last change applied.
            DJV_MODELS_API int64_t getRevision() const;

            //! Get whether the session state has been received.
            DJV_MODELS_API bool hasSnapshot() const;

            //! Apply the session state, as sent when a client (re)connects:
            //! the server's markers replace the local ones, markers the
            //! server deleted are removed, and markers only this client has
            //! are sent. Returns the changes to send.
            DJV_MODELS_API std::vector<SyncOp> snapshot(const nlohmann::json&);

            //! Apply one change from the server's event stream. Returns
            //! false for an event that asks for the session state to be
            //! read again.
            DJV_MODELS_API bool event(const nlohmann::json&);

            //! Compare the markers with what the server was last told,
            //! returning the changes to send. Does nothing before the
            //! session state has been received.
            DJV_MODELS_API std::vector<SyncOp> localChanged();

            //! A change could not be sent and will not be retried.
            DJV_MODELS_API void dropped(const SyncOp&);

            //! Get the session's media, as the server last said.
            DJV_MODELS_API const std::optional<SyncMedia>& getMedia() const;

            //! Get the media this client has open.
            DJV_MODELS_API const std::optional<SyncMedia>& getLocalMedia() const;

            //! Set the media this client has open. A session that has none
            //! when its state arrives is given this one, so whoever joins
            //! with a file open shares it with the people who join after.
            DJV_MODELS_API void setLocalMedia(const std::optional<SyncMedia>&);

            //! Make the media this client has open the session's, replacing
            //! what it had. Returns the change to send, or nothing when no
            //! media is open.
            DJV_MODELS_API std::optional<SyncOp> shareMedia() const;

        private:
            std::vector<SyncOp> _diff();

            FTK_PRIVATE();
        };

        //! Sync of the review markers with other people, through a sync
        //! server (see server/README.md).
        //!
        //! The network runs on two threads of its own: one reads the
        //! session state and then follows the server's event stream,
        //! reconnecting when the stream drops, and one sends the local
        //! changes in order, retrying until the server takes them. What
        //! arrives is applied to the markers on the main thread, from a
        //! timer.
        class DJV_MODELS_API_TYPE SyncModel : public std::enable_shared_from_this<SyncModel>
        {
            FTK_NON_COPYABLE(SyncModel);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<MarkersModel>&);

            SyncModel();

        public:
            DJV_MODELS_API ~SyncModel();

            //! Create a new model.
            DJV_MODELS_API static std::shared_ptr<SyncModel> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<MarkersModel>&);

            //! Get the session, when connected or connecting.
            DJV_MODELS_API const std::optional<ReviewSync>& getSync() const;

            //! Observe the connection status.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<SyncStatus> > observeStatus() const;

            //! Connect to a session, leaving any other first, as the user the
            //! session token belongs to (see LanModel). The markers already
            //! open are merged into the session: the ones it does not have
            //! are sent to it. Throws if the server or the session cannot be
            //! used.
            DJV_MODELS_API void connect(
                const ReviewSync&,
                const std::string& token,
                const SyncIdentity& = SyncIdentity());

            //! Leave the session. The markers stay as they are.
            DJV_MODELS_API void disconnect();

            //! Observe the session's media: the file its markers are about,
            //! when the session has one. Empty while not connected.
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<std::optional<SyncMedia> > > observeMedia() const;

            //! Set the media this client has open, kept across connections
            //! (see SyncMerger::setLocalMedia()).
            DJV_MODELS_API void setLocalMedia(const std::optional<SyncMedia>&);

            //! Make the media this client has open the session's. Returns
            //! false when not connected or when no media is open.
            DJV_MODELS_API bool shareMedia();

        private:
            void _tick();
            void _setStatus(const SyncStatus&);

            FTK_PRIVATE();
        };
    }
}
