#pragma once

#include <QObject>
#include <QString>
#include <mpv/client.h>

// Small glue layer between LAMBDA Player and mpv's built-in `vapoursynth` video
// filter. It locates the bundled VapourSynth/RIFE runtime, adds or removes the
// labelled `@novarife` filter and reports activation failures. It contains no
// interpolation logic of its own; the actual work is done by mpv, VapourSynth
// and the VapourSynth-RIFE-ncnn-Vulkan plugin.
class InterpolationController final : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Off,
        RifeDouble, // 2x the source frame rate
        Rife60      // 60 fps output (sources below 60 fps only)
    };

    // Source frame rate snapped to the nearest standard rate (23.976, 24, 25,
    // 29.97, 30, 48, 50, 59.94, 60) when within 0.5%; 0 when unknown. Mirrors
    // the logic in rife.vpy so the UI shows the rates RIFE will produce.
    static double normalizedFps(double containerFps);
    // True when the 60 fps mode makes sense for this source frame rate.
    static bool supports60(double normalizedFps);

    explicit InterpolationController(mpv_handle *mpv, QObject *parent = nullptr);

    Mode mode() const { return mode_; }

    // Switches the interpolation mode. Returns false and fills `error` when the
    // requested mode could not be activated; the mode then stays/reverts to Off.
    bool setMode(Mode mode, QString *error = nullptr);

    // Feed mpv log messages (MPV_EVENT_LOG_MESSAGE) so asynchronous filter
    // failures (script errors, missing GPU, ...) can be detected.
    void handleLogMessage(const QString &prefix, const QString &level, const QString &text);

    // Points VSSCRIPT_PATH at the bundled VapourSynth. Must be called before
    // mpv_create(): mpv caches the process environment on first use.
    static void configureProcessEnvironment();

    // Directory that holds rife.vpy, the plugin DLLs and the models.
    static QString runtimeDirectory();
    // Directory that holds the embedded Python + VapourSynth runtime.
    static QString vapourSynthDirectory();

signals:
    // Emitted when RIFE had to be turned off after it was enabled, e.g. because
    // the VapourSynth script failed once video started flowing through it.
    void deactivated(const QString &reason);

private:
    bool checkRuntimeFiles(QString *error) const;
    bool preloadVsScript(QString *error) const;
    static QString vsScriptPath();
    bool addFilter(Mode mode, QString *error);
    void removeFilter();
    static QString quoted(const QString &value);

    mpv_handle *mpv_ = nullptr;
    Mode mode_ = Mode::Off;
    QString lastFilterError_;
    bool detailedError_ = false;
};
