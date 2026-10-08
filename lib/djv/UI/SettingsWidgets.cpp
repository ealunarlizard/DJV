// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/SettingsWidgets.h>

#include <djv/Models/LanModel.h>
#include <djv/Models/NetworkDrive.h>
#include <djv/Models/SyncModel.h>
#include <djv/Models/TimeUnitsModel.h>

#if defined(TLRENDER_USD)
#include <tlRender/IO/USD.h>
#endif // TLRENDER_USD

#include <ftk/UI/Bellows.h>
#include <ftk/UI/CheckBox.h>
#include <ftk/UI/ColorSwatch.h>
#include <ftk/UI/ComboBox.h>
#include <ftk/UI/DialogSystem.h>
#include <ftk/UI/Divider.h>
#include <ftk/UI/DoubleEdit.h>
#include <ftk/UI/FileBrowser.h>
#include <ftk/UI/FileEdit.h>
#include <ftk/UI/FloatEdit.h>
#include <ftk/UI/FloatEditSlider.h>
#include <ftk/UI/FormLayout.h>
#include <ftk/UI/IntEdit.h>
#include <ftk/UI/IntEditSlider.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/LineEdit.h>
#include <ftk/UI/PushButton.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/ScreenshotTag.h>
#include <ftk/UI/ScrollWidget.h>
#include <ftk/Core/Format.h>

namespace djv
{
    namespace ui
    {
        ISettingsWidget::~ISettingsWidget()
        {}

        struct AudioSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::IntEdit> bufferFramesEdit;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::AudioSettings> > settingsObserver;
        };

        void AudioSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::AudioSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.bufferFramesEdit = ftk::IntEdit::create(context);
            p.bufferFramesEdit->setRange(0, 1000000);
            p.bufferFramesEdit->setStep(256);
            p.bufferFramesEdit->setLargeStep(1024);
            p.bufferFramesEdit->setTooltip(
                "The size of the buffer the audio device is asked for, in\n"
                "sample frames. Zero is the device's own default.\n"
                "\n"
                "Increase this if the audio breaks up or crackles during\n"
                "playback. Smaller values reduce the audio latency.\n"
                "\n"
                "A change is applied the next time the audio device is\n"
                "opened: with every file closed, or when the application\n"
                "starts.");

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Buffer frames:", p.bufferFramesEdit);

            p.settingsObserver = ftk::Observer<models::AudioSettings>::create(
                settings->observeAudio(),
                [this](const models::AudioSettings& value)
                {
                    _p->bufferFramesEdit->setValue(value.bufferFrameCount);
                });

