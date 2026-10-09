// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <string>
#include <vector>

namespace djv
{
    namespace models
    {
        //! \name Network drives
        //!
        //! Mapping the studio's media share to a drive letter, the way
        //! "net use S: \\server\media" would. Only Windows has drive letters;
        //! elsewhere these report that mapping is not supported, and the
        //! share is reached through a folder set by hand instead.
        ///@{

        //! Whether drives can be mapped on this platform.
        DJV_MODELS_API bool isNetworkDriveSupported();

        //! A drive letter as stored and shown: one upper case letter, from
        //! "s", "S", "S:" or "S:\". Empty for anything else.
        DJV_MODELS_API std::string normalizeDriveLetter(const std::string&);

        //! The server of a UNC path: "192.168.1.40" for
        //! "\\192.168.1.40\media". Empty when it is not a UNC path.
        DJV_MODELS_API std::string getUncHost(const std::string&);

        //! A share account as Windows is to be given it: one alone, "djv",
        //! is the server's own, "192.168.1.40\djv", so that a PC in a
        //! domain does not look for it there. One with a domain or an "@"
        //! is left as it is.
        DJV_MODELS_API std::string shareAccountName(const std::string& user, const std::string& unc);

        //! Whether two UNC paths name the same share, ignoring case, the
        //! kind of slash, and a trailing one.
        DJV_MODELS_API bool isSameUncPath(const std::string&, const std::string&);

        //! What a drive letter is mapped to, as a UNC path, or empty when it
        //! is not a mapped network drive.
        DJV_MODELS_API std::string getDriveConnection(const std::string& letter);

        //! The drive letters free to map, D to Z, as single letters.
        DJV_MODELS_API std::vector<std::string> getFreeDriveLetters();

        //! The result of mapping a drive.
        struct DJV_MODELS_API_TYPE NetworkDriveResult
        {
            bool ok = false;

            //! The letter was already mapped to the share, so nothing was
            //! changed.
            bool alreadyMapped = false;

            //! What went wrong, in words for the person at the PC.
            std::string message;
        };

        //! Map a drive letter to a UNC path for this logon, not remembered
        //! by Windows: DJV maps it again when it starts. A letter already
        //! mapped to the same share is left as it is; one mapped elsewhere,
        //! or used by a disk, is refused rather than taken over. Blocks while
        //! Windows reaches the server, which can take many seconds, so it is
        //! called from a thread of its own (see LanModel). The share is
        //! signed in to with the account given, which the sync server hands
        //! to signed-in users; without one, with the user's own Windows
        //! credentials or those saved for the server in the Credential
        //! Manager (see saveShareCredential()).
        DJV_MODELS_API NetworkDriveResult mapNetworkDrive(
            const std::string& letter,
            const std::string& unc,
            const std::string& user = std::string(),
            const std::string& password = std::string());

        //! A path on a mapped drive as the UNC path it maps to, so that it
        //! can be found by a PC that has mapped the share to another letter,
        //! or not at all. Any other path is returned as it is.
        DJV_MODELS_API std::string toUncPath(const std::string&);

        ///@}
    }
}
