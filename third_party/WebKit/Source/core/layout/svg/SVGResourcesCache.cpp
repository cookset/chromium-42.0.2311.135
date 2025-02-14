/*
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "core/layout/svg/SVGResourcesCache.h"

#include "core/HTMLNames.h"
#include "core/layout/svg/LayoutSVGResourceContainer.h"
#include "core/layout/svg/SVGResources.h"
#include "core/layout/svg/SVGResourcesCycleSolver.h"
#include "core/svg/SVGDocumentExtensions.h"

namespace blink {

SVGResourcesCache::SVGResourcesCache()
{
}

SVGResourcesCache::~SVGResourcesCache()
{
}

void SVGResourcesCache::addResourcesFromLayoutObject(LayoutObject* object, const LayoutStyle& style)
{
    ASSERT(object);
    ASSERT(!m_cache.contains(object));

    const SVGLayoutStyle& svgStyle = style.svgStyle();

    // Build a list of all resources associated with the passed LayoutObject
    OwnPtr<SVGResources> newResources = SVGResources::buildResources(object, svgStyle);
    if (!newResources)
        return;

    // Put object in cache.
    SVGResources* resources = m_cache.set(object, newResources.release()).storedValue->value.get();

    // Run cycle-detection _afterwards_, so self-references can be caught as well.
    SVGResourcesCycleSolver solver(object, resources);
    solver.resolveCycles();

    // Walk resources and register the render object at each resources.
    HashSet<LayoutSVGResourceContainer*> resourceSet;
    resources->buildSetOfResources(resourceSet);

    for (auto* resourceContainer : resourceSet)
        resourceContainer->addClient(object);
}

void SVGResourcesCache::removeResourcesFromLayoutObject(LayoutObject* object)
{
    OwnPtr<SVGResources> resources = m_cache.take(object);
    if (!resources)
        return;

    // Walk resources and register the render object at each resources.
    HashSet<LayoutSVGResourceContainer*> resourceSet;
    resources->buildSetOfResources(resourceSet);

    for (auto* resourceContainer : resourceSet)
        resourceContainer->removeClient(object);
}

static inline SVGResourcesCache* resourcesCacheFromLayoutObject(const LayoutObject* renderer)
{
    Document& document = renderer->document();

    SVGDocumentExtensions& extensions = document.accessSVGExtensions();
    SVGResourcesCache* cache = extensions.resourcesCache();
    ASSERT(cache);

    return cache;
}

SVGResources* SVGResourcesCache::cachedResourcesForLayoutObject(const LayoutObject* renderer)
{
    ASSERT(renderer);
    return resourcesCacheFromLayoutObject(renderer)->m_cache.get(renderer);
}

void SVGResourcesCache::clientLayoutChanged(LayoutObject* object)
{
    SVGResources* resources = SVGResourcesCache::cachedResourcesForLayoutObject(object);
    if (!resources)
        return;

    // Invalidate the resources if either the LayoutObject itself changed,
    // or we have filter resources, which could depend on the layout of children.
    if (object->selfNeedsLayout() || resources->filter())
        resources->removeClientFromCache(object);
}

static inline bool rendererCanHaveResources(LayoutObject* renderer)
{
    ASSERT(renderer);
    return renderer->node() && renderer->node()->isSVGElement() && !renderer->isSVGInlineText();
}

void SVGResourcesCache::clientStyleChanged(LayoutObject* renderer, StyleDifference diff, const LayoutStyle& newStyle)
{
    ASSERT(renderer);
    ASSERT(renderer->node());
    ASSERT(renderer->node()->isSVGElement());

    if (!diff.hasDifference() || !renderer->parent())
        return;

    // In this case the proper SVGFE*Element will decide whether the modified CSS properties require a relayout or paintInvalidation.
    if (renderer->isSVGResourceFilterPrimitive() && !diff.needsLayout())
        return;

    // Dynamic changes of CSS properties like 'clip-path' may require us to recompute the associated resources for a renderer.
    // FIXME: Avoid passing in a useless StyleDifference, but instead compare oldStyle/newStyle to see which resources changed
    // to be able to selectively rebuild individual resources, instead of all of them.
    if (rendererCanHaveResources(renderer)) {
        SVGResourcesCache* cache = resourcesCacheFromLayoutObject(renderer);
        cache->removeResourcesFromLayoutObject(renderer);
        cache->addResourcesFromLayoutObject(renderer, newStyle);
    }

    LayoutSVGResourceContainer::markForLayoutAndParentResourceInvalidation(renderer, false);
}

void SVGResourcesCache::clientWasAddedToTree(LayoutObject* renderer, const LayoutStyle& newStyle)
{
    if (!renderer->node())
        return;
    LayoutSVGResourceContainer::markForLayoutAndParentResourceInvalidation(renderer, false);

    if (!rendererCanHaveResources(renderer))
        return;
    SVGResourcesCache* cache = resourcesCacheFromLayoutObject(renderer);
    cache->addResourcesFromLayoutObject(renderer, newStyle);
}

void SVGResourcesCache::clientWillBeRemovedFromTree(LayoutObject* renderer)
{
    if (!renderer->node())
        return;
    LayoutSVGResourceContainer::markForLayoutAndParentResourceInvalidation(renderer, false);

    if (!rendererCanHaveResources(renderer))
        return;
    SVGResourcesCache* cache = resourcesCacheFromLayoutObject(renderer);
    cache->removeResourcesFromLayoutObject(renderer);
}

void SVGResourcesCache::clientDestroyed(LayoutObject* renderer)
{
    ASSERT(renderer);

    SVGResources* resources = SVGResourcesCache::cachedResourcesForLayoutObject(renderer);
    if (resources)
        resources->removeClientFromCache(renderer);

    SVGResourcesCache* cache = resourcesCacheFromLayoutObject(renderer);
    cache->removeResourcesFromLayoutObject(renderer);
}

void SVGResourcesCache::resourceDestroyed(LayoutSVGResourceContainer* resource)
{
    ASSERT(resource);
    SVGResourcesCache* cache = resourcesCacheFromLayoutObject(resource);

    // The resource itself may have clients, that need to be notified.
    cache->removeResourcesFromLayoutObject(resource);

    for (auto& objectResources : cache->m_cache) {
        objectResources.value->resourceDestroyed(resource);

        // Mark users of destroyed resources as pending resolution based on the id of the old resource.
        Element* resourceElement = resource->element();
        Element* clientElement = toElement(objectResources.key->node());
        SVGDocumentExtensions& extensions = clientElement->document().accessSVGExtensions();

        extensions.addPendingResource(resourceElement->fastGetAttribute(HTMLNames::idAttr), clientElement);
    }
}

}
