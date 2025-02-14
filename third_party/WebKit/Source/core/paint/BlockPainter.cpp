// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/paint/BlockPainter.h"

#include "core/editing/Caret.h"
#include "core/editing/FrameSelection.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/layout/Layer.h"
#include "core/layout/PaintInfo.h"
#include "core/page/Page.h"
#include "core/paint/BoxClipper.h"
#include "core/paint/BoxPainter.h"
#include "core/paint/GraphicsContextAnnotator.h"
#include "core/paint/InlinePainter.h"
#include "core/paint/LineBoxListPainter.h"
#include "core/paint/RenderDrawingRecorder.h"
#include "core/paint/ScopeRecorder.h"
#include "core/paint/ScrollRecorder.h"
#include "core/paint/ScrollableAreaPainter.h"
#include "core/paint/SubtreeRecorder.h"
#include "core/rendering/RenderBlock.h"
#include "core/rendering/RenderFlexibleBox.h"
#include "core/rendering/RenderInline.h"
#include "core/rendering/RenderView.h"
#include "platform/geometry/LayoutPoint.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/paint/ClipRecorder.h"

namespace blink {

void BlockPainter::paint(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    SubtreeRecorder subtreeRecorder(paintInfo.context, m_renderBlock, paintInfo.phase);
    PaintInfo localPaintInfo(paintInfo);

    ANNOTATE_GRAPHICS_CONTEXT(localPaintInfo, &m_renderBlock);

    LayoutPoint adjustedPaintOffset = paintOffset + m_renderBlock.location();

    PaintPhase originalPhase = localPaintInfo.phase;

    // Check if we need to do anything at all.
    // FIXME: Could eliminate the isDocumentElement() check if we fix background painting so that the RenderView
    // paints the root's background.
    if (!m_renderBlock.isDocumentElement()) {
        LayoutRect overflowBox = overflowRectForPaintRejection();
        m_renderBlock.flipForWritingMode(overflowBox);
        overflowBox.moveBy(adjustedPaintOffset);
        if (!overflowBox.intersects(localPaintInfo.rect))
            return;
    }

    subtreeRecorder.begin();

    // There are some cases where not all clipped visual overflow is accounted for.
    // FIXME: reduce the number of such cases.
    ContentsClipBehavior contentsClipBehavior = ForceContentsClip;
    if (m_renderBlock.hasOverflowClip() && !m_renderBlock.hasControlClip() && !(m_renderBlock.shouldPaintSelectionGaps() && originalPhase == PaintPhaseForeground) && !hasCaret())
        contentsClipBehavior = SkipContentsClipIfPossible;

    if (localPaintInfo.phase == PaintPhaseOutline) {
        localPaintInfo.phase = PaintPhaseChildOutlines;
    } else if (localPaintInfo.phase == PaintPhaseChildBlockBackground) {
        localPaintInfo.phase = PaintPhaseBlockBackground;
        m_renderBlock.paintObject(localPaintInfo, adjustedPaintOffset);
        localPaintInfo.phase = PaintPhaseChildBlockBackgrounds;
    }

    {
        BoxClipper boxClipper(m_renderBlock, localPaintInfo, adjustedPaintOffset, contentsClipBehavior);
        m_renderBlock.paintObject(localPaintInfo, adjustedPaintOffset);
    }

    if (originalPhase == PaintPhaseOutline) {
        localPaintInfo.phase = PaintPhaseSelfOutline;
        m_renderBlock.paintObject(localPaintInfo, adjustedPaintOffset);
        localPaintInfo.phase = originalPhase;
    } else if (originalPhase == PaintPhaseChildBlockBackground) {
        localPaintInfo.phase = originalPhase;
    }

    // Our scrollbar widgets paint exactly when we tell them to, so that they work properly with
    // z-index. We paint after we painted the background/border, so that the scrollbars will
    // sit above the background/border.
    paintOverflowControlsIfNeeded(localPaintInfo, adjustedPaintOffset);
}

void BlockPainter::paintOverflowControlsIfNeeded(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    PaintPhase phase = paintInfo.phase;
    if (m_renderBlock.hasOverflowClip() && m_renderBlock.style()->visibility() == VISIBLE && (phase == PaintPhaseBlockBackground || phase == PaintPhaseChildBlockBackground) && paintInfo.shouldPaintWithinRoot(&m_renderBlock) && !paintInfo.paintRootBackgroundOnly()) {
        ScrollableAreaPainter(*m_renderBlock.layer()->scrollableArea()).paintOverflowControls(paintInfo.context, roundedIntPoint(paintOffset), paintInfo.rect, false /* paintingOverlayControls */);
    }
}

void BlockPainter::paintChildren(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    for (RenderBox* child = m_renderBlock.firstChildBox(); child; child = child->nextSiblingBox())
        paintChild(*child, paintInfo, paintOffset);
}

void BlockPainter::paintChild(RenderBox& child, const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    LayoutPoint childPoint = m_renderBlock.flipForWritingModeForChild(&child, paintOffset);
    if (!child.hasSelfPaintingLayer() && !child.isFloating() && !child.isColumnSpanAll())
        child.paint(paintInfo, childPoint);
}

void BlockPainter::paintChildrenOfFlexibleBox(RenderFlexibleBox& renderFlexibleBox, const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    for (RenderBox* child = renderFlexibleBox.orderIterator().first(); child; child = renderFlexibleBox.orderIterator().next())
        BlockPainter(renderFlexibleBox).paintChildAsInlineBlock(*child, paintInfo, paintOffset);
}

void BlockPainter::paintChildAsInlineBlock(RenderBox& child, const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    LayoutPoint childPoint = m_renderBlock.flipForWritingModeForChild(&child, paintOffset);
    if (!child.hasSelfPaintingLayer() && !child.isFloating())
        paintAsInlineBlock(child, paintInfo, childPoint);
}

void BlockPainter::paintInlineBox(InlineBox& inlineBox, const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!paintInfo.shouldPaintWithinRoot(&inlineBox.renderer()) || (paintInfo.phase != PaintPhaseForeground && paintInfo.phase != PaintPhaseSelection))
        return;

