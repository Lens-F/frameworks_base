/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Snapshot.h"

#include <SkCanvas.h>

namespace android {
namespace uirenderer {

///////////////////////////////////////////////////////////////////////////////
// Constructors
///////////////////////////////////////////////////////////////////////////////

Snapshot::Snapshot(): flags(0), previous(NULL), layer(NULL), fbo(0),
        invisible(false), empty(false), alpha(1.0f) {

    transform = &mTransformRoot;
    clipRect = &mClipRectRoot;
    region = NULL;
    clipRegion = NULL;
}

/**
 * Copies the specified snapshot/ The specified snapshot is stored as
 * the previous snapshot.
 */
Snapshot::Snapshot(const sp<Snapshot>& s, int saveFlags):
        flags(0), previous(s), layer(NULL), fbo(s->fbo),
        invisible(s->invisible), empty(false),
        viewport(s->viewport), height(s->height), alpha(s->alpha) {

    clipRegion = NULL;

    if (saveFlags & SkCanvas::kMatrix_SaveFlag) {
        mTransformRoot.load(*s->transform);
        transform = &mTransformRoot;
    } else {
        transform = s->transform;
    }

    if (saveFlags & SkCanvas::kClip_SaveFlag) {
        mClipRectRoot.set(*s->clipRect);
        clipRect = &mClipRectRoot;
#if STENCIL_BUFFER_SIZE
        if (s->clipRegion) {
            mClipRegionRoot.merge(*s->clipRegion);
            clipRegion = &mClipRegionRoot;
        }
#endif
    } else {
        clipRect = s->clipRect;
#if STENCIL_BUFFER_SIZE
        clipRegion = s->clipRegion;
#endif
    }

    if (s->flags & Snapshot::kFlagFboTarget) {
        flags |= Snapshot::kFlagFboTarget;
        region = s->region;
    } else {
        region = NULL;
    }
}

///////////////////////////////////////////////////////////////////////////////
// Clipping
///////////////////////////////////////////////////////////////////////////////

void Snapshot::ensureClipRegion() {
#if STENCIL_BUFFER_SIZE
    if (!clipRegion) {
        clipRegion = &mClipRegionRoot;
        android::Rect tmp(clipRect->left, clipRect->top, clipRect->right, clipRect->bottom);
        clipRegion->set(tmp);
    }
#endif
}

void Snapshot::copyClipRectFromRegion() {
#if STENCIL_BUFFER_SIZE
    if (!clipRegion->isEmpty()) {
        android::Rect bounds(clipRegion->bounds());
        clipRect->set(bounds.left, bounds.top, bounds.right, bounds.bottom);

        if (clipRegion->isRect()) {
            clipRegion->clear();
            clipRegion = NULL;
        }
    } else {
        clipRect->setEmpty();
        clipRegion = NULL;
    }
#endif
}

bool Snapshot::clipRegionOr(float left, float top, float right, float bottom) {
#if STENCIL_BUFFER_SIZE
    android::Rect tmp(left, top, right, bottom);
    clipRegion->orSelf(tmp);
    copyClipRectFromRegion();
    return true;
#else
    return false;
#endif
}

bool Snapshot::clipRegionXor(float left, float top, float right, float bottom) {
#if STENCIL_BUFFER_SIZE
    android::Rect tmp(left, top, right, bottom);
    clipRegion->xorSelf(tmp);
    copyClipRectFromRegion();
    return true;
#else
    return false;
#endif
}

bool Snapshot::clipRegionAnd(float left, float top, float right, float bottom) {
#if STENCIL_BUFFER_SIZE
    android::Rect tmp(left, top, right, bottom);
    clipRegion->andSelf(tmp);
    copyClipRectFromRegion();
    return true;
#else
    return false;
#endif
}

bool Snapshot::clipRegionNand(float left, float top, float right, float bottom) {
#if STENCIL_BUFFER_SIZE
    android::Rect tmp(left, top, right, bottom);
    clipRegion->subtractSelf(tmp);
    copyClipRectFromRegion();
    return true;
#else
    return false;
#endif
}

bool Snapshot::clip(float left, float top, float right, float bottom, SkRegion::Op op) {
    Rect r(left, top, right, bottom);
    transform->mapRect(r);
    return clipTransformed(r, op);
}

bool Snapshot::clipTransformed(const Rect& r, SkRegion::Op op) {
    bool clipped = false;

    switch (op) {
        case SkRegion::kDifference_Op: {
            ensureClipRegion();
            clipped = clipRegionNand(r.left, r.top, r.right, r.bottom);
            break;
        }
        case SkRegion::kIntersect_Op: {
            if (CC_UNLIKELY(clipRegion)) {
                clipped = clipRegionOr(r.left, r.top, r.right, r.bottom);
            } else {
                clipped = clipRect->intersect(r);
                if (!clipped) {
                    clipRect->setEmpty();
                    clipped = true;
                }
            }
            break;
        }
        case SkRegion::kUnion_Op: {
            if (CC_UNLIKELY(clipRegion)) {
                clipped = clipRegionAnd(r.left, r.top, r.right, r.bottom);
            } else {
                clipped = clipRect->unionWith(r);
            }
            break;
        }
        case SkRegion::kXOR_Op: {
            ensureClipRegion();
            clipped = clipRegionXor(r.left, r.top, r.right, r.bottom);
            break;
        }
        case SkRegion::kReverseDifference_Op: {
            // TODO!!!!!!!
            break;
        }
        case SkRegion::kReplace_Op: {
            setClip(r.left, r.top, r.right, r.bottom);
            clipped = true;
            break;
        }
    }

    if (clipped) {
        flags |= Snapshot::kFlagClipSet;
    }

    return clipped;
}

void Snapshot::setClip(float left, float top, float right, float bottom) {
    clipRect->set(left, top, right, bottom);
#if STENCIL_BUFFER_SIZE
    if (clipRegion) {
        clipRegion->clear();
        clipRegion = NULL;
    }
#endif
    flags |= Snapshot::kFlagClipSet;
}

const Rect& Snapshot::getLocalClip() {
    mat4 inverse;
    inverse.loadInverse(*transform);

    mLocalClip.set(*clipRect);
    inverse.mapRect(mLocalClip);

    return mLocalClip;
}

void Snapshot::resetClip(float left, float top, float right, float bottom) {
    clipRect = &mClipRectRoot;
    setClip(left, top, right, bottom);
}

///////////////////////////////////////////////////////////////////////////////
// Transforms
///////////////////////////////////////////////////////////////////////////////

void Snapshot::resetTransform(float x, float y, float z) {
    transform = &mTransformRoot;
    transform->loadTranslate(x, y, z);
}

///////////////////////////////////////////////////////////////////////////////
// Queries
///////////////////////////////////////////////////////////////////////////////

bool Snapshot::isIgnored() const {
    return invisible || empty;
}

}; // namespace uirenderer
}; // namespace android
