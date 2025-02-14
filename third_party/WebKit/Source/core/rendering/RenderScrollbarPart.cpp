/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "core/rendering/RenderScrollbarPart.h"

#include "core/frame/UseCounter.h"
#include "core/layout/PaintInfo.h"
#include "core/rendering/RenderScrollbar.h"
#include "core/rendering/RenderScrollbarTheme.h"
#include "core/rendering/RenderView.h"
#include "platform/LengthFunctions.h"

namespace blink {

RenderScrollbarPart::RenderScrollbarPart(RenderScrollbar* scrollbar, ScrollbarPart part)
    : RenderBlock(0)
    , m_scrollbar(scrollbar)
    , m_part(part)
{
}

RenderScrollbarPart::~RenderScrollbarPart()
{
}

static void recordScrollbarPartStats(Document& document, ScrollbarPart part)
{
    switch (part) {
    case BackButtonStartPart:
    case ForwardButtonStartPart:
    case BackButtonEndPart:
    case ForwardButtonEndPart:
        UseCounter::count(document, UseCounter::CSSSelectorPseudoScrollbarButton);
        break;
    case BackTrackPart:
    case ForwardTrackPart:
        UseCounter::count(document, UseCounter::CSSSelectorPseudoScrollbarTrackPiece);
        break;
    case ThumbPart:
        UseCounter::count(document, UseCounter::CSSSelectorPseudoScrollbarThumb);
        break;
    case TrackBGPart:
        UseCounter::count(document, UseCounter::CSSSelectorPseudoScrollbarTrack);
        break;
    case ScrollbarBGPart:
        UseCounter::count(document, UseCounter::CSSSelectorPseudoScrollbar);
        break;
    case NoPart:
    case AllParts:
        break;
    }
}

RenderScrollbarPart* RenderScrollbarPart::createAnonymous(Document* document, RenderScrollbar* scrollbar, ScrollbarPart part)
{
    RenderScrollbarPart* renderer = new RenderScrollbarPart(scrollbar, part);
    recordScrollbarPartStats(*document, part);
    renderer->setDocumentForAnonymous(document);
    return renderer;
}

void RenderScrollbarPart::layout()
{
    setLocation(LayoutPoint()); // We don't worry about positioning ourselves. We're just determining our minimum width/height.
    if (m_scrollbar->orientation() == HorizontalScrollbar)
        layoutHorizontalPart();
    else
        layoutVerticalPart();

    clearNeedsLayout();
}

void RenderScrollbarPart::layoutHorizontalPart()
{
    if (m_part == ScrollbarBGPart) {
        setWidth(m_scrollbar->width());
        computeScrollbarHeight();
    } else {
        computeScrollbarWidth();
        setHeight(m_scrollbar->height());
    }
}

void RenderScrollbarPart::layoutVerticalPart()
{
    if (m_part == ScrollbarBGPart) {
        computeScrollbarWidth();
        setHeight(m_scrollbar->height());
    } else {
        setWidth(m_scrollbar->width());
        computeScrollbarHeight();
    }
}

static int calcScrollbarThicknessUsing(SizeType sizeType, const Length& length, int containingLength)
{
    if (!length.isIntrinsicOrAuto() || (sizeType == MinSize && length.isAuto()))
        return minimumValueForLength(length, containingLength);
    return ScrollbarTheme::theme()->scrollbarThickness();
}

void RenderScrollbarPart::computeScrollbarWidth()
{
    if (!m_scrollbar->owningRenderer())
        return;
    // FIXME: We are querying layout information but nothing guarantees that it's up-to-date, especially since we are called at style change.
    // FIXME: Querying the style's border information doesn't work on table cells with collapsing borders.
    int visibleSize = m_scrollbar->owningRenderer()->size().width() - m_scrollbar->owningRenderer()->style()->borderLeftWidth() - m_scrollbar->owningRenderer()->style()->borderRightWidth();
    int w = calcScrollbarThicknessUsing(MainOrPreferredSize, style()->width(), visibleSize);
    int minWidth = calcScrollbarThicknessUsing(MinSize, style()->minWidth(), visibleSize);
    int maxWidth = style()->maxWidth().isMaxSizeNone() ? w : calcScrollbarThicknessUsing(MaxSize, style()->maxWidth(), visibleSize);
    setWidth(std::max(minWidth, std::min(maxWidth, w)));

    // Buttons and track pieces can all have margins along the axis of the scrollbar.
    setMarginLeft(minimumValueForLength(style()->marginLeft(), visibleSize));
    setMarginRight(minimumValueForLength(style()->marginRight(), visibleSize));
}

void RenderScrollbarPart::computeScrollbarHeight()
{
    if (!m_scrollbar->owningRenderer())
        return;
    // FIXME: We are querying layout information but nothing guarantees that it's up-to-date, especially since we are called at style change.
    // FIXME: Querying the style's border information doesn't work on table cells with collapsing borders.
    int visibleSize = m_scrollbar->owningRenderer()->size().height() -  m_scrollbar->owningRenderer()->style()->borderTopWidth() - m_scrollbar->owningRenderer()->style()->borderBottomWidth();
    int h = calcScrollbarThicknessUsing(MainOrPreferredSize, style()->height(), visibleSize);
    int minHeight = calcScrollbarThicknessUsing(MinSize, style()->minHeight(), visibleSize);
    int maxHeight = style()->maxHeight().isMaxSizeNone() ? h : calcScrollbarThicknessUsing(MaxSize, style()->maxHeight(), visibleSize);
    setHeight(std::max(minHeight, std::min(maxHeight, h)));

    // Buttons and track pieces can all have margins along the axis of the scrollbar.
    setMarginTop(minimumValueForLength(style()->marginTop(), visibleSize));
    setMarginBottom(minimumValueForLength(style()->marginBottom(), visibleSize));
}

void RenderScrollbarPart::computePreferredLogicalWidths()
{
    if (!preferredLogicalWidthsDirty())
        return;

    m_minPreferredLogicalWidth = m_maxPreferredLogicalWidth = 0;

    clearPreferredLogicalWidthsDirty();
}

void RenderScrollbarPart::styleWillChange(StyleDifference diff, const LayoutStyle& newStyle)
{
    RenderBlock::styleWillChange(diff, newStyle);
    setInline(false);
}

void RenderScrollbarPart::styleDidChange(StyleDifference diff, const LayoutStyle* oldStyle)
{
    RenderBlock::styleDidChange(diff, oldStyle);
    setInline(false);
    clearPositionedState();
    setFloating(false);
    setHasOverflowClip(false);
    if (oldStyle && m_scrollbar && m_part != NoPart && (diff.needsPaintInvalidation() || diff.needsLayout()))
        m_scrollbar->theme()->invalidatePart(m_scrollbar, m_part);
}

void RenderScrollbarPart::imageChanged(WrappedImagePtr image, const IntRect* rect)
{
    if (m_scrollbar && m_part != NoPart)
        m_scrollbar->theme()->invalidatePart(m_scrollbar, m_part);
    else {
        if (FrameView* frameView = view()->frameView()) {
            if (frameView->isFrameViewScrollCorner(this)) {
                frameView->invalidateScrollCorner(frameView->scrollCornerRect());
                return;
            }
        }

        RenderBlock::imageChanged(image, rect);
    }
}

LayoutObject* RenderScrollbarPart::rendererOwningScrollbar() const
{
    if (!m_scrollbar)
        return 0;
    return m_scrollbar->owningRenderer();
}

}