    LayoutPoint childPoint = paintOffset;
    if (inlineBox.parent()->renderer().style()->isFlippedBlocksWritingMode()) // Faster than calling containingBlock().
        childPoint = inlineBox.renderer().containingBlock()->flipForWritingModeForChild(&toRenderBox(inlineBox.renderer()), childPoint);

    paintAsInlineBlock(inlineBox.renderer(), paintInfo, childPoint);
}

void BlockPainter::paintAsInlineBlock(LayoutObject& renderer, const PaintInfo& paintInfo, const LayoutPoint& childPoint)
{
    if (paintInfo.phase != PaintPhaseForeground && paintInfo.phase != PaintPhaseSelection)
        return;

    // Paint all phases atomically, as though the element established its own
    // stacking context.  (See Appendix E.2, section 7.2.1.4 on
    // inline block/table/replaced elements in the CSS2.1 specification.)
    // This is also used by other elements (e.g. flex items and grid items).
    bool preservePhase = paintInfo.phase == PaintPhaseSelection || paintInfo.phase == PaintPhaseTextClip;
    PaintInfo info(paintInfo);
    info.phase = preservePhase ? paintInfo.phase : PaintPhaseBlockBackground;
    renderer.paint(info, childPoint);
    if (!preservePhase) {
        info.phase = PaintPhaseChildBlockBackgrounds;
        renderer.paint(info, childPoint);
        info.phase = PaintPhaseFloat;
        renderer.paint(info, childPoint);
        info.phase = PaintPhaseForeground;
        renderer.paint(info, childPoint);
        info.phase = PaintPhaseOutline;
        renderer.paint(info, childPoint);
    }
}

