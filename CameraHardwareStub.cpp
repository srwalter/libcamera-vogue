/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "CameraHardwareStub"
#include <utils/Log.h>

#include "CameraHardwareStub.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "CannedJpeg.h"

namespace android {

CameraHardwareStub::CameraHardwareStub()
                  : mParameters(),
                    mPreviewHeap(0),
                    mRawHeap(0),
                    mFakeCamera(0),
                    mPreviewFrameSize(0),
                    mRawPictureCallback(0),
                    mJpegPictureCallback(0),
                    mPictureCallbackCookie(0),
                    mPreviewCallback(0),
                    mPreviewCallbackCookie(0),
                    mAutoFocusCallback(0),
                    mAutoFocusCallbackCookie(0),
                    mCurrentPreviewFrame(0)
{
    initDefaultParameters();
}

void CameraHardwareStub::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(176, 144);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("yuv422sp");

    p.setPictureSize(kCannedJpegWidth, kCannedJpegHeight);
    p.setPictureFormat("jpeg");

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    } 
}

void CameraHardwareStub::initHeapLocked()
{
    // Create raw heap.
    int picture_width, picture_height;
    mParameters.getPictureSize(&picture_width, &picture_height);
    mRawHeap = new MemoryHeapBase(picture_width * 2 * picture_height);

    int preview_width, preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    LOGD("initHeapLocked: preview size=%dx%d", preview_width, preview_height);

    // Note that we enforce yuv422 in setParameters().
    int how_big = preview_width * preview_height * 2;

    // If we are being reinitialized to the same size as before, no
    // work needs to be done.
    if (how_big == mPreviewFrameSize)
        return;

    mPreviewFrameSize = how_big;

    // Make a new mmap'ed heap that can be shared across processes. 
    // use code below to test with pmem
    mPreviewHeap = new MemoryHeapBase(mPreviewFrameSize * kBufferCount);
    // Make an IMemory for each frame so that we can reuse them in callbacks.
    for (int i = 0; i < kBufferCount; i++) {
        mBuffers[i] = new MemoryBase(mPreviewHeap, i * mPreviewFrameSize, mPreviewFrameSize);
    }
    
    // Recreate the fake camera to reflect the current size.
    delete mFakeCamera;
    mFakeCamera = new VogueCamera(preview_width, preview_height);
}

CameraHardwareStub::~CameraHardwareStub()
{
    delete mFakeCamera;
    mFakeCamera = 0; // paranoia
    singleton.clear();
}

sp<IMemoryHeap> CameraHardwareStub::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHardwareStub::getRawHeap() const
{
    return mRawHeap;
}

// ---------------------------------------------------------------------------

int CameraHardwareStub::previewThread()
{
    mLock.lock();
        // the attributes below can change under our feet...

        int previewFrameRate = mParameters.getPreviewFrameRate();

        // Find the offset within the heap of the current buffer.
        ssize_t offset = mCurrentPreviewFrame * mPreviewFrameSize;

        sp<MemoryHeapBase> heap = mPreviewHeap;
    
        // this assumes the internal state of fake camera doesn't change
        // (or is thread safe)
        VogueCamera* fakeCamera = mFakeCamera;
        
        sp<MemoryBase> buffer = mBuffers[mCurrentPreviewFrame];
        
    mLock.unlock();

    // TODO: here check all the conditions that could go wrong
    if (buffer != 0) {
        // Calculate how long to wait between frames.
        int delay = (int)(1000000.0f / float(previewFrameRate));
    
        // This is always valid, even if the client died -- the memory
        // is still mapped in our process.
        void *base = heap->base();
    
        // Fill the current frame with the fake camera.
        uint8_t *frame = ((uint8_t *)base) + offset;
        fakeCamera->getNextFrameAsYuv422(frame);
    
        //LOGV("previewThread: generated frame to buffer %d", mCurrentPreviewFrame);
        
        // Notify the client of a new frame.
        mPreviewCallback(buffer, mPreviewCallbackCookie);
    
        // Advance the buffer pointer.
        mCurrentPreviewFrame = (mCurrentPreviewFrame + 1) % kBufferCount;

        // Wait for it...
        usleep(delay);
    }

    return NO_ERROR;
}

status_t CameraHardwareStub::startPreview(preview_callback cb, void* user)
{
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }
    mPreviewCallback = cb;
    mPreviewCallbackCookie = user;
    mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}

