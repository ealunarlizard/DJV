// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/PasswordEdit.h>

#include <ftk/UI/ClipboardSystem.h>
#include <ftk/UI/DrawUtil.h>
#include <ftk/UI/IWindow.h>
#include <ftk/UI/LayoutUtil.h>

#include <ftk/Core/RenderUtil.h>
#include <ftk/Core/String.h>

#include <chrono>
#include <optional>

namespace djv
{
    namespace ui
    {
        namespace
        {
            //! What stands for each character: a bullet.
            const std::string dot = "\xe2\x80\xa2";

            //! The width the field asks for, in characters.
            const std::string format(20, '#');
        }

        struct PasswordEdit::Private
        {
            std::string text;
            std::function<void(const std::string&)> callback;
            std::function<void(const std::string&)> textChangedCallback;

            bool cursorVisible = false;
            std::chrono::steady_clock::time_point cursorTimer;

            struct SizeData
            {
                bool init = true;
                int margin = 0;
                int border = 0;
                int keyFocus = 0;
                ftk::FontInfo fontInfo;
                ftk::FontMetrics fontMetrics;
                ftk::Size2I sizeHint;
            };
            SizeData size;

            struct DrawData
            {
                ftk::Box2I g;
                ftk::Box2I g2;
                ftk::Box2I g3;
                ftk::TriMesh2F bgMesh;
                ftk::TriMesh2F border;
                ftk::TriMesh2F keyFocus;
                std::vector<std::shared_ptr<ftk::Glyph> > glyphs;
                int textWidth = 0;
            };
            std::optional<DrawData> draw;
        };

        void PasswordEdit::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<IWidget>& parent)
        {
            IMouseWidget::_init(context, "djv::ui::PasswordEdit", parent);
            setHAlign(ftk::HAlign::Fill);
            setAcceptsKeyFocus(true);
            setKeyFocusAlwaysVisible(true);
            _setMouseHoverEnabled(true);
            _setMousePressEnabled(true);
        }

        PasswordEdit::PasswordEdit() :
            _p(new Private)
        {}

        PasswordEdit::~PasswordEdit()
        {}