void BlockPainter::paintObject(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    PaintPhase paintPhase = paintInfo.phase;

    LayoutPoint scrolledOffset = paintOffset;
    if (!RuntimeEnabledFeatures::slimmingPaintCompositorLayerizationEnabled()) {
        // Adjust our painting position if we're inside a scrolled layer (e.g., an overflow:auto div).
        // With compositor layerization, a scroll display item is used instead.
        if (m_renderBlock.hasOverflowClip())
            scrolledOffset.move(-m_renderBlock.scrolledContentOffset());
    }

    LayoutRect bounds;
    if (RuntimeEnabledFeatures::slimmingPaintEnabled()) {
        bounds = m_renderBlock.visualOverflowRect();
        bounds.moveBy(scrolledOffset);
    }

    if ((paintPhase == PaintPhaseBlockBackground || paintPhase == PaintPhaseChildBlockBackground)
        && m_renderBlock.style()->visibility() == VISIBLE
        && m_renderBlock.hasBoxDecorationBackground())
        m_renderBlock.paintBoxDecorationBackground(paintInfo, paintOffset);

    if (paintPhase == PaintPhaseMask && m_renderBlock.style()->visibility() == VISIBLE) {
        RenderDrawingRecorder recorder(paintInfo.context, m_renderBlock, paintPhase, bounds);
        if (!recorder.canUseCachedDrawing())
            m_renderBlock.paintMask(paintInfo, paintOffset);
        return;
    }

    if (paintPhase == PaintPhaseClippingMask && m_renderBlock.style()->visibility() == VISIBLE) {
        RenderDrawingRecorder recorder(paintInfo.context, m_renderBlock, paintPhase, bounds);
        if (!recorder.canUseCachedDrawing())
            m_renderBlock.paintClippingMask(paintInfo, paintOffset);
        return;
    }

    {
        OwnPtr<ScrollRecorder> scrollRecorder;
        if (RuntimeEnabledFeatures::slimmingPaintCompositorLayerizationEnabled()
            && m_renderBlock.hasOverflowClip()
            && m_renderBlock.layer()->scrollsOverflow()) {
            scrollRecorder = adoptPtr(new ScrollRecorder(
                paintInfo.context,
                m_renderBlock.displayItemClient(),
                paintPhase,
                m_renderBlock.scrolledContentOffset()));
        }

        if ((paintPhase == PaintPhaseBlockBackground || paintPhase == PaintPhaseChildBlockBackground)
            && m_renderBlock.style()->visibility() == VISIBLE
            && m_renderBlock.hasColumns()
            && !paintInfo.paintRootBackgroundOnly()) {
            RenderDrawingRecorder recorder(paintInfo.context, m_renderBlock, DisplayItem::ColumnRules, bounds);
            if (!recorder.canUseCachedDrawing())
                paintColumnRules(paintInfo, scrolledOffset);
        }

        // We're done. We don't bother painting any children.
        if (paintPhase == PaintPhaseBlockBackground || paintInfo.paintRootBackgroundOnly())
            return;

        if (paintPhase != PaintPhaseSelfOutline) {
            if (m_renderBlock.hasColumns())
                paintColumnContents(paintInfo, scrolledOffset);
            else
                paintContents(paintInfo, scrolledOffset);
        }

        // FIXME: Make this work with multi column layouts. For now don't fill gaps.
        bool isPrinting = m_renderBlock.document().printing();
        if (!isPrinting && !m_renderBlock.hasColumns())
            m_renderBlock.paintSelection(paintInfo, scrolledOffset); // Fill in gaps in selection on lines and between blocks.

        if (paintPhase == PaintPhaseFloat || paintPhase == PaintPhaseSelection || paintPhase == PaintPhaseTextClip) {
            if (m_renderBlock.hasColumns())
                paintColumnContents(paintInfo, scrolledOffset, true);
            else
                m_renderBlock.paintFloats(paintInfo, scrolledOffset, paintPhase == PaintPhaseSelection || paintPhase == PaintPhaseTextClip);
        }
    }

    if ((paintPhase == PaintPhaseOutline || paintPhase == PaintPhaseSelfOutline) && m_renderBlock.style()->hasOutline() && m_renderBlock.style()->visibility() == VISIBLE) {
        // Don't paint focus ring for anonymous block continuation because the
        // inline element having outline-style:auto paints the whole focus ring.
        if (!m_renderBlock.style()->outlineStyleIsAuto() || !m_renderBlock.isAnonymousBlockContinuation())
            ObjectPainter(m_renderBlock).paintOutline(paintInfo, LayoutRect(paintOffset, m_renderBlock.size()));
    }

    if (paintPhase == PaintPhaseOutline || paintPhase == PaintPhaseChildOutlines)
        paintContinuationOutlines(paintInfo, paintOffset);

    // If the caret's node's render object's containing block is this block, and the paint action is PaintPhaseForeground,
    // then paint the caret.
    if (paintPhase == PaintPhaseForeground && hasCaret()) {
        RenderDrawingRecorder recorder(paintInfo.context, m_renderBlock, DisplayItem::Caret, bounds);
        if (!recorder.canUseCachedDrawing())
            paintCarets(paintInfo, paintOffset);
    }
}