void CameraHardwareStub::stopPreview()
{
    sp<PreviewThread> previewThread;
    
    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool CameraHardwareStub::previewEnabled() {
    return mPreviewThread != 0;
}

status_t CameraHardwareStub::startRecording(recording_callback cb, void* user)
{
    return UNKNOWN_ERROR;
}

void CameraHardwareStub::stopRecording()
{
}

bool CameraHardwareStub::recordingEnabled()
{
    return false;
}

void CameraHardwareStub::releaseRecordingFrame(const sp<IMemory>& mem)
{
}

// ---------------------------------------------------------------------------

int CameraHardwareStub::beginAutoFocusThread(void *cookie)
{
    CameraHardwareStub *c = (CameraHardwareStub *)cookie;
    return c->autoFocusThread();
}

int CameraHardwareStub::autoFocusThread()
{
    if (mAutoFocusCallback != NULL) {
        mAutoFocusCallback(true, mAutoFocusCallbackCookie);
        mAutoFocusCallback = NULL;
        return NO_ERROR;
    }
    return UNKNOWN_ERROR;
}

status_t CameraHardwareStub::autoFocus(autofocus_callback af_cb,
                                       void *user)
{
    Mutex::Autolock lock(mLock);

    if (mAutoFocusCallback != NULL) {
        return mAutoFocusCallback == af_cb ? NO_ERROR : INVALID_OPERATION;
    }

    mAutoFocusCallback = af_cb;
    mAutoFocusCallbackCookie = user;
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

/*static*/ int CameraHardwareStub::beginPictureThread(void *cookie)
{
    CameraHardwareStub *c = (CameraHardwareStub *)cookie;
    return c->pictureThread();
}

int CameraHardwareStub::pictureThread()
{
    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);

    if (mRawPictureCallback) {
        //FIXME: use a canned YUV image!
        // In the meantime just make another fake camera picture.
        int w, h;
        mParameters.getPictureSize(&w, &h);
        sp<MemoryBase> mem = new MemoryBase(mRawHeap, 0, w * 2 * h);
        VogueCamera cam(w, h);
        cam.getNextFrameAsYuv422((uint8_t *)mRawHeap->base());
        if (mRawPictureCallback)
            mRawPictureCallback(mem, mPictureCallbackCookie);
    }

    if (mJpegPictureCallback) {
        sp<MemoryHeapBase> heap = new MemoryHeapBase(kCannedJpegSize);
        sp<MemoryBase> mem = new MemoryBase(heap, 0, kCannedJpegSize);
        memcpy(heap->base(), kCannedJpeg, kCannedJpegSize);
        if (mJpegPictureCallback)
            mJpegPictureCallback(mem, mPictureCallbackCookie);
    }
    return NO_ERROR;
}

status_t CameraHardwareStub::takePicture(shutter_callback shutter_cb,
                                         raw_callback raw_cb,
                                         jpeg_callback jpeg_cb,
                                         void* user)
{
    stopPreview();
    mShutterCallback = shutter_cb;
    mRawPictureCallback = raw_cb;
    mJpegPictureCallback = jpeg_cb;
    mPictureCallbackCookie = user;
    if (createThread(beginPictureThread, this) == false)
        return -1;
    return NO_ERROR;
}

status_t CameraHardwareStub::cancelPicture(bool cancel_shutter,
                                           bool cancel_raw,
                                           bool cancel_jpeg)
{
    if (cancel_shutter) mShutterCallback = NULL;
    if (cancel_raw) mRawPictureCallback = NULL;
    if (cancel_jpeg) mJpegPictureCallback = NULL;
    return NO_ERROR;
}

status_t CameraHardwareStub::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);
    if (mFakeCamera != 0) {
        mFakeCamera->dump(fd, args);
        mParameters.dump(fd, args);
        snprintf(buffer, 255, " preview frame(%d), size (%d), running(%s)\n", mCurrentPreviewFrame, mPreviewFrameSize, mPreviewRunning?"true": "false");
        result.append(buffer);
    } else {
        result.append("No camera client yet.\n");
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t CameraHardwareStub::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);
    // XXX verify params

    if (strcmp(params.getPreviewFormat(), "yuv422sp") != 0) {
        LOGE("Only yuv422sp preview is supported");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int w, h;
    params.getPictureSize(&w, &h);
    if (w != kCannedJpegWidth && h != kCannedJpegHeight) {
        LOGE("Still picture size must be size of canned JPEG (%dx%d)",
             kCannedJpegWidth, kCannedJpegHeight);
        return -1;
    }

    mParameters = params;

    initHeapLocked();

    return NO_ERROR;
}

CameraParameters CameraHardwareStub::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

void CameraHardwareStub::release()
{
}

wp<CameraHardwareInterface> CameraHardwareStub::singleton;

sp<CameraHardwareInterface> CameraHardwareStub::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardwareStub());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    return CameraHardwareStub::createInstance();
}

}; // namespace android