            p.bufferFramesEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    models::AudioSettings settings = p.settings->getAudio();
                    settings.bufferFrameCount = value;
                    p.settings->setAudio(settings);
                });
        }

        AudioSettingsWidget::AudioSettingsWidget() :
            _p(new Private)
        {}

        AudioSettingsWidget::~AudioSettingsWidget()
        {}

        std::shared_ptr<AudioSettingsWidget> AudioSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<AudioSettingsWidget>(new AudioSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct CacheSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::FloatEdit> videoEdit;
            std::shared_ptr<ftk::FloatEdit> audioEdit;
            std::shared_ptr<ftk::FloatEdit> readBehindEdit;
            std::shared_ptr<ftk::FloatEdit> thumbnailEdit;
            std::shared_ptr<ftk::FloatEdit> waveformEdit;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<tl::PlayerCacheOptions> > cacheObserver;
            std::shared_ptr<ftk::Observer<tl::ui::ThumbnailCacheOptions> > thumbnailCacheObserver;
        };

        void CacheSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::CacheSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            // No further than the machine itself: a cache larger than the
            // memory there is takes the machine down with it when a file
            // fills it (DJV #664).
            const float maxCacheGB = models::SettingsModel::getMaxCacheGB();

            p.videoEdit = ftk::FloatEdit::create(context);
            p.videoEdit->setRange(0.F, maxCacheGB);
            p.videoEdit->setStep(1.0);
            p.videoEdit->setLargeStep(10.0);
            // What the number is, because it reads as a limit on DJV's
            // memory and is not one (DJV #873).
            p.videoEdit->setTooltip(
                "Memory for decoded video frames.\n"
                "\n"
                "DJV uses more than this in total: the audio cache, the\n"
                "decoders, the thumbnails and waveforms, the GPU, and the\n"
                "application itself come on top. That is roughly a fixed\n"
                "amount rather than a share of the cache.");

            p.audioEdit = ftk::FloatEdit::create(context);
            p.audioEdit->setRange(0.F, maxCacheGB);
            p.audioEdit->setStep(1.0);
            p.audioEdit->setLargeStep(10.0);
            p.audioEdit->setTooltip(
                "Memory for decoded audio, separate from the video cache.\n"
                "\n"
                "It fills with as much of the in/out range as fits, so a\n"
                "long file uses all of it.");

            p.readBehindEdit = ftk::FloatEdit::create(context);
            p.readBehindEdit->setRange(0.F, 10.F);
            p.readBehindEdit->setStep(0.1);
            p.readBehindEdit->setLargeStep(1.0);

            p.thumbnailEdit = ftk::FloatEdit::create(context);
            p.thumbnailEdit->setRange(0.F, 1024.F);
            p.thumbnailEdit->setStep(1.0);
            p.thumbnailEdit->setLargeStep(10.0);

            p.waveformEdit = ftk::FloatEdit::create(context);
            p.waveformEdit->setRange(0.F, 1024.F);
            p.waveformEdit->setStep(1.0);
            p.waveformEdit->setLargeStep(10.0);

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            auto hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.videoEdit->setParent(hLayout);
            // Read as a sentence with the number in the edit: "8 of 14 GB".
            // What the second number is goes in the tooltip, since it is
            // the largest cache this machine allows rather than the memory
            // it has (DJV #664).
            const std::string cacheMaxText =
                ftk::Format("of {0} GB").arg(maxCacheGB, 0).str();
            const std::string cacheMaxTooltip =
                "The largest cache this machine allows: the memory it has,\n"
                "less the couple of gigabytes DJV itself takes to run.\n"
                "\n"
                "Not a promise of free memory: whatever else is open has\n"
                "its own share.";
            auto label = ftk::Label::create(context, cacheMaxText, hLayout);
            label->setTooltip(cacheMaxTooltip);
            p.layout->addRow("Video cache:", hLayout);
            hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.audioEdit->setParent(hLayout);
            label = ftk::Label::create(context, cacheMaxText, hLayout);
            label->setTooltip(cacheMaxTooltip);
            p.layout->addRow("Audio cache:", hLayout);
            hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.readBehindEdit->setParent(hLayout);
            ftk::Label::create(context, "seconds", hLayout);
            p.layout->addRow("Read behind:", hLayout);
            hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.thumbnailEdit->setParent(hLayout);
            ftk::Label::create(context, "MB", hLayout);
            p.layout->addRow("Thumbnails:", hLayout);
            hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.waveformEdit->setParent(hLayout);
            ftk::Label::create(context, "MB", hLayout);
            p.layout->addRow("Waveforms:", hLayout);

            p.cacheObserver = ftk::Observer<tl::PlayerCacheOptions>::create(
                settings->observeCache(),
                [this](const tl::PlayerCacheOptions& value)
                {
                    FTK_P();
                    p.videoEdit->setValue(value.videoGB);
                    p.audioEdit->setValue(value.audioGB);
                    p.readBehindEdit->setValue(value.readBehind);
                });

            p.thumbnailCacheObserver = ftk::Observer<tl::ui::ThumbnailCacheOptions>::create(
                settings->observeThumbnailCache(),
                [this](const tl::ui::ThumbnailCacheOptions& value)
                {
                    FTK_P();
                    p.thumbnailEdit->setValue(value.thumbnailMB);
                    p.waveformEdit->setValue(value.waveformMB);
                });

            p.videoEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::PlayerCacheOptions settings = p.settings->getCache();
                    settings.videoGB = value;
                    p.settings->setCache(settings);
                });

            p.audioEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::PlayerCacheOptions settings = p.settings->getCache();
                    settings.audioGB = value;
                    p.settings->setCache(settings);
                });

            p.readBehindEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::PlayerCacheOptions options = p.settings->getCache();
                    options.readBehind = value;
                    p.settings->setCache(options);
                });

            p.thumbnailEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::ui::ThumbnailCacheOptions options = p.settings->getThumbnailCache();
                    options.thumbnailMB = value;
                    p.settings->setThumbnailCache(options);
                });

            p.waveformEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::ui::ThumbnailCacheOptions options = p.settings->getThumbnailCache();
                    options.waveformMB = value;
                    p.settings->setThumbnailCache(options);
                });
        }

        CacheSettingsWidget::CacheSettingsWidget() :
            _p(new Private)
        {}

        CacheSettingsWidget::~CacheSettingsWidget()
        {}

        std::shared_ptr<CacheSettingsWidget> CacheSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<CacheSettingsWidget>(new CacheSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct FileBrowserSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::CheckBox> nfdCheckBox;
            std::shared_ptr<ftk::CheckBox> floatingCheckBox;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::FileBrowserSettings> > settingsObserver;
        };

        void FileBrowserSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::FileBrowserSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.nfdCheckBox = ftk::CheckBox::create(context);
            p.nfdCheckBox->setHStretch(ftk::Stretch::Expanding);

            p.floatingCheckBox = ftk::CheckBox::create(context);
            p.floatingCheckBox->setHStretch(ftk::Stretch::Expanding);
            p.floatingCheckBox->setTooltip(
                "Show the file browser in a window of its own, so that it\n"
                "does not cover what is being played while you browse.\n"
                "\n"
                "Takes effect the next time the file browser is opened.");

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Native file dialog:", p.nfdCheckBox);
            p.layout->addRow("Floating window:", p.floatingCheckBox);

            // The native dialog is not built everywhere: it means linking to
            // the desktop's own, which is not wanted on Linux. Where it is not
            // built the setting is remembered but does nothing, so it is shown
            // as what it is rather than as a choice.
            const bool nativeAvailable = context->getSystem<ftk::FileBrowserSystem>()->
                isNativeFileDialogAvailable();
            p.nfdCheckBox->setEnabled(nativeAvailable);
            if (!nativeAvailable)
            {
                p.nfdCheckBox->setTooltip(
                    "The native file dialog is not available in this build.");
            }

            p.settingsObserver = ftk::Observer<models::FileBrowserSettings>::create(
                settings->observeFileBrowser(),
                [this, nativeAvailable](const models::FileBrowserSettings& value)
                {
                    FTK_P();
                    p.nfdCheckBox->setChecked(value.nativeFileDialog);
                    p.floatingCheckBox->setChecked(value.floating);
                    // The native dialog is a window of its own already, and
                    // is not ours to place -- but only where there is one.
                    p.floatingCheckBox->setEnabled(
                        !(nativeAvailable && value.nativeFileDialog));
                });

            p.nfdCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    auto settings = p.settings->getFileBrowser();
                    settings.nativeFileDialog = value;
                    p.settings->setFileBrowser(settings);
                });

            p.floatingCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    auto settings = p.settings->getFileBrowser();
                    settings.floating = value;
                    p.settings->setFileBrowser(settings);
                });
        }

        FileBrowserSettingsWidget::FileBrowserSettingsWidget() :
            _p(new Private)
        {}

        FileBrowserSettingsWidget::~FileBrowserSettingsWidget()
        {}

        std::shared_ptr<FileBrowserSettingsWidget> FileBrowserSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<FileBrowserSettingsWidget>(new FileBrowserSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct OTIOSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::ComboBox> spatialComboBox;
            std::shared_ptr<ftk::CheckBox> compatCheckBox;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::OTIOSettings> > settingsObserver;
        };

        void OTIOSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::OTIOSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.spatialComboBox = ftk::ComboBox::create(context, tl::getSpatialLabels());
            p.spatialComboBox->setHStretch(ftk::Stretch::Expanding);
            p.spatialComboBox->setTooltip(
                "Use the spatial coordinates in OTIO files to position and\n"
                "size the images.\n"
                "\n"
                "* None: Ignore the spatial coordinates.\n"
                "* Coordinates: Use the spatial coordinates where clips\n"
                "  provide them.\n"
                "* Normalize: Use the spatial coordinates, and display clips\n"
                "  without them at the size of the first clip. Use this to\n"
                "  play clips of differing resolutions at the same size.");

            p.compatCheckBox = ftk::CheckBox::create(context);
            p.compatCheckBox->setHStretch(ftk::Stretch::Expanding);
            p.compatCheckBox->setTooltip(
                "Enable workarounds for timelines that may not conform exactly\n"
                "to specification.");

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Spatial coordinates:", p.spatialComboBox);
            p.layout->addRow("Compatibility:", p.compatCheckBox);

            p.settingsObserver = ftk::Observer<models::OTIOSettings>::create(
                settings->observeOTIO(),
                [this](const models::OTIOSettings& value)
                {
                    FTK_P();
                    p.spatialComboBox->setCurrentIndex(
                        static_cast<int>(value.spatial));
                    p.compatCheckBox->setChecked(value.compat);
                });

            p.spatialComboBox->setIndexCallback(
                [this](int value)
                {
                    FTK_P();
                    models::OTIOSettings settings = p.settings->getOTIO();
                    settings.spatial = static_cast<tl::Spatial>(value);
                    p.settings->setOTIO(settings);
                });

            p.compatCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    models::OTIOSettings settings = p.settings->getOTIO();
                    settings.compat = value;
                    p.settings->setOTIO(settings);
                });
        }

        OTIOSettingsWidget::OTIOSettingsWidget() :
            _p(new Private)
        {}

        OTIOSettingsWidget::~OTIOSettingsWidget()
        {}

        std::shared_ptr<OTIOSettingsWidget> OTIOSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<OTIOSettingsWidget>(new OTIOSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct ImageSeqSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::ComboBox> audioComboBox;
            std::shared_ptr<ftk::LineEdit> audioExtensionsEdit;
            std::shared_ptr<ftk::LineEdit> audioFileNameEdit;
            std::shared_ptr<ftk::IntEdit> maxDigitsEdit;
            std::shared_ptr<ftk::DoubleEdit> defaultSpeedEdit;
            std::shared_ptr<ftk::ComboBox> missingFramesComboBox;
            std::shared_ptr<ftk::CheckBox> missingIndicatorCheckBox;
            std::shared_ptr<ftk::IntEditSlider> missingIndicatorWidthEdit;
            std::shared_ptr<ftk::ColorSwatch> missingIndicatorColorSwatch;
            std::shared_ptr<ftk::IntEdit> threadsEdit;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::ImageSeqSettings> > settingsObserver;
            std::shared_ptr<ftk::Observer<tl::ForegroundOptions> > foregroundObserver;
        };

        void ImageSeqSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::ImageSeqSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.audioComboBox = ftk::ComboBox::create(context, tl::getImageSeqAudioLabels());
            p.audioComboBox->setHStretch(ftk::Stretch::Expanding);
            p.audioComboBox->setTooltip(
                "Open audio files for image sequences.\n"
                "\n"
                "* None: Do not open audio files.\n"
                "* Ext: Find audio files by extension.\n"
                "* Filename: Specify the file name to open.");

            p.audioExtensionsEdit = ftk::LineEdit::create(context);
            p.audioExtensionsEdit->setHStretch(ftk::Stretch::Expanding);
            p.audioExtensionsEdit->setTooltip(
                "List of audio file extensions to search for.\n"
                "\n"
                "Example: .wav .mp3");

            p.audioFileNameEdit = ftk::LineEdit::create(context);
            p.audioFileNameEdit->setHStretch(ftk::Stretch::Expanding);
            p.audioFileNameEdit->setTooltip("Audio file name to open.");

            p.maxDigitsEdit = ftk::IntEdit::create(context);
            p.maxDigitsEdit->setTooltip(
                "Maximum number of digits allowed in a frame number.");

            p.defaultSpeedEdit = ftk::DoubleEdit::create(context);
            p.defaultSpeedEdit->setRange(1.0, 120.0);

            p.missingFramesComboBox = ftk::ComboBox::create(
                context, tl::getMissingFramesLabels());
            p.missingFramesComboBox->setHStretch(ftk::Stretch::Expanding);
            p.missingFramesComboBox->setTooltip(
                "What to show for a frame a sequence does not have, which\n"
                "happens with a render still in progress.\n"
                "\n"
                "* Error: The frame does not read.\n"
                "* Hold: Repeat the nearest frame before it.\n"
                "* Black: A blank frame.\n"
                "* Skip: Leave it out, so only the frames that are there\n"
                "  play and the sequence is as long as they are.\n"
                "* Gaps: Leave a hole, so every frame keeps the time it\n"
                "  had and the gaps show on the timeline.\n"
                "\n"
                "The first three are decided as each frame is read, and a\n"
                "timeline that says what it wants is followed instead. The\n"
                "last two decide what the timeline is built from, so they\n"
                "only apply to an image sequence opened on its own, and\n"
                "changing to or from one opens the file again.");

            p.missingIndicatorCheckBox = ftk::CheckBox::create(context);
            p.missingIndicatorCheckBox->setTooltip(
                "Mark a picture that stands in for a frame the sequence does\n"
                "not have, so a render still in progress does not look\n"
                "finished.\n"
                "\n"
                "A cross is drawn over the picture, and the heads up display\n"
                "time says which frame is being held.\n"
                "\n"
                "Applies to Hold and Black. The others either show nothing or\n"
                "leave no missing frames to mark. Unlike the setting above,\n"
                "this is only what is drawn, so it applies to what is already\n"
                "open.");
            ftk::setScreenshotTag(
                p.missingIndicatorCheckBox, "Settings.MissingIndicator.Enabled");

            p.missingIndicatorWidthEdit = ftk::IntEditSlider::create(context);
            p.missingIndicatorWidthEdit->setRange(1, 40);
            ftk::setScreenshotTag(
                p.missingIndicatorWidthEdit, "Settings.MissingIndicator.LineWidth");

            p.missingIndicatorColorSwatch = ftk::ColorSwatch::create(context);
            p.missingIndicatorColorSwatch->setEditable(true);
            p.missingIndicatorColorSwatch->setHAlign(ftk::HAlign::Left);
            ftk::setScreenshotTag(
                p.missingIndicatorColorSwatch, "Settings.MissingIndicator.Color");

            p.threadsEdit = ftk::IntEdit::create(context);
            p.threadsEdit->setRange(1, 64);

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Open audio files:", p.audioComboBox);
            p.layout->addRow("Audio file extensions:", p.audioExtensionsEdit);
            p.layout->addRow("Audio file name:", p.audioFileNameEdit);
            p.layout->addRow("Maximum digits:", p.maxDigitsEdit);
            auto hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.defaultSpeedEdit->setParent(hLayout);
            ftk::Label::create(context, "FPS", hLayout);
            p.layout->addRow("Default speed:", hLayout);
            p.layout->addRow("Missing frames:", p.missingFramesComboBox);
            p.layout->addRow("Indicate missing:", p.missingIndicatorCheckBox);
            p.layout->addRow("Indicator width:", p.missingIndicatorWidthEdit);
            p.layout->addRow("Indicator color:", p.missingIndicatorColorSwatch);
            p.layout->addRow("I/O threads:", p.threadsEdit);

            // The indicator lives with the viewport's other drawing rather
            // than with the sequence settings, since it changes only what is
            // drawn. It is shown here because this is where someone deciding
            // what to do about missing frames is looking.
            p.foregroundObserver = ftk::Observer<tl::ForegroundOptions>::create(
                viewportModel->observeForegroundOptions(),
                [this](const tl::ForegroundOptions& value)
                {
                    FTK_P();
                    p.missingIndicatorCheckBox->setChecked(
                        value.missingIndicator.enabled);
                    p.missingIndicatorWidthEdit->setValue(
                        value.missingIndicator.width);
                    p.missingIndicatorColorSwatch->setColor(
                        value.missingIndicator.color);
                });

            p.missingIndicatorCheckBox->setCheckedCallback(
                [viewportModel](bool value)
                {
                    auto options = viewportModel->getForegroundOptions();
                    options.missingIndicator.enabled = value;
                    viewportModel->setForegroundOptions(options);
                });

            p.missingIndicatorWidthEdit->setCallback(
                [viewportModel](int value)
                {
                    auto options = viewportModel->getForegroundOptions();
                    options.missingIndicator.width = value;
                    viewportModel->setForegroundOptions(options);
                });

            p.missingIndicatorColorSwatch->setCallback(
                [viewportModel](const ftk::Color4F& value)
                {
                    auto options = viewportModel->getForegroundOptions();
                    options.missingIndicator.color = value;
                    viewportModel->setForegroundOptions(options);
                });

            p.settingsObserver = ftk::Observer<models::ImageSeqSettings>::create(
                settings->observeImageSeq(),
                [this](const models::ImageSeqSettings& value)
                {
                    FTK_P();
                    p.audioComboBox->setCurrentIndex(static_cast<int>(value.audio));
                    p.audioExtensionsEdit->setText(ftk::join(value.audioExts, ' '));
                    p.layout->setRowVisible(p.audioExtensionsEdit, tl::ImageSeqAudio::Ext == value.audio);
                    p.audioFileNameEdit->setText(value.audioFileName);
                    p.layout->setRowVisible(p.audioFileNameEdit, tl::ImageSeqAudio::FileName == value.audio);
                    p.maxDigitsEdit->setValue(value.maxDigits);
                    p.defaultSpeedEdit->setValue(value.io.defaultSpeed);
                    p.missingFramesComboBox->setCurrentIndex(
                        static_cast<int>(value.io.missingFrames));
                    p.threadsEdit->setValue(value.readThreadCount);
                });

            p.audioComboBox->setIndexCallback(
                [this](int value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.audio = static_cast<tl::ImageSeqAudio>(value);
                    p.settings->setImageSeq(settings);
                });

            p.audioExtensionsEdit->setCallback(
                [this](const std::string& value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.audioExts = ftk::split(value, ' ');
                    p.settings->setImageSeq(settings);
                });

            p.audioFileNameEdit->setCallback(
                [this](const std::string& value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.audioFileName = value;
                    p.settings->setImageSeq(settings);
                });

            p.maxDigitsEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.maxDigits = value;
                    p.settings->setImageSeq(settings);
                });

            p.defaultSpeedEdit->setCallback(
                [this](double value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.io.defaultSpeed = value;
                    p.settings->setImageSeq(settings);
                });

            p.missingFramesComboBox->setIndexCallback(
                [this](int value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.io.missingFrames = static_cast<tl::MissingFrames>(value);
                    p.settings->setImageSeq(settings);
                });

            p.threadsEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    models::ImageSeqSettings settings = p.settings->getImageSeq();
                    settings.readThreadCount = value;
                    p.settings->setImageSeq(settings);
                });
        }

        ImageSeqSettingsWidget::ImageSeqSettingsWidget() :
            _p(new Private)
        {}

        ImageSeqSettingsWidget::~ImageSeqSettingsWidget()
        {}

        std::shared_ptr<ImageSeqSettingsWidget> ImageSeqSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ImageSeqSettingsWidget>(new ImageSeqSettingsWidget);
            out->_init(context, settings, viewportModel, parent);
            return out;
        }

        struct MouseSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;
            std::vector<std::string> buttonLabels;
            std::vector<ftk::KeyModifier> modifiers;
            std::vector<std::string> modifierLabels;
            std::shared_ptr<ftk::FloatEdit> wheelScaleEdit;
            std::shared_ptr<ftk::FloatEdit> frameShuttleScaleEdit;
            std::map<models::MouseAction, std::shared_ptr<ftk::ComboBox> > buttonComboBoxes;
            std::map<models::MouseAction, std::shared_ptr<ftk::ComboBox> > modifierComboBoxes;
            std::map<models::WheelAction, std::shared_ptr<ftk::ComboBox> > wheelComboBoxes;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::MouseSettings> > settingsObserver;
        };

        void MouseSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::MouseSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.modifiers.push_back(ftk::KeyModifier::None);
            p.modifiers.push_back(ftk::KeyModifier::Shift);
            p.modifiers.push_back(ftk::KeyModifier::Control);
            p.modifiers.push_back(ftk::KeyModifier::Alt);