static inline bool caretBrowsingEnabled(const Frame* frame)
{
    Settings* settings = frame->settings();
    return settings && settings->caretBrowsingEnabled();
}

static inline bool hasCursorCaret(const FrameSelection& selection, const RenderBlock* block, bool caretBrowsing)
{
    return selection.caretRenderer() == block && (selection.hasEditableStyle() || caretBrowsing);
}

static inline bool hasDragCaret(const DragCaretController& dragCaretController, const RenderBlock* block, bool caretBrowsing)
{
    return dragCaretController.caretRenderer() == block && (dragCaretController.isContentEditable() || caretBrowsing);
}

void BlockPainter::paintCarets(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    bool caretBrowsing = caretBrowsingEnabled(m_renderBlock.frame());

    FrameSelection& selection = m_renderBlock.frame()->selection();
    if (hasCursorCaret(selection, &m_renderBlock, caretBrowsing)) {
        selection.paintCaret(paintInfo.context, paintOffset, paintInfo.rect);
    }

    DragCaretController& dragCaretController = m_renderBlock.frame()->page()->dragCaretController();
    if (hasDragCaret(dragCaretController, &m_renderBlock, caretBrowsing)) {
        dragCaretController.paintDragCaret(m_renderBlock.frame(), paintInfo.context, paintOffset, paintInfo.rect);
    }
}

LayoutRect BlockPainter::overflowRectForPaintRejection() const
{
    LayoutRect overflowRect = m_renderBlock.visualOverflowRect();
    if (!m_renderBlock.hasRenderOverflow() || !m_renderBlock.usesCompositedScrolling())
        return overflowRect;

    overflowRect.unite(m_renderBlock.layoutOverflowRect());
    overflowRect.move(-m_renderBlock.scrolledContentOffset());
    return overflowRect;
}

bool BlockPainter::hasCaret() const
{
    bool caretBrowsing = caretBrowsingEnabled(m_renderBlock.frame());
    return hasCursorCaret(m_renderBlock.frame()->selection(), &m_renderBlock, caretBrowsing)
        || hasDragCaret(m_renderBlock.frame()->page()->dragCaretController(), &m_renderBlock, caretBrowsing);
}

