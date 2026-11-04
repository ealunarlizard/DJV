// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/AnnotationsModel.h>

#include <ftk/Core/Command.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace djv
{
    namespace models
    {
        namespace
        {
            //! A change to one drawing: what it was and what it became, either
            //! of them absent for a drawing that was added or removed.
            struct AnnotationChange
            {
                std::string id;
                std::optional<ReviewAnnotation> before;
                std::optional<ReviewAnnotation> after;

                //! Where the drawing was in the list, so that undoing its
                //! removal puts it back in its place: the order of the
                //! drawings is the order their strokes are drawn in.
                size_t index = 0;
            };

            //! The changes that take one list of drawings to another, by
            //! identifier.
            std::vector<AnnotationChange> diffAnnotations(
                const std::vector<ReviewAnnotation>& before,
                const std::vector<ReviewAnnotation>& after)
            {
                std::vector<AnnotationChange> out;
                for (size_t i = 0; i < before.size(); ++i)
                {
                    const auto j = std::find_if(
                        after.begin(),
                        after.end(),
                        [&before, i](const ReviewAnnotation& value) { return value.id == before[i].id; });
                    if (j == after.end())
                    {
                        out.push_back({ before[i].id, before[i], std::nullopt, i });
                    }
                    else if (*j != before[i])
                    {
                        out.push_back({ before[i].id, before[i], *j, i });
                    }
                }
                for (size_t i = 0; i < after.size(); ++i)
                {
                    const auto j = std::find_if(
                        before.begin(),
                        before.end(),
                        [&after, i](const ReviewAnnotation& value) { return value.id == after[i].id; });
                    if (j == before.end())
                    {
                        out.push_back({ after[i].id, std::nullopt, after[i], i });
                    }
                }
                return out;
            }

            //! Set each changed drawing to one side of its change, leaving
            //! every other drawing as it is now.
            std::vector<ReviewAnnotation> applyChanges(
                std::vector<ReviewAnnotation> annotations,
                const std::vector<AnnotationChange>& changes,
                bool undo)
            {
                for (const auto& change : changes)
                {
                    const auto& target = undo ? change.before : change.after;
                    const auto i = std::find_if(
                        annotations.begin(),
                        annotations.end(),
                        [&change](const ReviewAnnotation& value) { return value.id == change.id; });
                    if (target.has_value())
                    {
                        if (i != annotations.end())
                        {
                            *i = target.value();
                        }
                        else
                        {
                            annotations.insert(
                                annotations.begin() + std::min(change.index, annotations.size()),
                                target.value());
                        }
                    }
                    else if (i != annotations.end())
                    {
                        annotations.erase(i);
                    }
                }
                return annotations;
            }

            //! A command that changes some of the drawings.
            //!
            //! Only the drawings the user changed are recorded, before and
            //! after, rather than the whole list. In a shared session other
            //! people's drawings change underneath the history, and replaying
            //! a snapshot of the whole list would take their changes back.
            class ChangeCommand : public ftk::ICommand
            {
            public:
                ChangeCommand(
                    const std::function<void(const std::vector<AnnotationChange>&, bool)>& apply,
                    const std::vector<AnnotationChange>& changes) :
                    _apply(apply),
                    _changes(changes)
                {}

                void exec() override { _apply(_changes, false); }
                void undo() override { _apply(_changes, true); }

            private:
                std::function<void(const std::vector<AnnotationChange>&, bool)> _apply;
                std::vector<AnnotationChange> _changes;
            };

            //! The distance from a point to a segment.
            float distanceToSegment(
                const ftk::V2F& p,
                const ftk::V2F& a,
                const ftk::V2F& b)
            {
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;
                const float lengthSquared = dx * dx + dy * dy;
                float t = 0.F;
                if (lengthSquared > 0.F)
                {
                    t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared;
                    t = std::max(0.F, std::min(1.F, t));
                }
                const float x = a.x + t * dx;
                const float y = a.y + t * dy;
                return std::sqrt((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y));
            }

            //! Does the stroke pass within the radius of the position?
            bool strokeHit(const ReviewStroke& stroke, const ftk::V2F& pos, float radius)
            {
                // Include the stroke's own width, so a thick stroke is as easy to
                // hit as it looks.
                const float threshold = radius + stroke.width / 2.F;
                if (stroke.points.empty())
                {
                    return false;
                }
                if (1 == stroke.points.size())
                {
                    return distanceToSegment(pos, stroke.points[0], stroke.points[0]) <= threshold;
                }
                for (size_t i = 0; i + 1 < stroke.points.size(); ++i)
                {
                    if (distanceToSegment(pos, stroke.points[i], stroke.points[i + 1]) <= threshold)
                    {
                        return true;
                    }
                }
                return false;
            }
        }

        struct AnnotationsModel::Private
        {
            std::shared_ptr<ftk::ObservableList<ReviewAnnotation> > annotations;
            std::shared_ptr<ftk::CommandStack> commandStack;
            std::string user;
        };

        void AnnotationsModel::_init()
        {
            FTK_P();
            p.annotations = ftk::ObservableList<ReviewAnnotation>::create();
            p.commandStack = ftk::CommandStack::create();
            p.user = reviewAuthor();
        }

        AnnotationsModel::AnnotationsModel() :
            _p(new Private)
        {}

        AnnotationsModel::~AnnotationsModel()
        {}

        std::shared_ptr<AnnotationsModel> AnnotationsModel::create()
        {
            auto out = std::shared_ptr<AnnotationsModel>(new AnnotationsModel);
            out->_init();
            return out;
        }

        const std::vector<ReviewAnnotation>& AnnotationsModel::getAnnotations() const
        {
            return _p->annotations->get();
        }

        std::shared_ptr<ftk::IObservableList<ReviewAnnotation> > AnnotationsModel::observeAnnotations() const
        {
            return _p->annotations;
        }

        const std::string& AnnotationsModel::getUser() const
        {
            return _p->user;
        }

        void AnnotationsModel::setUser(const std::string& value)
        {
            _p->user = value;
        }

        std::vector<ReviewStroke> AnnotationsModel::getStrokes(
            const std::string& sourceId,
            const OTIO_NS::RationalTime& time) const
        {
            FTK_P();
            std::vector<ReviewStroke> out;
            for (const auto& annotation : p.annotations->get())
            {
                if (annotation.sourceId == sourceId &&
                    sameTime(annotation.time, time))
                {
                    out.insert(out.end(), annotation.strokes.begin(), annotation.strokes.end());
                }
            }
            return out;
        }

        void AnnotationsModel::setAnnotations(const std::vector<ReviewAnnotation>& value)
        {
            FTK_P();
            p.commandStack->clear();
            p.annotations->setIfChanged(value);
        }

        void AnnotationsModel::addStroke(
            const std::string& sourceId,
            const OTIO_NS::RationalTime& time,
            const ReviewStroke& stroke)
        {
            FTK_P();
            if (stroke.points.empty())
            {
                return;
            }
            auto annotations = p.annotations->get();
            const auto i = std::find_if(
                annotations.begin(),
                annotations.end(),
                [&sourceId, &time, &p](const ReviewAnnotation& value)
                {
                    return
                        value.sourceId == sourceId &&
                        sameTime(value.time, time) &&
                        value.author == p.user;
                });
            if (i != annotations.end())
            {
                i->strokes.push_back(stroke);
                i->modified = timestamp();
                i->modifiedBy = p.user;
            }
            else
            {
                ReviewAnnotation annotation;
                annotation.id = generateId();
                annotation.sourceId = sourceId;
                annotation.time = time;
                // Attributed once, when the user's drawing on this frame
                // starts: the strokes added to it afterwards are the same
                // hand's, and someone else's go into a drawing of their own.
                annotation.author = p.user;
                annotation.created = timestamp();
                annotation.strokes.push_back(stroke);
                annotations.push_back(annotation);
            }
            _push(annotations);
        }

        void AnnotationsModel::eraseStrokes(
            const std::string& sourceId,
            const OTIO_NS::RationalTime& time,
            const ftk::V2F& pos,
            float radius)
        {
            FTK_P();
            auto annotations = p.annotations->get();
            bool changed = false;
            for (auto i = annotations.begin(); i != annotations.end(); )
            {
                if (i->sourceId == sourceId && sameTime(i->time, time))
                {
                    auto& strokes = i->strokes;
                    const size_t before = strokes.size();
                    strokes.erase(
                        std::remove_if(
                            strokes.begin(),
                            strokes.end(),
                            [&pos, radius](const ReviewStroke& stroke)
                            {
                                return strokeHit(stroke, pos, radius);
                            }),
                        strokes.end());
                    if (strokes.size() != before)
                    {
                        i->modified = timestamp();
                        i->modifiedBy = p.user;
                        changed = true;
                    }
                }
                // Drop annotations that no longer hold anything, so they do not
                // leave a phantom timeline marker.
                if (i->strokes.empty())
                {
                    i = annotations.erase(i);
                }
                else
                {
                    ++i;
                }
            }
            if (changed)
            {
                _push(annotations);
            }
        }

        void AnnotationsModel::clearFrame(
            const std::vector<std::string>& sourceIds,
            const OTIO_NS::RationalTime& time)
        {
            FTK_P();
            auto annotations = p.annotations->get();
            const size_t before = annotations.size();
            annotations.erase(
                std::remove_if(
                    annotations.begin(),
                    annotations.end(),
                    [&sourceIds, &time](const ReviewAnnotation& value)
                    {
                        return sameTime(value.time, time) &&
                            std::find(
                                sourceIds.begin(),
                                sourceIds.end(),
                                value.sourceId) != sourceIds.end();
                    }),
                annotations.end());
            if (annotations.size() != before)
            {
                _push(annotations);
            }
        }

        void AnnotationsModel::removeAnnotation(const std::string& id)
        {
            FTK_P();
            auto annotations = p.annotations->get();
            const auto i = std::find_if(
                annotations.begin(),
                annotations.end(),
                [&id](const ReviewAnnotation& value) { return value.id == id; });
            if (i != annotations.end())
            {
                annotations.erase(i);
                _push(annotations);
            }
        }

        void AnnotationsModel::upsert(const ReviewAnnotation& annotation)
        {
            FTK_P();
            auto annotations = p.annotations->get();
            const auto i = std::find_if(
                annotations.begin(),
                annotations.end(),
                [&annotation](const ReviewAnnotation& value) { return value.id == annotation.id; });
            if (i != annotations.end())
            {
                *i = annotation;
            }
            else
            {
                annotations.push_back(annotation);
            }
            p.annotations->setIfChanged(annotations);
        }

        void AnnotationsModel::remove(const std::string& id)
        {
            FTK_P();
            auto annotations = p.annotations->get();
            const auto i = std::find_if(
                annotations.begin(),
                annotations.end(),
                [&id](const ReviewAnnotation& value) { return value.id == id; });
            if (i != annotations.end())
            {
                annotations.erase(i);
                p.annotations->setIfChanged(annotations);
            }
        }

        void AnnotationsModel::clear()
        {
            FTK_P();
            p.commandStack->clear();
            p.annotations->setIfChanged({});
        }

        std::shared_ptr<ftk::IObservable<bool> > AnnotationsModel::observeHasUndo() const
        {
            return _p->commandStack->observeHasUndo();
        }

        std::shared_ptr<ftk::IObservable<bool> > AnnotationsModel::observeHasRedo() const
        {
            return _p->commandStack->observeHasRedo();
        }

        void AnnotationsModel::undo()
        {
            _p->commandStack->undo();
        }

        void AnnotationsModel::redo()
        {
            _p->commandStack->redo();
        }

        void AnnotationsModel::_set(const std::vector<ReviewAnnotation>& value)
        {
            _p->annotations->setIfChanged(value);
        }

        void AnnotationsModel::_push(const std::vector<ReviewAnnotation>& value)
        {
            FTK_P();
            const auto changes = diffAnnotations(p.annotations->get(), value);
            if (changes.empty())
            {
                return;
            }
            p.commandStack->push(std::make_shared<ChangeCommand>(
                [this](const std::vector<AnnotationChange>& changes, bool undo)
                {
                    _set(applyChanges(_p->annotations->get(), changes, undo));
                },
                changes));
        }
    }
}