#if defined(__APPLE__)
            p.modifiers.push_back(ftk::KeyModifier::Super);
#endif // __APPLE__
            p.modifierLabels.push_back(ftk::to_string(ftk::KeyModifier::None));
            p.modifierLabels.push_back(ftk::to_string(ftk::KeyModifier::Shift));
            p.modifierLabels.push_back(ftk::to_string(ftk::KeyModifier::Control));
            p.modifierLabels.push_back(ftk::to_string(ftk::KeyModifier::Alt));
#if defined(__APPLE__)
            p.modifierLabels.push_back(ftk::to_string(ftk::KeyModifier::Super));
#endif // __APPLE__

            for (const auto mouseAction : models::getMouseActionEnums())
            {
                p.buttonComboBoxes[mouseAction] = ftk::ComboBox::create(
                    context,
                    ftk::getMouseButtonLabels());
                p.buttonComboBoxes[mouseAction]->setHStretch(ftk::Stretch::Expanding);
                p.modifierComboBoxes[mouseAction] = ftk::ComboBox::create(context, p.modifierLabels);
                p.modifierComboBoxes[mouseAction]->setHStretch(ftk::Stretch::Expanding);
            }

            // The wheel actions ride a modifier rather than a button, so
            // their rows have only the modifier; the missing button box is
            // the information that these ride the wheel.
            for (const auto wheelAction : models::getWheelActionEnums())
            {
                p.wheelComboBoxes[wheelAction] = ftk::ComboBox::create(context, p.modifierLabels);
                p.wheelComboBoxes[wheelAction]->setHStretch(ftk::Stretch::Expanding);
            }

            p.wheelScaleEdit = ftk::FloatEdit::create(context);
            p.wheelScaleEdit->setRange(.5F, 5.F);
            p.wheelScaleEdit->setTooltip(
                "How much one wheel click zooms, in the viewport and the "
                "timeline. The \"-\" and \"=\" keys zoom by a factor of "
                "two regardless.");
            ftk::setScreenshotTag(p.wheelScaleEdit, "Mouse.WheelScale");

            p.frameShuttleScaleEdit = ftk::FloatEdit::create(context);
            p.frameShuttleScaleEdit->setRange(.1F, 10.F);
            ftk::setScreenshotTag(p.frameShuttleScaleEdit, "Mouse.FrameShuttleScale");

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            std::map<models::MouseAction, std::string> mouseActionLabels =
            {
                { models::MouseAction::PanView, "Pan view" },
                { models::MouseAction::CompareWipe, "Compare wipe" },
                { models::MouseAction::Pick, "Pick" },
                { models::MouseAction::FrameShuttle, "Frame shuttle" }
            };
            std::map<models::MouseAction, std::string> mouseActionScreenshots =
            {
                { models::MouseAction::PanView, "Mouse.PanView" },
                { models::MouseAction::CompareWipe, "Mouse.CompareWipe" },
                { models::MouseAction::Pick, "Mouse.Pick" },
                { models::MouseAction::FrameShuttle, "Mouse.FrameShuttle" }
            };
            for (const auto mouseAction : models::getMouseActionEnums())
            {
                auto hLayout = ftk::HorizontalLayout::create(context);
                hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
                hLayout->setHStretch(ftk::Stretch::Expanding);
                p.buttonComboBoxes[mouseAction]->setParent(hLayout);
                p.modifierComboBoxes[mouseAction]->setParent(hLayout);
                p.layout->addRow(ftk::Format("{0}:").
                    arg(mouseActionLabels[mouseAction]),
                    hLayout);
                ftk::setScreenshotTag(hLayout, mouseActionScreenshots[mouseAction]);
            }
            const std::map<models::WheelAction, std::string> wheelActionLabels =
            {
                { models::WheelAction::Zoom, "Wheel zoom" },
                { models::WheelAction::Scrub, "Wheel scrub" }
            };
            const std::map<models::WheelAction, std::string> wheelActionScreenshots =
            {
                { models::WheelAction::Zoom, "Mouse.WheelZoom" },
                { models::WheelAction::Scrub, "Mouse.WheelScrub" }
            };
            for (const auto wheelAction : models::getWheelActionEnums())
            {
                p.layout->addRow(ftk::Format("{0}:").
                    arg(wheelActionLabels.at(wheelAction)),
                    p.wheelComboBoxes[wheelAction]);
                ftk::setScreenshotTag(
                    p.wheelComboBoxes[wheelAction],
                    wheelActionScreenshots.at(wheelAction));
            }
            p.layout->addRow("Zoom scale:", p.wheelScaleEdit);
            p.layout->addRow("Frame shuttle scale:", p.frameShuttleScaleEdit);

            p.settingsObserver = ftk::Observer<models::MouseSettings>::create(
                settings->observeMouse(),
                [this](const models::MouseSettings& value)
                {
                    FTK_P();
                    for (const auto& i : value.bindings)
                    {
                        if (auto j = p.buttonComboBoxes.find(i.first);
                            j != p.buttonComboBoxes.end())
                        {
                            j->second->setCurrentIndex(static_cast<int>(i.second.button));
                        }
                        if (auto j = p.modifierComboBoxes.find(i.first);
                            j != p.modifierComboBoxes.end())
                        {
                            const auto k = std::find(p.modifiers.begin(), p.modifiers.end(), i.second.modifier);
                            if (k != p.modifiers.end())
                            {

                                j->second->setCurrentIndex(k - p.modifiers.begin());
                            }
                        }
                    }
                    for (const auto& i : value.wheelBindings)
                    {
                        if (auto j = p.wheelComboBoxes.find(i.first);
                            j != p.wheelComboBoxes.end())
                        {
                            const auto k = std::find(p.modifiers.begin(), p.modifiers.end(), i.second);
                            if (k != p.modifiers.end())
                            {
                                j->second->setCurrentIndex(k - p.modifiers.begin());
                            }
                        }
                    }
                    p.wheelScaleEdit->setValue(value.wheelScale);
                    p.frameShuttleScaleEdit->setValue(value.frameShuttleScale);
                });

            for (const auto mouseAction : models::getMouseActionEnums())
            {
                p.buttonComboBoxes[mouseAction]->setIndexCallback(
                    [this, mouseAction](int index)
                    {
                        FTK_P();
                        auto settings = p.settings->getMouse();
                        settings.bindings[mouseAction].button = static_cast<ftk::MouseButton>(index);
                        p.settings->setMouse(settings);
                    });
                p.modifierComboBoxes[mouseAction]->setIndexCallback(
                    [this, mouseAction](int index)
                    {
                        FTK_P();
                        if (index >= 0 && index < static_cast<int>(p.modifiers.size()))
                        {
                            auto settings = p.settings->getMouse();
                            settings.bindings[mouseAction].modifier = p.modifiers[index];
                            p.settings->setMouse(settings);
                        }
                    });
            }

            for (const auto wheelAction : models::getWheelActionEnums())
            {
                p.wheelComboBoxes[wheelAction]->setIndexCallback(
                    [this, wheelAction](int index)
                    {
                        FTK_P();
                        if (index >= 0 && index < static_cast<int>(p.modifiers.size()))
                        {
                            auto settings = p.settings->getMouse();
                            settings.wheelBindings[wheelAction] = p.modifiers[index];
                            p.settings->setMouse(settings);
                        }
                    });
            }

            p.wheelScaleEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    auto settings = p.settings->getMouse();
                    settings.wheelScale = value;
                    p.settings->setMouse(settings);
                });

            p.frameShuttleScaleEdit->setCallback(
                [this](float value)
                {
                    FTK_P();
                    auto settings = p.settings->getMouse();
                    settings.frameShuttleScale = value;
                    p.settings->setMouse(settings);
                });
        }

        MouseSettingsWidget::MouseSettingsWidget() :
            _p(new Private)
        {}

        MouseSettingsWidget::~MouseSettingsWidget()
        {}

        std::shared_ptr<MouseSettingsWidget> MouseSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<MouseSettingsWidget>(new MouseSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct ReviewSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;
            std::shared_ptr<ftk::CheckBox> showNotesCheckBox;
            std::shared_ptr<ftk::FloatEditSlider> noteDurationSlider;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::ReviewSettings> > settingsObserver;
        };

        void ReviewSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::ReviewSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.showNotesCheckBox = ftk::CheckBox::create(context);
            p.showNotesCheckBox->setTooltip(
                "Show the text of the markers over the video as playback "
                "reaches them, and while stopped on a frame that has one.");

            p.noteDurationSlider = ftk::FloatEditSlider::create(context);
            p.noteDurationSlider->setRange(.5F, 10.F);
            p.noteDurationSlider->setStep(.5F);
            p.noteDurationSlider->setLargeStep(1.F);
            p.noteDurationSlider->setDefault(models::ReviewSettings().noteDuration);
            p.noteDurationSlider->setTooltip(
                "How long a note stays once playback has passed it, in "
                "seconds, fading out over the end of that time.");

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Show notes over the video:", p.showNotesCheckBox);
            p.layout->addRow("Note duration (seconds):", p.noteDurationSlider);

            p.settingsObserver = ftk::Observer<models::ReviewSettings>::create(
                settings->observeReview(),
                [this](const models::ReviewSettings& value)
                {
                    FTK_P();
                    p.showNotesCheckBox->setChecked(value.showNotes);
                    p.noteDurationSlider->setValue(value.noteDuration);
                    p.noteDurationSlider->setEnabled(value.showNotes);
                });

            p.showNotesCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    auto settings = p.settings->getReview();
                    settings.showNotes = value;
                    p.settings->setReview(settings);
                });

            p.noteDurationSlider->setCallback(
                [this](float value)
                {
                    FTK_P();
                    auto settings = p.settings->getReview();
                    settings.noteDuration = value;
                    p.settings->setReview(settings);
                });
        }

        ReviewSettingsWidget::ReviewSettingsWidget() :
            _p(new Private)
        {}

        ReviewSettingsWidget::~ReviewSettingsWidget()
        {}

        std::shared_ptr<ReviewSettingsWidget> ReviewSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ReviewSettingsWidget>(new ReviewSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct SyncSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;
            std::shared_ptr<models::LanModel> lanModel;

            //! The letters the drive combo offers, "None" first as empty.
            std::vector<std::string> driveLetters;

            std::shared_ptr<ftk::LineEdit> serverEdit;
            std::shared_ptr<ftk::LineEdit> tokenEdit;
            std::shared_ptr<ftk::LineEdit> clientIdEdit;
            std::shared_ptr<ftk::Label> clientIdLabel;
            std::shared_ptr<ftk::ComboBox> driveComboBox;
            std::shared_ptr<ftk::Label> shareLabel;
            std::shared_ptr<ftk::FileEdit> mediaRootEdit;
            std::shared_ptr<ftk::CheckBox> connectOnStartCheckBox;
            std::shared_ptr<ftk::PushButton> testButton;
            std::shared_ptr<ftk::Label> serverStatusLabel;
            std::shared_ptr<ftk::Label> driveStatusLabel;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::SyncSettings> > settingsObserver;
            std::shared_ptr<ftk::Observer<models::SyncServerInfo> > serverInfoObserver;
            std::shared_ptr<ftk::Observer<models::LanDriveStatus> > driveObserver;
        };

        namespace
        {
            //! An address or an identifier as typed, without the spaces a
            //! paste brings along.
            std::string trimmed(const std::string& value)
            {
                const size_t start = value.find_first_not_of(" \t\r\n");
                if (std::string::npos == start)
                {
                    return std::string();
                }
                const size_t end = value.find_last_not_of(" \t\r\n");
                return value.substr(start, end - start + 1);
            }

            //! Wrap a long status line at spaces: a label does not wrap.
            std::string wrapStatus(const std::string& text, size_t width = 48)
            {
                std::string out;
                size_t line = 0;
                for (const auto& word : ftk::split(text, ' '))
                {
                    if (line > 0 && line + 1 + word.size() > width)
                    {
                        out += '\n';
                        line = 0;
                    }
                    else if (line > 0)
                    {
                        out += ' ';
                        ++line;
                    }
                    out += word;
                    line += word.size();
                }
                return out;
            }
        }

        void SyncSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<models::LanModel>& lanModel,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::SyncSettingsWidget", parent);
            FTK_P();

            p.settings = settings;
            p.lanModel = lanModel;

            p.serverEdit = ftk::LineEdit::create(context);
            p.serverEdit->setTooltip(
                "The sync server, e.g. http://10.0.0.5:8787: the address of "
                "the dev server and the port the sync server listens on.");
            ftk::setScreenshotTag(p.serverEdit, "Settings.Sync.Server");

            p.tokenEdit = ftk::LineEdit::create(context);
            p.tokenEdit->setTooltip(
                "The access token, if the sync server asks for one.");

            p.clientIdEdit = ftk::LineEdit::create(context);
            p.clientIdEdit->setTooltip(
                "Who this PC is on the sync server: the admin registers this "
                "identifier on the server's admin page. Unique per PC; letters, "
                "digits, '.', '_' and '-'. The computer name by default.");
            ftk::setScreenshotTag(p.clientIdEdit, "Settings.Sync.ClientId");
            p.clientIdLabel = ftk::Label::create(context);

            p.driveComboBox = ftk::ComboBox::create(context);
            p.driveComboBox->setTooltip(
                "The drive letter the server's media share is mapped to on "
                "this PC. Each PC chooses its own; DJV maps it at start.");
            ftk::setScreenshotTag(p.driveComboBox, "Settings.Sync.Drive");

            p.shareLabel = ftk::Label::create(context);
            p.shareLabel->setTooltip(
                "The media share the server last named. It is set on the "
                "server's admin page.");

            p.mediaRootEdit = ftk::FileEdit::create(context, ftk::FileBrowserMode::Dir);
            p.mediaRootEdit->setTooltip(
                "Where the media is reached when no drive is mapped: on a PC "
                "that is not Windows (e.g. /Volumes/media), or one that "
                "reaches the share some other way.\n"
                "A sync session names its media relative to this folder, or "
                "to the mapped drive, so that machines reaching the storage "
                "under different names still find the same file.");

            p.connectOnStartCheckBox = ftk::CheckBox::create(
                context,
                "Contact the server and map the drive at start");

            p.testButton = ftk::PushButton::create(context, "Check");
            p.testButton->setTooltip(
                "Ask the server whether this PC is registered and where the "
                "media share is, then map the drive.");
            ftk::setScreenshotTag(p.testButton, "Settings.Sync.Check");
            p.serverStatusLabel = ftk::Label::create(context);
            p.driveStatusLabel = ftk::Label::create(context);

            p.layout = ftk::FormLayout::create(context);
            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Server:", p.serverEdit);
            p.layout->addRow("Token:", p.tokenEdit);
            p.layout->addRow("Client ID:", p.clientIdEdit);
            p.layout->addRow("", p.clientIdLabel);
            p.layout->addRow("Media drive:", p.driveComboBox);
            p.layout->addRow("Media share:", p.shareLabel);
            p.layout->addRow("Media folder:", p.mediaRootEdit);
            p.layout->addRow("", p.connectOnStartCheckBox);
            p.layout->addRow("", p.testButton);
            p.layout->addRow("Server:", p.serverStatusLabel);
            p.layout->addRow("Drive:", p.driveStatusLabel);

            _driveLettersUpdate();

            p.settingsObserver = ftk::Observer<models::SyncSettings>::create(
                settings->observeSync(),
                [this](const models::SyncSettings& value)
                {
                    FTK_P();
                    p.serverEdit->setText(value.server);
                    p.tokenEdit->setText(value.token);
                    p.clientIdEdit->setText(value.clientId);
                    p.clientIdLabel->setText(models::isValidSyncClientId(value.clientId) ?
                        std::string() :
                        std::string("Letters, digits, '.', '_' and '-' only, up to 64."));
                    _driveLettersUpdate();
                    p.shareLabel->setText(value.shareUnc.empty() ?
                        std::string("Not known yet: click Check.") :
                        value.shareUnc);
                    p.mediaRootEdit->setPath(ftk::Path(value.mediaRoot));
                    p.connectOnStartCheckBox->setChecked(value.connectOnStart);
                });

            p.serverInfoObserver = ftk::Observer<models::SyncServerInfo>::create(
                lanModel->observeServerInfo(),
                [this](const models::SyncServerInfo& value)
                {
                    FTK_P();
                    std::string text;
                    switch (value.state)
                    {
                    case models::SyncServerState::Unknown:
                        text = "Not checked yet.";
                        break;
                    case models::SyncServerState::Checking:
                        text = "Checking...";
                        break;
                    case models::SyncServerState::Ok:
                        text = ftk::Format("{0}{1}").
                            arg(value.name.empty() ? std::string("Reached") : value.name).
                            arg(value.requireRegistration ?
                                std::string(ftk::Format(": registered as {0}.").
                                    arg(value.clientName.empty() ? value.clientId : value.clientName)) :
                                std::string("."));
                        break;
                    case models::SyncServerState::Unregistered:
                    case models::SyncServerState::Unreachable:
                    case models::SyncServerState::Error:
                        break;
                    }
                    if (!value.message.empty())
                    {
                        text = text.empty() ? value.message : text + " " + value.message;
                    }
                    p.serverStatusLabel->setText(wrapStatus(text));
                    p.testButton->setEnabled(value.state != models::SyncServerState::Checking);
                });

            p.driveObserver = ftk::Observer<models::LanDriveStatus>::create(
                lanModel->observeDrive(),
                [this](const models::LanDriveStatus& value)
                {
                    FTK_P();
                    std::string text;
                    switch (value.state)
                    {
                    case models::LanDriveState::None:
                        text = "No drive letter is set.";
                        break;
                    case models::LanDriveState::Unsupported:
                        text = "Drive letters are a Windows feature; set the media folder instead.";
                        break;
                    default:
                        text = value.message;
                        break;
                    }
                    p.driveStatusLabel->setText(wrapStatus(text));
                    // The folder is only what is used when no drive is.
                    p.mediaRootEdit->setEnabled(value.state != models::LanDriveState::Mapped);
                });

            p.serverEdit->setCallback(
                [this](const std::string& value)
                {
                    FTK_P();
                    auto settings = p.settings->getSync();
                    settings.server = trimmed(value);
                    p.settings->setSync(settings);
                });

            p.tokenEdit->setCallback(
                [this](const std::string& value)
                {
                    FTK_P();
                    auto settings = p.settings->getSync();
                    settings.token = value;
                    p.settings->setSync(settings);
                });

            p.clientIdEdit->setCallback(
                [this](const std::string& value)
                {
                    FTK_P();
                    auto settings = p.settings->getSync();
                    settings.clientId = trimmed(value);
                    p.settings->setSync(settings);
                });

            p.driveComboBox->setIndexCallback(
                [this](int index)
                {
                    FTK_P();
                    if (index >= 0 && index < static_cast<int>(p.driveLetters.size()))
                    {
                        auto settings = p.settings->getSync();
                        settings.driveLetter = p.driveLetters[index];
                        p.settings->setSync(settings);
                    }
                });

            p.mediaRootEdit->setCallback(
                [this](const ftk::Path& value)
                {
                    FTK_P();
                    auto settings = p.settings->getSync();
                    settings.mediaRoot = value.get();
                    p.settings->setSync(settings);
                });

            p.connectOnStartCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    auto settings = p.settings->getSync();
                    settings.connectOnStart = value;
                    p.settings->setSync(settings);
                });

            p.testButton->setClickedCallback(
                [this]
                {
                    FTK_P();
                    _driveLettersUpdate();
                    p.lanModel->start();
                });
        }

        SyncSettingsWidget::SyncSettingsWidget() :
            _p(new Private)
        {}

        SyncSettingsWidget::~SyncSettingsWidget()
        {}

        std::shared_ptr<SyncSettingsWidget> SyncSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<models::LanModel>& lanModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<SyncSettingsWidget>(new SyncSettingsWidget);
            out->_init(context, settings, lanModel, parent);
            return out;
        }

        void SyncSettingsWidget::_driveLettersUpdate()
        {
            FTK_P();
            // The free letters, and the one chosen even when it is not free:
            // it is in use because DJV mapped it.
            const std::string current = models::normalizeDriveLetter(p.settings->getSync().driveLetter);
            std::vector<std::string> letters = models::getFreeDriveLetters();
            if (!current.empty() && std::find(letters.begin(), letters.end(), current) == letters.end())
            {
                letters.push_back(current);
                std::sort(letters.begin(), letters.end());
            }
            letters.insert(letters.begin(), std::string());
            std::vector<std::string> labels;
            int index = 0;
            for (size_t i = 0; i < letters.size(); ++i)
            {
                labels.push_back(letters[i].empty() ? std::string("None") : letters[i] + ":");
                if (letters[i] == current)
                {
                    index = static_cast<int>(i);
                }
            }
            if (letters != p.driveLetters)
            {
                p.driveLetters = letters;
                p.driveComboBox->setItems(labels);
            }
            p.driveComboBox->setCurrentIndex(index);
            p.driveComboBox->setEnabled(models::isNetworkDriveSupported());
        }

        struct PlaybackSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;
            std::shared_ptr<ftk::CheckBox> startPlaybackCheckBox;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<models::PlaybackSettings> > settingsObserver;
        };

        void PlaybackSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::PlaybackSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.startPlaybackCheckBox = ftk::CheckBox::create(context);

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Start playback on open:", p.startPlaybackCheckBox);

            p.settingsObserver = ftk::Observer<models::PlaybackSettings>::create(
                settings->observePlayback(),
                [this](const models::PlaybackSettings& value)
                {
                    FTK_P();
                    p.startPlaybackCheckBox->setChecked(value.startPlayback);
                });

            p.startPlaybackCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    auto settings = p.settings->getPlayback();
                    settings.startPlayback = value;
                    p.settings->setPlayback(settings);
                });
        }

        PlaybackSettingsWidget::PlaybackSettingsWidget() :
            _p(new Private)
        {}

        PlaybackSettingsWidget::~PlaybackSettingsWidget()
        {}

        std::shared_ptr<PlaybackSettingsWidget> PlaybackSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<PlaybackSettingsWidget>(new PlaybackSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }

        struct TimeSettingsWidget::Private
        {
            std::shared_ptr<models::TimeUnitsModel> timeUnitsModel;

            std::shared_ptr<ftk::ComboBox> timeUnitsComboBox;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<tl::TimeUnits> > timeUnitsObserver;
        };

        void TimeSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::TimeUnitsModel>& timeUnitsModel,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::TimeSettingsWidget", parent);
            FTK_P();

            p.timeUnitsModel = timeUnitsModel;

            p.timeUnitsComboBox = ftk::ComboBox::create(context, tl::getTimeUnitsLabels());

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Time units:", p.timeUnitsComboBox);

            p.timeUnitsComboBox->setIndexCallback(
                [this](int value)
                {
                    _p->timeUnitsModel->setTimeUnits(static_cast<tl::TimeUnits>(value));
                });

            p.timeUnitsObserver = ftk::Observer<tl::TimeUnits>::create(
                p.timeUnitsModel->observeTimeUnits(),
                [this](tl::TimeUnits value)
                {
                    _p->timeUnitsComboBox->setCurrentIndex(static_cast<int>(value));
                });

        }

        TimeSettingsWidget::TimeSettingsWidget() :
            _p(new Private)
        {}

        TimeSettingsWidget::~TimeSettingsWidget()
        {}

        std::shared_ptr<TimeSettingsWidget> TimeSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::TimeUnitsModel>& timeUnitsModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<TimeSettingsWidget>(new TimeSettingsWidget);
            out->_init(context, timeUnitsModel, parent);
            return out;
        }

