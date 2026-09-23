#include "interpolationcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstdlib>
#include <stdlib.h>

namespace {

constexpr const char *kFilterLabel = "novarife";
constexpr const char *kScriptFile = "rife.vpy";
constexpr const char *kRifePlugin = "librife_windows_x86-64.dll";
constexpr const char *kMiscPlugin = "MiscFilters.dll";
constexpr const char *kModelDir = "models/rife-v4.6_ensembleFalse";
constexpr const char *kVsScriptRelative = "Lib/site-packages/vapoursynth/vsscript.dll";

QString firstLine(const QString &text)
{
    const QString trimmed = text.trimmed();
    const int newline = trimmed.indexOf('\n');
    return newline < 0 ? trimmed : trimmed.left(newline).trimmed();
}

} // namespace

InterpolationController::InterpolationController(mpv_handle *mpv, QObject *parent)
    : QObject(parent)
    , mpv_(mpv)
{
}

QString InterpolationController::runtimeDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("rife");
}

QString InterpolationController::vapourSynthDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("vapoursynth");
}

bool InterpolationController::checkRuntimeFiles(QString *error) const
{
    const QDir rife(runtimeDirectory());
    const QDir vs(vapourSynthDirectory());

    const QStringList required = {
        rife.filePath(kScriptFile),
        rife.filePath(kRifePlugin),
        rife.filePath(kMiscPlugin),
        rife.filePath(QString(kModelDir) + "/flownet.bin"),
        rife.filePath(QString(kModelDir) + "/flownet.param"),
        vs.filePath("python.exe"),
        vs.filePath("python3.dll"),
        vs.filePath(kVsScriptRelative),
    };

    QStringList missing;
    for (const QString &path : required) {
        if (!QFileInfo::exists(path)) {
            missing << QDir::toNativeSeparators(path);
        }
    }

    if (!missing.isEmpty()) {
        if (error) {
            *error = QString("The bundled RIFE/VapourSynth runtime is incomplete. Missing:\n%1")
                         .arg(missing.join('\n'));
        }
        return false;
    }
    return true;
}

void InterpolationController::prepareEnvironment() const
{
#ifdef _WIN32
    // mpv's vapoursynth filter loads VSScript from $VSSCRIPT_PATH first
    // (otherwise it searches for "VSScript.dll" on the DLL search path).
    // VapourSynth R80's vsscript.dll then finds the embedded Python by itself:
    // python.exe + python3.dll three directories above vsscript.dll
    // ("implicit config"). Verified locally with the pinned runtime; no PATH
    // changes are needed.
    const QString vsScript = QDir::toNativeSeparators(QDir(vapourSynthDirectory()).filePath(kVsScriptRelative));

    // libmpv-2.dll and NovaPlayer (/MD) share the Universal CRT, so
    // _wputenv_s updates the environment table libmpv's getenv() reads, and
    // the process environment block.
    _wputenv_s(L"VSSCRIPT_PATH", reinterpret_cast<const wchar_t *>(vsScript.utf16()));
#endif
}

QString InterpolationController::quoted(const QString &value)
{
    // mpv option syntax: %<byte-length>%<string> quotes arbitrary characters
    // (Windows paths contain ':' and '\' which would otherwise be parsed).
    // (QString::arg() is not used here: it has no "%%" escape and would also
    // substitute any "%1" that happens to be part of the path.)
    const qsizetype bytes = value.toUtf8().size();
    return QStringLiteral("%") + QString::number(bytes) + QStringLiteral("%") + value;
}

bool InterpolationController::addFilter(QString *error)
{
    const QDir rife(runtimeDirectory());
    const QString script = QDir::toNativeSeparators(rife.filePath(kScriptFile));
    const QString runtime = QDir::toNativeSeparators(rife.absolutePath());

    const QString spec = QStringLiteral("@") + QLatin1String(kFilterLabel)
                         + QStringLiteral(":vapoursynth=file=") + quoted(script)
                         + QStringLiteral(":user-data=") + quoted(runtime);

    const QByteArray specUtf8 = spec.toUtf8();
    const char *args[] = {"vf", "add", specUtf8.constData(), nullptr};

    lastFilterError_.clear();
    detailedError_ = false;
    const int rc = mpv_command(mpv_, args);
    if (rc < 0) {
        if (error) {
            // mpv logs the precise reason (e.g. "Failed to load VapourSynth
            // VSScript library") asynchronously; it may not have arrived yet.
            QString detail = lastFilterError_;
            if (detail.isEmpty()) {
                detail = QString::fromUtf8(mpv_error_string(rc))
                         + ". The VapourSynth runtime (vapoursynth\\ folder) could not be loaded.";
            }
            *error = QString("mpv could not create the VapourSynth filter: %1").arg(detail);
        }
        return false;
    }
    return true;
}

void InterpolationController::removeFilter()
{
    // Removes only the labelled filter; unrelated video filters stay untouched.
    const QByteArray label = QByteArray("@") + kFilterLabel;
    const char *args[] = {"vf", "remove", label.constData(), nullptr};
    mpv_command(mpv_, args);
}

bool InterpolationController::setMode(Mode mode, QString *error)
{
    if (!mpv_) {
        if (error) {
            *error = "libmpv is not initialised.";
        }
        return false;
    }

    if (mode == mode_) {
        return true;
    }

    if (mode == Mode::Off) {
        removeFilter();
        mode_ = Mode::Off;
        return true;
    }

    if (!checkRuntimeFiles(error)) {
        return false;
    }

    prepareEnvironment();

    if (!addFilter(error)) {
        // mpv does not add the filter when creation fails, but make sure no
        // half-configured entry is left behind.
        removeFilter();
        mode_ = Mode::Off;
        return false;
    }

    mode_ = Mode::Rife2x;
    return true;
}

void InterpolationController::handleLogMessage(const QString &prefix, const QString &level, const QString &text)
{
    if (level != "error" && level != "fatal") {
        return;
    }

    const QString line = firstLine(text);
    if (line.isEmpty()) {
        return;
    }

    // Errors raised while creating or running the VapourSynth filter/script.
    // A failing script is logged as "Script evaluation failed:" followed by
    // "Python exception: <message>" and a traceback; keep the most useful line.
    if (prefix.contains("vapoursynth")) {
        static const QString kPythonException = "Python exception:";
        if (line.startsWith(kPythonException)) {
            lastFilterError_ = line.mid(kPythonException.size()).trimmed();
            detailedError_ = true;
        } else if (!detailedError_ && lastFilterError_.isEmpty()
                   && line != "Script evaluation failed:" && line != "could not init VS"
                   && !line.startsWith("Traceback") && !line.startsWith("File \"")) {
            lastFilterError_ = line;
        }
    } else if (prefix.contains("user_filter_wrapper") && lastFilterError_.isEmpty()) {
        lastFilterError_ = line;
    }

    // mpv disables a failed user filter and continues with pass-through video
    // ("Disabling filter <label> because it has failed."). Clean up our entry
    // and report it so the UI can fall back to Off.
    if (mode_ == Mode::Rife2x && line.contains("Disabling filter") && line.contains(kFilterLabel)) {
        removeFilter();
        mode_ = Mode::Off;
        QString reason = lastFilterError_.isEmpty()
                             ? QString("The RIFE VapourSynth filter failed during playback.")
                             : lastFilterError_;
        lastFilterError_.clear();
        detailedError_ = false;
        emit deactivated(reason);
    }
}
