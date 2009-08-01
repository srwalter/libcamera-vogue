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

#ifndef ANDROID_HARDWARE_VOGUECAMERA_H
#define ANDROID_HARDWARE_VOGUECAMERA_H

#include <ui/CameraHardwareInterface.h>

namespace android {

class VogueCamera {
public:
	VogueCamera(int width, int height);
	~VogueCamera();
	void start(void);
	void stop(void);
	
	void setSize(int width, int height);
	void getNextFrameAsYuv422(uint8_t *buffer);
	void getRawSnapshot(uint8_t *buffer);
	status_t dump(int fd, const Vector<String16>& args);
	
private:
	int vfe_dev;
	char *frame_buffer;
	int mWidth, mHeight;
};

}; // namespace android

#endif // ANDROID_HARDWARE_VOGUECAMERA_H
