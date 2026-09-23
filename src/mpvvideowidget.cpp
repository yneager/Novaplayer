#include "mpvvideowidget.h"

#include <QMetaObject>
#include <QOpenGLContext>

#include <mpv/render_gl.h>

MpvVideoWidget::MpvVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setObjectName("videoSurface");
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Keep receiving frames while partially covered by the web chrome.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        if (renderContext_) {
            mpv_render_context_report_swap(renderContext_);
        }
    });
}

MpvVideoWidget::~MpvVideoWidget()
{
    shutdown();
}

void MpvVideoWidget::setMpv(mpv_handle *mpv)
{
    mpv_ = mpv;
}

void *MpvVideoWidget::getProcAddress(void *, const char *name)
{
    QOpenGLContext *glContext = QOpenGLContext::currentContext();
    if (!glContext) {
        return nullptr;
    }
    return reinterpret_cast<void *>(glContext->getProcAddress(QByteArray(name)));
}

void MpvVideoWidget::onMpvUpdate(void *ctx)
{
    // Called from an mpv thread: hop to the GUI thread before touching Qt.
    QMetaObject::invokeMethod(static_cast<MpvVideoWidget *>(ctx), "requestFrame",
                              Qt::QueuedConnection);
}

void MpvVideoWidget::requestFrame()
{
    if (renderContext_) {
        update();
    }
}

void MpvVideoWidget::initializeGL()
{
    if (renderContext_ || !mpv_) {
        if (!mpv_) {
            emit renderFailed("libmpv is not initialised.");
        }
        return;
    }

    mpv_opengl_init_params glInit{};
    glInit.get_proc_address = &MpvVideoWidget::getProcAddress;
    glInit.get_proc_address_ctx = nullptr;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    const int rc = mpv_render_context_create(&renderContext_, mpv_, params);
    if (rc < 0) {
        renderContext_ = nullptr;
        emit renderFailed(QString("mpv could not create its OpenGL renderer: %1")
                              .arg(QString::fromUtf8(mpv_error_string(rc))));
        return;
    }

    mpv_render_context_set_update_callback(renderContext_, &MpvVideoWidget::onMpvUpdate, this);

    // The GL context can be recreated (e.g. when the widget changes top-level);
    // the mpv render context must never outlive the context it was made for.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
            &MpvVideoWidget::releaseRenderContext, Qt::DirectConnection);

    emit renderReady();
}

void MpvVideoWidget::paintGL()
{
    if (!renderContext_) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{};
    fbo.fbo = static_cast<int>(defaultFramebufferObject());
    fbo.w = qRound(width() * dpr);
    fbo.h = qRound(height() * dpr);
    fbo.internal_format = 0;

    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(renderContext_, params);
}

void MpvVideoWidget::releaseRenderContext()
{
    if (!renderContext_) {
        return;
    }
    mpv_render_context_set_update_callback(renderContext_, nullptr, nullptr);
    mpv_render_context_free(renderContext_);
    renderContext_ = nullptr;
}

void MpvVideoWidget::shutdown()
{
    if (!renderContext_) {
        return;
    }
    // mpv_render_context_free() may call GL functions: the context must be current.
    const bool hasContext = context() != nullptr;
    if (hasContext) {
        makeCurrent();
    }
    releaseRenderContext();
    if (hasContext) {
        doneCurrent();
    }
}