        std::shared_ptr<PasswordEdit> PasswordEdit::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<PasswordEdit>(new PasswordEdit);
            out->_init(context, parent);
            return out;
        }

        const std::string& PasswordEdit::getText() const
        {
            return _p->text;
        }

        void PasswordEdit::clearText()
        {
            FTK_P();
            if (!p.text.empty())
            {
                // Overwritten before it goes, so that the characters are
                // not left in memory that is handed back.
                std::fill(p.text.begin(), p.text.end(), '\0');
                p.text.clear();
                _changed();
            }
        }

        void PasswordEdit::setCallback(const std::function<void(const std::string&)>& value)
        {
            _p->callback = value;
        }

        void PasswordEdit::setTextChangedCallback(const std::function<void(const std::string&)>& value)
        {
            _p->textChangedCallback = value;
        }

        ftk::Size2I PasswordEdit::getSizeHint() const
        {
            return _p->size.sizeHint;
        }

        void PasswordEdit::setGeometry(const ftk::Box2I& value)
        {
            const bool changed = value != getGeometry();
            IMouseWidget::setGeometry(value);
            if (changed)
            {
                _p->draw.reset();
            }
        }

        void PasswordEdit::tickEvent(
            bool parentsVisible,
            bool parentsEnabled,
            const ftk::TickEvent& event)
        {
            IMouseWidget::tickEvent(parentsVisible, parentsEnabled, event);
            FTK_P();
            if (hasKeyFocus())
            {
                const auto now = std::chrono::steady_clock::now();
                const std::chrono::duration<float> diff = now - p.cursorTimer;
                if (diff.count() > .7F)
                {
                    p.cursorVisible = !p.cursorVisible;
                    setDrawUpdate();
                    p.cursorTimer = now;
                }
            }
        }

        void PasswordEdit::styleEvent(const ftk::StyleEvent& event)
        {
            IMouseWidget::styleEvent(event);
            FTK_P();
            if (event.hasChanges())
            {
                p.size.init = true;
                p.draw.reset();
            }
        }

        void PasswordEdit::sizeHintEvent(const ftk::SizeHintEvent& event)
        {
            IMouseWidget::sizeHintEvent(event);
            FTK_P();
            if (p.size.init)
            {
                p.size.init = false;
                p.size.margin = event.style->getSizeRole(ftk::SizeRole::MarginInside, event.displayScale);
                p.size.border = event.style->getSizeRole(ftk::SizeRole::Border, event.displayScale);
                p.size.keyFocus = event.style->getSizeRole(ftk::SizeRole::KeyFocus, event.displayScale);
                p.size.fontInfo = event.style->getFont(ftk::FontType::Regular, event.displayScale);
                p.size.fontMetrics = event.fontSystem->getMetrics(p.size.fontInfo);
                const ftk::Size2I formatSize = event.fontSystem->getSize(format, p.size.fontInfo);
                p.size.sizeHint = ftk::margin(
                    ftk::Size2I(formatSize.w, p.size.fontMetrics.lineHeight),
                    p.size.margin * 2 + p.size.keyFocus,
                    p.size.margin + p.size.keyFocus);
                p.draw.reset();
            }
        }

        void PasswordEdit::drawEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event)
        {
            IMouseWidget::drawEvent(drawRect, event);
            FTK_P();

            if (!p.draw.has_value())
            {
                p.draw = Private::DrawData();
                p.draw->g = getGeometry();
                p.draw->g2 = ftk::margin(p.draw->g, -p.size.keyFocus);
                p.draw->g3 = ftk::margin(p.draw->g2, -p.size.margin * 2, -p.size.margin);
                p.draw->bgMesh = ftk::rect(p.draw->g);
                p.draw->border = ftk::border(p.draw->g, p.size.border);
                p.draw->keyFocus = ftk::border(p.draw->g, p.size.keyFocus);
                std::string dots;
                const size_t count = ftk::utf8Count(p.text, p.text.size());
                for (size_t i = 0; i < count; ++i)
                {
                    dots += dot;
                }
                if (!dots.empty())
                {
                    p.draw->glyphs = event.fontSystem->getGlyphs(dots, p.size.fontInfo);
                    p.draw->textWidth = event.fontSystem->getSize(dots, p.size.fontInfo).w;
                }
            }

            const bool enabled = isEnabled();
            event.render->drawMesh(p.draw->bgMesh, event.style->getColorRole(ftk::ColorRole::Well));
            const bool keyFocus = showKeyFocus();
            event.render->drawMesh(
                keyFocus ? p.draw->keyFocus : p.draw->border,
                event.style->getColorRole(keyFocus ? ftk::ColorRole::KeyFocus : ftk::ColorRole::Border));

            const ftk::ClipRectEnabledState clipRectEnabledState(event.render);
            const ftk::ClipRectState clipRectState(event.render);
            event.render->setClipRectEnabled(true);
            event.render->setClipRect(ftk::intersect(p.draw->g2, drawRect));

            // A long password scrolls so that its end, where the typing
            // goes, stays in view.
            const int scroll = std::max(0, p.draw->textWidth - p.draw->g3.w() + p.size.border);
            const ftk::V2I pos(
                p.draw->g3.x() - scroll,
                p.draw->g3.y() + p.draw->g3.h() / 2 - p.size.fontMetrics.lineHeight / 2);
            event.render->drawText(
                p.draw->glyphs,
                p.size.fontMetrics,
                pos,
                event.style->getColorRole(ftk::ColorRole::Text, enabled));

            if (p.cursorVisible)
            {
                event.render->drawRect(
                    ftk::Box2I(
                        p.draw->g3.x() - scroll + p.draw->textWidth,
                        p.draw->g3.y(),
                        p.size.border,
                        p.draw->g3.h()),
                    event.style->getColorRole(ftk::ColorRole::Text));
            }
        }

        void PasswordEdit::mousePressEvent(ftk::MouseClickEvent& event)
        {
            IMouseWidget::mousePressEvent(event);
            if (ftk::MouseButton::Left == event.button)
            {
                takeKeyFocus();
            }
        }

        void PasswordEdit::keyFocusEvent(bool value)
        {
            IMouseWidget::keyFocusEvent(value);
            FTK_P();
            p.cursorVisible = value;
            p.cursorTimer = std::chrono::steady_clock::now();
            setDrawUpdate();
            if (auto window = getWindow())
            {
                window->setTextInput(value);
            }
        }

        void PasswordEdit::keyPressEvent(ftk::KeyEvent& event)
        {
            FTK_P();
            if (hasKeyFocus())
            {
                switch (event.key)
                {
                case ftk::Key::Return:
                    event.accept = true;
                    if (p.callback)
                    {
                        p.callback(p.text);
                    }
                    break;
                case ftk::Key::Escape:
                    event.accept = true;
                    releaseKeyFocus();
                    break;
                case ftk::Key::Backspace:
                    event.accept = true;
                    if (!p.text.empty())
                    {
                        // A whole word at once takes the whole password:
                        // there are no words to see.
                        if (event.modifiers & static_cast<int>(ftk::KeyModifier::Control))
                        {
                            clearText();
                        }
                        else
                        {
                            p.text.erase(ftk::utf8Prev(p.text, p.text.size()));
                            _changed();
                        }
                    }
                    break;
                case ftk::Key::V:
                    if (event.modifiers & static_cast<int>(ftk::commandKeyModifier))
                    {
                        event.accept = true;
                        if (auto context = getContext())
                        {
                            std::string text = context->getSystem<ftk::ClipboardSystem>()->getText();
                            // One line: a line break copied with it is not
                            // part of the password.
                            const size_t end = text.find_first_of("\r\n");
                            if (end != std::string::npos)
                            {
                                text.erase(end);
                            }
                            if (!text.empty())
                            {
                                p.text += text;
                                _changed();
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            if (!event.accept)
            {
                IMouseWidget::keyPressEvent(event);
            }
        }

        void PasswordEdit::keyReleaseEvent(ftk::KeyEvent& event)
        {
            IMouseWidget::keyReleaseEvent(event);
            event.accept = true;
        }

        void PasswordEdit::textEvent(ftk::TextEvent& event)
        {
            event.accept = true;
            _p->text += event.text;
            _changed();
        }

        void PasswordEdit::_changed()
        {
            FTK_P();
            p.cursorVisible = hasKeyFocus();
            p.cursorTimer = std::chrono::steady_clock::now();
            p.draw.reset();
            setDrawUpdate();
            if (p.textChangedCallback)
            {
                p.textChangedCallback(p.text);
            }
        }
    }
}
