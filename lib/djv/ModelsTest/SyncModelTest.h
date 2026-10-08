// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <ftk/TestLib/ITest.h>

namespace djv
{
    namespace models_tests
    {
        class SyncModelTest : public ftk::test::ITest
        {
        protected:
            SyncModelTest(const std::shared_ptr<ftk::Context>&);

        public:
            static std::shared_ptr<SyncModelTest> create(
                const std::shared_ptr<ftk::Context>&);

            void run() override;

        private:
            void _sessions();
            void _events();
            void _markers();
            void _review();
            void _settings();
            void _media();
            void _merge();
            void _connect();
            void _live();
        };
    }
}
