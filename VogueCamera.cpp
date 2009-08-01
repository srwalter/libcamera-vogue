#define LOG_TAG "VogueCamera"
#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include "VogueCamera.h"

namespace android {

const int buffersize=320*240+320*240/2;

VogueCamera::VogueCamera(int width, int height) :vfe_dev(-1)
{
	LOGI("Vogue Camera %d %d",width,height);
	setSize(width, height);
}

VogueCamera::~VogueCamera()
{
	stop();
}

void VogueCamera::start(void) {
	LOGI("Vogue Camera Preview Start");
	if(vfe_dev!=-1)
		return;
	vfe_dev=open("/dev/vfe",O_RDONLY);
	if(vfe_dev<0) {
		LOGE("Vogue Camera: /dev/vfe not found");
		return;
	}
	frame_buffer=(char *)mmap(NULL,buffersize*4,PROT_READ,MAP_SHARED,vfe_dev,0);
	LOGI("frame buffer=%x\n",(unsigned)frame_buffer);
	if(frame_buffer==MAP_FAILED)
		frame_buffer=0;
}

void VogueCamera::stop(void) {
	LOGI("Vogue Camera Preview Stop");
	close(vfe_dev);
	vfe_dev==-1;
}

void VogueCamera::setSize(int width, int height)
{
	mWidth = width;
	mHeight = height;
}

void VogueCamera::getNextFrameAsYuv422(uint8_t *buffer)
{
	unsigned offset;
	
	if(vfe_dev==-1)
		return;
	read(vfe_dev,&offset,4);
	//	LOGI("get next frame: %x=%x\n",(unsigned)offset,*(frame_buffer+offset));
	memcpy(buffer,frame_buffer+offset,buffersize);
}


status_t VogueCamera::dump(int fd, const Vector<String16>& args)
{
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;
	snprintf(buffer, 255, " width x height (%d x %d)\n", mWidth, mHeight);
	result.append(buffer);
	::write(fd, result.string(), result.size());
	return NO_ERROR;
}


}; // namespace android