void BlockPainter::paintColumnRules(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    const Color& ruleColor = m_renderBlock.resolveColor(CSSPropertyWebkitColumnRuleColor);
    bool ruleTransparent = m_renderBlock.style()->columnRuleIsTransparent();
    EBorderStyle ruleStyle = m_renderBlock.style()->columnRuleStyle();
    LayoutUnit ruleThickness = m_renderBlock.style()->columnRuleWidth();
    LayoutUnit colGap = m_renderBlock.columnGap();
    bool renderRule = ruleStyle > BHIDDEN && !ruleTransparent;
    if (!renderRule)
        return;

    ColumnInfo* colInfo = m_renderBlock.columnInfo();
    unsigned colCount = m_renderBlock.columnCount(colInfo);

    bool antialias = BoxPainter::shouldAntialiasLines(paintInfo.context);

    if (colInfo->progressionAxis() == ColumnInfo::InlineAxis) {
        bool leftToRight = m_renderBlock.style()->isLeftToRightDirection();
        LayoutUnit currLogicalLeftOffset = leftToRight ? LayoutUnit() : m_renderBlock.contentLogicalWidth();
        LayoutUnit ruleAdd = m_renderBlock.logicalLeftOffsetForContent();
        LayoutUnit ruleLogicalLeft = leftToRight ? LayoutUnit() : m_renderBlock.contentLogicalWidth();
        LayoutUnit inlineDirectionSize = colInfo->desiredColumnWidth();
        BoxSide boxSide = m_renderBlock.isHorizontalWritingMode()
            ? leftToRight ? BSLeft : BSRight
            : leftToRight ? BSTop : BSBottom;

        for (unsigned i = 0; i < colCount; i++) {
            // Move to the next position.
            if (leftToRight) {
                ruleLogicalLeft += inlineDirectionSize + colGap / 2;
                currLogicalLeftOffset += inlineDirectionSize + colGap;
            } else {
                ruleLogicalLeft -= (inlineDirectionSize + colGap / 2);
                currLogicalLeftOffset -= (inlineDirectionSize + colGap);
            }

            // Now paint the column rule.
            if (i < colCount - 1) {
                LayoutUnit ruleLeft = m_renderBlock.isHorizontalWritingMode() ? paintOffset.x() + ruleLogicalLeft - ruleThickness / 2 + ruleAdd : paintOffset.x() + m_renderBlock.borderLeft() + m_renderBlock.paddingLeft();
                LayoutUnit ruleRight = m_renderBlock.isHorizontalWritingMode() ? ruleLeft + ruleThickness : ruleLeft + m_renderBlock.contentWidth();
                LayoutUnit ruleTop = m_renderBlock.isHorizontalWritingMode() ? paintOffset.y() + m_renderBlock.borderTop() + m_renderBlock.paddingTop() : paintOffset.y() + ruleLogicalLeft - ruleThickness / 2 + ruleAdd;
                LayoutUnit ruleBottom = m_renderBlock.isHorizontalWritingMode() ? ruleTop + m_renderBlock.contentHeight() : ruleTop + ruleThickness;
                IntRect pixelSnappedRuleRect = pixelSnappedIntRectFromEdges(ruleLeft, ruleTop, ruleRight, ruleBottom);
                ObjectPainter::drawLineForBoxSide(paintInfo.context, pixelSnappedRuleRect.x(), pixelSnappedRuleRect.y(), pixelSnappedRuleRect.maxX(), pixelSnappedRuleRect.maxY(), boxSide, ruleColor, ruleStyle, 0, 0, antialias);
            }

            ruleLogicalLeft = currLogicalLeftOffset;
        }
    } else {
        bool topToBottom = !m_renderBlock.style()->isFlippedBlocksWritingMode();
        LayoutUnit ruleLeft = m_renderBlock.isHorizontalWritingMode()
            ? m_renderBlock.borderLeft() + m_renderBlock.paddingLeft()
            : colGap / 2 - colGap - ruleThickness / 2 + m_renderBlock.borderBefore() + m_renderBlock.paddingBefore();
        LayoutUnit ruleWidth = m_renderBlock.isHorizontalWritingMode() ? m_renderBlock.contentWidth() : ruleThickness;
        LayoutUnit ruleTop = m_renderBlock.isHorizontalWritingMode()
            ? colGap / 2 - colGap - ruleThickness / 2 + m_renderBlock.borderBefore() + m_renderBlock.paddingBefore()
            : m_renderBlock.borderStart() + m_renderBlock.paddingStart();
        LayoutUnit ruleHeight = m_renderBlock.isHorizontalWritingMode() ? ruleThickness : m_renderBlock.contentHeight();
        LayoutRect ruleRect(ruleLeft, ruleTop, ruleWidth, ruleHeight);

        if (!topToBottom) {
            if (m_renderBlock.isHorizontalWritingMode())
                ruleRect.setY(m_renderBlock.size().height() - ruleRect.maxY());
            else
                ruleRect.setX(m_renderBlock.size().width() - ruleRect.maxX());
        }

        ruleRect.moveBy(paintOffset);

        BoxSide boxSide = m_renderBlock.isHorizontalWritingMode()
            ? topToBottom ? BSTop : BSBottom
            : topToBottom ? BSLeft : BSRight;

        LayoutSize step(0, topToBottom ? colInfo->columnHeight() + colGap : -(colInfo->columnHeight() + colGap));
        if (!m_renderBlock.isHorizontalWritingMode())
            step = step.transposedSize();

        for (unsigned i = 1; i < colCount; i++) {
            ruleRect.move(step);
            IntRect pixelSnappedRuleRect = pixelSnappedIntRect(ruleRect);
            ObjectPainter::drawLineForBoxSide(paintInfo.context, pixelSnappedRuleRect.x(), pixelSnappedRuleRect.y(), pixelSnappedRuleRect.maxX(), pixelSnappedRuleRect.maxY(), boxSide, ruleColor, ruleStyle, 0, 0, antialias);
        }
    }
}