#if defined(TLRENDER_FFMPEG_PLUGIN)
        struct FFmpegSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::CheckBox> yuvToRGBCheckBox;
            std::shared_ptr<ftk::CheckBox> hwAccelCheckBox;
            std::shared_ptr<ftk::CheckBox> audioMergeCheckBox;
            std::shared_ptr<ftk::IntEdit> threadsEdit;
            std::shared_ptr<ftk::FileEdit> ffmpegEdit;
            std::shared_ptr<ftk::Label> ffmpegFoundLabel;
            std::shared_ptr<ftk::FileEdit> ffprobeEdit;
            std::shared_ptr<ftk::Label> ffprobeFoundLabel;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<tl::ffmpeg::Options> > optionsObserver;
            std::shared_ptr<ftk::Observer<tl::ffmpeg_cmd::Options> > cmdOptionsObserver;
        };

        void FFmpegSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::FFmpegSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.yuvToRGBCheckBox = ftk::CheckBox::create(context);
            p.yuvToRGBCheckBox->setHStretch(ftk::Stretch::Expanding);
            p.yuvToRGBCheckBox->setTooltip(
                "Convert YUV to RGB on the CPU when reading.\n"
                "\n"
                "When disabled, YUV frames are kept and converted on the GPU.");
            ftk::setScreenshotTag(p.yuvToRGBCheckBox, "FFmpeg.YUVtoRGB");

            p.hwAccelCheckBox = ftk::CheckBox::create(context);
            p.hwAccelCheckBox->setHStretch(ftk::Stretch::Expanding);
            // The minimal FFmpeg has no hardware decoders -- enabling one
            // would pull in the software decoder it depends on -- so the
            // option goes inert rather than promising what the build
            // cannot do.
            if (tl::ffmpeg::hasHWDecode())
            {
                p.hwAccelCheckBox->setTooltip(
                    "Use the GPU to decode video when possible.\n"
                    "\n"
                    "Falls back to software decoding automatically when hardware\n"
                    "decoding is unavailable for a file.\n"
                    "\n"
                    "Takes effect the next time a file is opened.");
            }
            else
            {
                p.hwAccelCheckBox->setEnabled(false);
                p.hwAccelCheckBox->setTooltip(
                    "Hardware decoding is not available.");
            }
            ftk::setScreenshotTag(p.hwAccelCheckBox, "FFmpeg.HardwareDecoding");

            p.audioMergeCheckBox = ftk::CheckBox::create(context);
            p.audioMergeCheckBox->setHStretch(ftk::Stretch::Expanding);
            p.audioMergeCheckBox->setTooltip(
                "Play a file's mono audio streams as the channels of one track.\n"
                "Broadcast files, such as XDCAM MXF, carry each channel as its\n"
                "own stream.\n"
                "\n"
                "When disabled, only the first stream plays.\n"
                "\n"
                "Takes effect the next time a file is opened.");
            ftk::setScreenshotTag(p.audioMergeCheckBox, "FFmpeg.AudioMerge");

            p.threadsEdit = ftk::IntEdit::create(context);
            p.threadsEdit->setRange(0, 64);
            p.threadsEdit->setTooltip(
                "Number of threads used for decoding. Set to 0 to choose the thread "
                "count automatically.");
            ftk::setScreenshotTag(p.threadsEdit, "FFmpeg.Threads");

            p.ffmpegEdit = ftk::FileEdit::create(context);
            p.ffmpegEdit->setTooltip(
                "Location of the ffmpeg command, used for files the built-in "
                "FFmpeg cannot decode and for exporting movies.");
            ftk::setScreenshotTag(p.ffmpegEdit, "FFmpeg.FFmpegPath");

            p.ffprobeEdit = ftk::FileEdit::create(context);
            p.ffprobeEdit->setTooltip(
                "Location of the ffprobe command.");
            ftk::setScreenshotTag(p.ffprobeEdit, "FFmpeg.FFprobePath");

            // Whether the command above is there at all, and which one it
            // is where more than one is installed. A name on its own is a
            // name to look for, which reads as an installation that was
            // found (DJV #893).
            const auto foundLabel = [&context]
            {
                auto out = ftk::Label::create(context);
                out->setElide(true, ftk::ElideMode::Middle);
                out->setHStretch(ftk::Stretch::Expanding);
                return out;
            };
            p.ffmpegFoundLabel = foundLabel();
            p.ffprobeFoundLabel = foundLabel();

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("YUV to RGB conversion:", p.yuvToRGBCheckBox);
            p.layout->addRow("Hardware decoding:", p.hwAccelCheckBox);
            p.layout->addRow("Merge mono audio:", p.audioMergeCheckBox);
            p.layout->addRow("I/O threads:", p.threadsEdit);
            p.layout->addRow("ffmpeg location:", p.ffmpegEdit);
            p.layout->addRow("", p.ffmpegFoundLabel);
            p.layout->addRow("ffprobe location:", p.ffprobeEdit);
            p.layout->addRow("", p.ffprobeFoundLabel);

            p.optionsObserver = ftk::Observer<tl::ffmpeg::Options>::create(
                settings->observeFFmpeg(),
                [this](const tl::ffmpeg::Options& value)
                {
                    FTK_P();
                    p.yuvToRGBCheckBox->setChecked(value.yuvToRgb);
                    p.hwAccelCheckBox->setChecked(
                        value.hwAccel && tl::ffmpeg::hasHWDecode());
                    p.audioMergeCheckBox->setChecked(value.audioMerge);
                    p.threadsEdit->setValue(value.threadCount);
                });

            p.cmdOptionsObserver = ftk::Observer<tl::ffmpeg_cmd::Options>::create(
                settings->observeFFmpegCmd(),
                [this](const tl::ffmpeg_cmd::Options& value)
                {
                    FTK_P();
                    p.ffmpegEdit->setPath(ftk::Path(value.ffmpegPath));
                    p.ffprobeEdit->setPath(ftk::Path(value.ffprobePath));
                    _foundUpdate();
                });

            p.yuvToRGBCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    tl::ffmpeg::Options options = p.settings->getFFmpeg();
                    options.yuvToRgb = value;
                    p.settings->setFFmpeg(options);
                });

            p.hwAccelCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    tl::ffmpeg::Options options = p.settings->getFFmpeg();
                    options.hwAccel = value;
                    p.settings->setFFmpeg(options);
                });

            p.audioMergeCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    tl::ffmpeg::Options options = p.settings->getFFmpeg();
                    options.audioMerge = value;
                    p.settings->setFFmpeg(options);
                });

            p.threadsEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    tl::ffmpeg::Options options = p.settings->getFFmpeg();
                    options.threadCount = value;
                    p.settings->setFFmpeg(options);
                });

            p.ffmpegEdit->setCallback(
                [this](const ftk::Path& value)
                {
                    FTK_P();
                    tl::ffmpeg_cmd::Options options = p.settings->getFFmpegCmd();
                    options.ffmpegPath = value.get();
                    p.settings->setFFmpegCmd(options);
                });

            p.ffprobeEdit->setCallback(
                [this](const ftk::Path& value)
                {
                    FTK_P();
                    tl::ffmpeg_cmd::Options options = p.settings->getFFmpegCmd();
                    options.ffprobePath = value.get();
                    p.settings->setFFmpegCmd(options);
                });
        }

        FFmpegSettingsWidget::FFmpegSettingsWidget() :
            _p(new Private)
        {}

        void FFmpegSettingsWidget::_foundUpdate()
        {
            FTK_P();
            const auto update =
                [](const std::shared_ptr<ftk::Label>& label,
                    const std::string& command,
                    const std::string& name)
            {
                const std::string found =
                    tl::ffmpeg_cmd::findCommand(command);
                label->setText(found.empty() ?
                    ftk::Format("No {0} found.").arg(name).str() :
                    found);
                label->setTooltip(found.empty() ?
                    ftk::Format(
                        "DJV did not find \"{0}\". A name on its own is "
                        "looked for on the system path; give the location "
                        "of the command to use one that is not there.").
                        arg(command).str() :
                    ftk::Format("Where DJV found \"{0}\".").
                        arg(command).str());
                label->setTextRole(found.empty() ?
                    ftk::ColorRole::Red :
                    ftk::ColorRole::TextDisabled);
            };
            const auto options = p.settings->getFFmpegCmd();
            update(p.ffmpegFoundLabel, options.ffmpegPath, "ffmpeg");
            update(p.ffprobeFoundLabel, options.ffprobePath, "ffprobe");
        }

        FFmpegSettingsWidget::~FFmpegSettingsWidget()
        {}

        std::shared_ptr<FFmpegSettingsWidget> FFmpegSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<FFmpegSettingsWidget>(new FFmpegSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }
