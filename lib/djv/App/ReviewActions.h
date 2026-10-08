// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/App/Export.h>
#include <djv/App/IActions.h>
#include <djv/Models/Export.h>

namespace djv
{
    namespace app
    {
        class MainWindow;

        //! The sync session a review joins when none is named: the one it
        //! is in, then one named after its file.
        std::string defaultSyncSession(const std::shared_ptr<App>&);


        //! Review actions.
        class DJV_APP_API_TYPE ReviewActions : public IActions
        {
            FTK_NON_COPYABLE(ReviewActions);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&);

            ReviewActions();

        public:
            DJV_APP_API ~ReviewActions();

            DJV_APP_API static std::shared_ptr<ReviewActions> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&);

        private:
            void _drawStateUpdate(const std::shared_ptr<App>&);
            void _markersUpdate();
            void _syncUpdate();

            FTK_PRIVATE();
        };
    }
}