void BlockPainter::paintColumnContents(const PaintInfo& paintInfo, const LayoutPoint& paintOffset, bool paintingFloats)
{
    // We need to do multiple passes, breaking up our child painting into strips.
    ColumnInfo* colInfo = m_renderBlock.columnInfo();
    unsigned colCount = m_renderBlock.columnCount(colInfo);
    if (!colCount)
        return;
    LayoutUnit currLogicalTopOffset = 0;
    LayoutUnit colGap = m_renderBlock.columnGap();

    for (unsigned i = 0; i < colCount; i++) {
        ScopeRecorder scopeRecorder(paintInfo.context, m_renderBlock);

        // For each rect, we clip to the rect, and then we adjust our coords.
        LayoutRect colRect = m_renderBlock.columnRectAt(colInfo, i);
        m_renderBlock.flipForWritingMode(colRect);
        LayoutUnit logicalLeftOffset = (m_renderBlock.isHorizontalWritingMode() ? colRect.x() : colRect.y()) - m_renderBlock.logicalLeftOffsetForContent();
        LayoutSize offset = m_renderBlock.isHorizontalWritingMode() ? LayoutSize(logicalLeftOffset, currLogicalTopOffset) : LayoutSize(currLogicalTopOffset, logicalLeftOffset);
        if (colInfo->progressionAxis() == ColumnInfo::BlockAxis) {
            if (m_renderBlock.isHorizontalWritingMode())
                offset.expand(0, colRect.y() - m_renderBlock.borderTop() - m_renderBlock.paddingTop());
            else
                offset.expand(colRect.x() - m_renderBlock.borderLeft() - m_renderBlock.paddingLeft(), 0);
        }
        colRect.moveBy(paintOffset);
        PaintInfo info(paintInfo);
        info.rect.intersect(enclosingIntRect(colRect));

        if (!info.rect.isEmpty()) {
            LayoutRect clipRect(colRect);

            if (i < colCount - 1) {
                if (m_renderBlock.isHorizontalWritingMode())
                    clipRect.expand(colGap / 2, 0);
                else
                    clipRect.expand(0, colGap / 2);
            }
            // Each strip pushes a clip, since column boxes are specified as being
            // like overflow:hidden.
            // FIXME: Content and column rules that extend outside column boxes at the edges of the multi-column element
            // are clipped according to the 'overflow' property.
            ClipRecorder clipRecorder(m_renderBlock.displayItemClient(), paintInfo.context,
                DisplayItem::paintPhaseToClipColumnBoundsType(paintInfo.phase), enclosingIntRect(clipRect));

            // Adjust our x and y when painting.
            LayoutPoint adjustedPaintOffset = paintOffset + offset;
            if (paintingFloats)
                m_renderBlock.paintFloats(info, adjustedPaintOffset, paintInfo.phase == PaintPhaseSelection || paintInfo.phase == PaintPhaseTextClip);
            else
                paintContents(info, adjustedPaintOffset);
        }

        LayoutUnit blockDelta = (m_renderBlock.isHorizontalWritingMode() ? colRect.height() : colRect.width());
        if (m_renderBlock.style()->isFlippedBlocksWritingMode())
            currLogicalTopOffset += blockDelta;
        else
            currLogicalTopOffset -= blockDelta;
    }
}

