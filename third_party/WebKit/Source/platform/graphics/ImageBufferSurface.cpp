/*
 * Copyright (c) 2013, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "platform/graphics/ImageBufferSurface.h"

#include "platform/graphics/BitmapImage.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/ImageBuffer.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDevice.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace blink {

ImageBufferSurface::ImageBufferSurface(const IntSize& size, OpacityMode opacityMode)
    : m_opacityMode(opacityMode)
    , m_size(size)
{
    setIsHidden(false);
}

ImageBufferSurface::~ImageBufferSurface() { }

PassRefPtr<SkPicture> ImageBufferSurface::getPicture()
{
    return nullptr;
}

void ImageBufferSurface::clear()
{
    // Clear the background transparent or opaque, as required. It would be nice if this wasn't
    // required, but the canvas is currently filled with the magic transparency
    // color. Can we have another way to manage this?
    if (isValid()) {
        if (m_opacityMode == Opaque)
            canvas()->drawARGB(255, 0, 0, 0, SkXfermode::kSrc_Mode);
        else
            canvas()->drawARGB(0, 0, 0, 0, SkXfermode::kClear_Mode);
    }
}

const SkBitmap& ImageBufferSurface::bitmap()
{
    ASSERT(canvas());
    willAccessPixels();
    return canvas()->getTopDevice()->accessBitmap(false);
}

const SkBitmap& ImageBufferSurface::cachedBitmap() const
{
    DEFINE_STATIC_LOCAL(SkBitmap, nullBitmap, ());
    return nullBitmap;
}

PassRefPtr<SkImage> ImageBufferSurface::newImageSnapshot() const
{
    return nullptr;
}

static SkBitmap deepSkBitmapCopy(const SkBitmap& bitmap)
{
    SkBitmap tmp;
    if (!bitmap.deepCopyTo(&tmp))
        bitmap.copyTo(&tmp, bitmap.colorType());

    return tmp;
}

void ImageBufferSurface::draw(GraphicsContext* context, const FloatRect& destRect, const FloatRect& srcRect, SkXfermode::Mode op, bool needsCopy)
{
    SkBitmap bmp = bitmap();
    // For ImageBufferSurface that enables cachedBitmap, Use the cached bitmap for CPU side usage
    // if it is available, otherwise generate and use it.
    if (!context->isAccelerated() && isAccelerated() && cachedBitmapEnabled() && isValid()) {
        updateCachedBitmapIfNeeded();
        bmp = cachedBitmap();
    }

    RefPtr<Image> image = BitmapImage::create(NativeImageSkia::create(needsCopy ? deepSkBitmapCopy(bmp) : bmp));

    context->drawImage(image.get(), destRect, srcRect, op, DoNotRespectImageOrientation);
}

} // namespace blink