#endif // TLRENDER_FFMPEG_PLUGIN

#if defined(TLRENDER_USD)
        struct USDSettingsWidget::Private
        {
            std::shared_ptr<models::SettingsModel> settings;

            std::shared_ptr<ftk::IntEdit> renderWidthEdit;
            std::shared_ptr<ftk::FloatEditSlider> complexitySlider;
            std::shared_ptr<ftk::ComboBox> drawModeComboBox;
            std::shared_ptr<ftk::CheckBox> lightingCheckBox;
            std::shared_ptr<ftk::CheckBox> sRGBCheckBox;
            std::shared_ptr<ftk::IntEdit> stageCacheEdit;
            std::shared_ptr<ftk::IntEdit> diskCacheEdit;
            std::shared_ptr<ftk::FormLayout> layout;

            std::shared_ptr<ftk::Observer<tl::usd::Options> > optionsObserver;
        };

        void USDSettingsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            ISettingsWidget::_init(context, "djv::ui::USDSettingsWidget", parent);
            FTK_P();

            p.settings = settings;

            p.renderWidthEdit = ftk::IntEdit::create(context);
            p.renderWidthEdit->setRange(1, 8192);

            p.complexitySlider = ftk::FloatEditSlider::create(context);

            p.drawModeComboBox = ftk::ComboBox::create(context, tl::usd::getDrawModeLabels());
            p.drawModeComboBox->setHStretch(ftk::Stretch::Expanding);

            p.lightingCheckBox = ftk::CheckBox::create(context);
            p.lightingCheckBox->setHStretch(ftk::Stretch::Expanding);

            p.sRGBCheckBox = ftk::CheckBox::create(context);
            p.sRGBCheckBox->setHStretch(ftk::Stretch::Expanding);

            p.stageCacheEdit = ftk::IntEdit::create(context);
            p.stageCacheEdit->setRange(0, 10);

            p.diskCacheEdit = ftk::IntEdit::create(context);
            p.diskCacheEdit->setRange(0, 1024);

            p.layout = ftk::FormLayout::create(context);

            _setWidget(p.layout);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.layout->addRow("Render width:", p.renderWidthEdit);
            p.layout->addRow("Render complexity:", p.complexitySlider);
            p.layout->addRow("Draw mode:", p.drawModeComboBox);
            p.layout->addRow("Enable lighting:", p.lightingCheckBox);
            p.layout->addRow("Enable sRGB color space:", p.sRGBCheckBox);
            p.layout->addRow("Stage cache size:", p.stageCacheEdit);
            auto hLayout = ftk::HorizontalLayout::create(context);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.diskCacheEdit->setParent(hLayout);
            ftk::Label::create(context, "GB", hLayout);
            p.layout->addRow("Disk cache size:", hLayout);

            p.optionsObserver = ftk::Observer<tl::usd::Options>::create(
                settings->observeUSD(),
                [this](const tl::usd::Options& value)
                {
                    FTK_P();
                    p.renderWidthEdit->setValue(value.renderWidth);
                    p.complexitySlider->setValue(value.complexity);
                    p.drawModeComboBox->setCurrentIndex(static_cast<int>(value.drawMode));
                    p.lightingCheckBox->setChecked(value.enableLighting);
                    p.sRGBCheckBox->setChecked(value.sRGB);
                    p.stageCacheEdit->setValue(value.stageCacheCount);
                    p.diskCacheEdit->setValue(value.diskCacheGB);
                });

            p.renderWidthEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.renderWidth = value;
                    p.settings->setUSD(options);
                });

            p.complexitySlider->setCallback(
                [this](float value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.complexity = value;
                    p.settings->setUSD(options);
                });

            p.drawModeComboBox->setIndexCallback(
                [this](int value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.drawMode = static_cast<tl::usd::DrawMode>(value);
                    p.settings->setUSD(options);
                });

            p.lightingCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.enableLighting = value;
                    p.settings->setUSD(options);
                });

            p.sRGBCheckBox->setCheckedCallback(
                [this](bool value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.sRGB = value;
                    p.settings->setUSD(options);
                });

            p.stageCacheEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.stageCacheCount = value;
                    p.settings->setUSD(options);
                });

            p.diskCacheEdit->setCallback(
                [this](int value)
                {
                    FTK_P();
                    tl::usd::Options options = p.settings->getUSD();
                    options.diskCacheGB = value;
                    p.settings->setUSD(options);
                });
        }

        USDSettingsWidget::USDSettingsWidget() :
            _p(new Private)
        {}

        USDSettingsWidget::~USDSettingsWidget()
        {}

        std::shared_ptr<USDSettingsWidget> USDSettingsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::SettingsModel>& settings,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<USDSettingsWidget>(new USDSettingsWidget);
            out->_init(context, settings, parent);
            return out;
        }
#endif // TLRENDER_USD
    }
}