void BlockPainter::paintContents(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    // Avoid painting descendants of the root element when stylesheets haven't loaded. This eliminates FOUC.
    // It's ok not to draw, because later on, when all the stylesheets do load, styleResolverChanged() on the Document
    // will do a full paint invalidation.
    if (m_renderBlock.document().didLayoutWithPendingStylesheets() && !m_renderBlock.isRenderView())
        return;

    if (m_renderBlock.childrenInline()) {
        LineBoxListPainter(*m_renderBlock.lineBoxes()).paint(&m_renderBlock, paintInfo, paintOffset);
    } else {
        PaintPhase newPhase = (paintInfo.phase == PaintPhaseChildOutlines) ? PaintPhaseOutline : paintInfo.phase;
        newPhase = (newPhase == PaintPhaseChildBlockBackgrounds) ? PaintPhaseChildBlockBackground : newPhase;

        // We don't paint our own background, but we do let the kids paint their backgrounds.
        PaintInfo paintInfoForChild(paintInfo);
        paintInfoForChild.phase = newPhase;
        paintInfoForChild.updatePaintingRootForChildren(&m_renderBlock);
        m_renderBlock.paintChildren(paintInfoForChild, paintOffset);
    }
}

void BlockPainter::paintContinuationOutlines(const PaintInfo& info, const LayoutPoint& paintOffset)
{
    RenderInline* inlineCont = m_renderBlock.inlineElementContinuation();
    if (inlineCont && inlineCont->style()->hasOutline() && inlineCont->style()->visibility() == VISIBLE) {
        RenderInline* inlineRenderer = toRenderInline(inlineCont->node()->renderer());
        RenderBlock* cb = m_renderBlock.containingBlock();

        bool inlineEnclosedInSelfPaintingLayer = false;
        for (RenderBoxModelObject* box = inlineRenderer; box != cb; box = box->parent()->enclosingBoxModelObject()) {
            if (box->hasSelfPaintingLayer()) {
                inlineEnclosedInSelfPaintingLayer = true;
                break;
            }
        }

        // Do not add continuations for outline painting by our containing block if we are a relative positioned
        // anonymous block (i.e. have our own layer), paint them straightaway instead. This is because a block depends on renderers in its continuation table being
        // in the same layer.
        if (!inlineEnclosedInSelfPaintingLayer && !m_renderBlock.hasLayer())
            cb->addContinuationWithOutline(inlineRenderer);
        else if (!inlineRenderer->firstLineBox() || (!inlineEnclosedInSelfPaintingLayer && m_renderBlock.hasLayer()))
            InlinePainter(*inlineRenderer).paintOutline(info, paintOffset - m_renderBlock.locationOffset() + inlineRenderer->containingBlock()->location());
    }

    ContinuationOutlineTableMap* table = continuationOutlineTable();
    if (table->isEmpty())
        return;

    OwnPtr<ListHashSet<RenderInline*> > continuations = table->take(&m_renderBlock);
    if (!continuations)
        return;

    LayoutPoint accumulatedPaintOffset = paintOffset;
    // Paint each continuation outline.
    ListHashSet<RenderInline*>::iterator end = continuations->end();
    for (ListHashSet<RenderInline*>::iterator it = continuations->begin(); it != end; ++it) {
        // Need to add in the coordinates of the intervening blocks.
        RenderInline* flow = *it;
        RenderBlock* block = flow->containingBlock();
        for ( ; block && block != &m_renderBlock; block = block->containingBlock())
            accumulatedPaintOffset.moveBy(block->location());
        ASSERT(block);
        InlinePainter(*flow).paintOutline(info, accumulatedPaintOffset);
    }
}


} // namespace blink
