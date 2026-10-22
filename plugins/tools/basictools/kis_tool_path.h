/*
 *  SPDX-FileCopyrightText: 2007 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOOL_PATH_H_
#define KIS_TOOL_PATH_H_

#include <KoCreatePathTool.h>
#include <KoToolFactoryBase.h>

#include "flake/kis_node_shape.h"
#include "kis_tool_shape.h"
#include "kis_delegated_tool.h"
#include <kis_icon.h>

class KoCanvasBase;
class KisToolPath;


class __KisToolPathLocalTool : public KoCreatePathTool {
public:
    __KisToolPathLocalTool(KoCanvasBase * canvas, KisToolPath* parentTool);

    void paintPath(KoPathShape &path, QPainter &painter, const KoViewConverter &converter) override;
    void addPathShape(KoPathShape* pathShape) override;
    void paint(QPainter &painter, const KoViewConverter &converter) override;
    QRectF decorationsRect() const override;

    /// IKriate: on-canvas buttons next to the last point while drawing.
    enum Button { NoButton, CloseButton, FinishButton };
    Button buttonAt(const QPointF &viewPoint) const;

    using KoCreatePathTool::createOptionWidgets;
    using KoCreatePathTool::endPathWithoutLastPoint;
    using KoCreatePathTool::endPath;
    using KoCreatePathTool::cancelPath;
    using KoCreatePathTool::removeLastPoint;
    using KoCreatePathTool::closeAndEndPath;

private:
    KisToolPath* const m_parentTool;
    QPointF m_closeCentre;
    QPointF m_finishCentre;
    bool m_buttonsShown {false};
};

typedef KisDelegatedTool<KisToolShape,
                         __KisToolPathLocalTool,
                         DeselectShapesActivationPolicy> DelegatedPathTool;

class KisToolPath : public DelegatedPathTool
{
    Q_OBJECT

public:
    KisToolPath(KoCanvasBase * canvas);
    void mousePressEvent(KoPointerEvent *event) override;

    QList< QPointer<QWidget> > createOptionWidgets() override;

    bool eventFilter(QObject *obj, QEvent *event) override;

    void beginPrimaryAction(KoPointerEvent* event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;

    void beginAlternateAction(KoPointerEvent *event, AlternateAction action) override;

    // reimplementing KisTool's method because that method calls beginPrimaryAction
    // which now is used to start the path tool.
    void beginPrimaryDoubleClickAction(KoPointerEvent* event) override;

    KisPopupWidgetInterface* popupWidget() override;

protected:
    void requestStrokeCancellation() override;
    bool m_buttonPress {false};
    void requestStrokeEnd() override;

protected Q_SLOTS:
    void resetCursorStyle() override;

private:
    friend class __KisToolPathLocalTool;
};

class KisToolPathFactory : public KisToolPaintFactoryBase
{

public:
    KisToolPathFactory()
            : KisToolPaintFactoryBase("KisToolPath") {
        setToolTip(i18n("Bezier Curve Tool: Shift-mouseclick ends the curve."));
        setSection(ToolBoxSection::Shape);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
        setIconName(koIconNameCStr("krita_draw_path"));
        setPriority(7);
    }

    ~KisToolPathFactory() override {}

    KoToolBase * createTool(KoCanvasBase *canvas) override {
        return new KisToolPath(canvas);
    }
};



#endif // KIS_TOOL_PATH_H_
