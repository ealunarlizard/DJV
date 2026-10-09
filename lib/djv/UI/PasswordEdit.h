// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <ftk/UI/IMouseWidget.h>

#include <functional>
#include <string>

namespace djv
{
    namespace ui
    {
        //! A field for typing a password: what is typed is shown as dots.
        //!
        //! ftk's line edit shows its text and keeps it in an undo history
        //! and on the clipboard, none of which a password should be. This
        //! one is kept small instead: the text is only added to at the end
        //! and taken from the end, it can be pasted into but never copied
        //! out of, and it is cleared once it has been used.
        class DJV_UI_API_TYPE PasswordEdit : public ftk::IMouseWidget
        {
            FTK_NON_COPYABLE(PasswordEdit);

        protected:
            DJV_UI_API void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<IWidget>& parent);

            DJV_UI_API PasswordEdit();

        public:
            DJV_UI_API virtual ~PasswordEdit();

            //! Create a new widget.
            DJV_UI_API static std::shared_ptr<PasswordEdit> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Get the password.
            DJV_UI_API const std::string& getText() const;

            //! Clear the password.
            DJV_UI_API void clearText();

            //! Set the callback for Return.
            DJV_UI_API void setCallback(const std::function<void(const std::string&)>&);

            //! Set the callback for each change.
            DJV_UI_API void setTextChangedCallback(const std::function<void(const std::string&)>&);

            DJV_UI_API ftk::Size2I getSizeHint() const override;
            DJV_UI_API void setGeometry(const ftk::Box2I&) override;
            DJV_UI_API void tickEvent(
                bool parentsVisible,
                bool parentsEnabled,
                const ftk::TickEvent&) override;
            DJV_UI_API void styleEvent(const ftk::StyleEvent&) override;
            DJV_UI_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            DJV_UI_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            DJV_UI_API void mousePressEvent(ftk::MouseClickEvent&) override;
            DJV_UI_API void keyFocusEvent(bool) override;
            DJV_UI_API void keyPressEvent(ftk::KeyEvent&) override;
            DJV_UI_API void keyReleaseEvent(ftk::KeyEvent&) override;
            DJV_UI_API void textEvent(ftk::TextEvent&) override;

        private:
            void _changed();

            FTK_PRIVATE();
        };
    }
}
