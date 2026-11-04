// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <djv/Models/Review.h>

#include <ftk/Core/ObservableList.h>
#include <ftk/Core/Observable.h>

namespace ftk
{
    class CommandStack;
}

namespace djv
{
    namespace models
    {
        //! The drawing annotations of a review.
        //!
        //! Adding and erasing strokes go through a command stack, so they can be
        //! undone and redone. Annotations are session state: they live in the
        //! review file, not in the application settings.
        //!
        //! Each person has a drawing of their own on a frame: the strokes the
        //! user adds go into the user's drawing (see setUser()), and the frame
        //! shows every drawing on it. In a shared session that keeps two
        //! people drawing on one frame from overwriting each other, since the
        //! sync server takes the last change to a drawing as a whole.
        class DJV_MODELS_API_TYPE AnnotationsModel : public std::enable_shared_from_this<AnnotationsModel>
        {
            FTK_NON_COPYABLE(AnnotationsModel);

        protected:
            DJV_MODELS_API void _init();

            AnnotationsModel();

        public:
            DJV_MODELS_API ~AnnotationsModel();

            //! Create a new model.
            DJV_MODELS_API static std::shared_ptr<AnnotationsModel> create();

            //! Get the annotations.
            DJV_MODELS_API const std::vector<ReviewAnnotation>& getAnnotations() const;

            //! Observe the annotations.
            DJV_MODELS_API std::shared_ptr<ftk::IObservableList<ReviewAnnotation> > observeAnnotations() const;

            //! Get the user the strokes drawn here are attributed to.
            DJV_MODELS_API const std::string& getUser() const;

            //! Set the user the strokes drawn here are attributed to: the
            //! user signed in to the sync server, or reviewAuthor() when no
            //! one is, which is also the default.
            DJV_MODELS_API void setUser(const std::string&);

            //! Get the strokes drawn on the given source and frame, every
            //! person's drawing in turn, or an empty list if there are none.
            DJV_MODELS_API std::vector<ReviewStroke> getStrokes(
                const std::string& sourceId,
                const OTIO_NS::RationalTime&) const;

            //! Replace all the annotations, e.g. when a review is opened. This
            //! clears the undo history.
            DJV_MODELS_API void setAnnotations(const std::vector<ReviewAnnotation>&);

            //! Add a stroke to the user's drawing on the given source and frame,
            //! starting one when the user has none there yet.
            DJV_MODELS_API void addStroke(
                const std::string& sourceId,
                const OTIO_NS::RationalTime&,
                const ReviewStroke&);

            //! Erase the strokes of the given source and frame whose path passes
            //! within the radius of the position. The position and the radius are
            //! in the pixels of the source image. Every drawing on the frame
            //! is erased, whoever drew it, and each one changed records the
            //! user as the one who changed it.
            DJV_MODELS_API void eraseStrokes(
                const std::string& sourceId,
                const OTIO_NS::RationalTime&,
                const ftk::V2F& pos,
                float radius);

            //! Remove every stroke of the given sources and frame. One call
            //! for all of the sources, so that clearing a compared frame is
            //! a single undo step.
            DJV_MODELS_API void clearFrame(
                const std::vector<std::string>& sourceIds,
                const OTIO_NS::RationalTime&);

            //! Remove the drawing with the given identifier, as one undo step.
            DJV_MODELS_API void removeAnnotation(const std::string& id);

            //! Remove all the annotations, clearing the undo history.
            DJV_MODELS_API void clear();

            //! \name Remote Changes
            //! Changes that arrive from somewhere else, e.g. a sync server.
            //! The drawing is taken as it stands -- nothing is filled in --
            //! and the change is not an undo step: undoing is for what the
            //! user did here. The undo history only ever replays its own
            //! drawings, so it does not take back a remote change to another.
            ///@{

            //! Add the drawing, or replace the one with the same identifier.
            DJV_MODELS_API void upsert(const ReviewAnnotation&);

            //! Remove the drawing with the given identifier.
            DJV_MODELS_API void remove(const std::string& id);

            ///@}

            //! \name Undo
            ///@{

            DJV_MODELS_API std::shared_ptr<ftk::IObservable<bool> > observeHasUndo() const;
            DJV_MODELS_API std::shared_ptr<ftk::IObservable<bool> > observeHasRedo() const;
            DJV_MODELS_API void undo();
            DJV_MODELS_API void redo();

            ///@}

        private:
            //! Replace the whole list without touching the undo history.
            DJV_MODELS_API void _set(const std::vector<ReviewAnnotation>&);

            //! Push a command that moves from the current list to the given
            //! one, recording only the drawings that differ.
            DJV_MODELS_API void _push(const std::vector<ReviewAnnotation>&);

            FTK_PRIVATE();
        };
    }
}
